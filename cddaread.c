/*******************************************************************
 * $Id$
 *
 * CD-DA to digital audio file conversion utility for SGI
 *
 * Copyright (c) Timo Kokkonen, 1996-1998.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sigfpe.h>
#include <dmedia/cdaudio.h>
#include <dmedia/audio.h>
#include <dmedia/audiofile.h>
#include <stdio.h>
#if HAVE_GETOPT_H && HAVE_GETOPT_LONG
#include <getopt.h>
#else
#include "getopt.h"
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define VERSION "1.4"
#define PRGNAME "cddaread"

#define MAX_TRACKS 99
#define AIFFC_HEADER_SIZE 156
#define FRAMES_PER_READ 12

static char rcsid[] = "$Id$";

/* buffer for audio playback */
static char playbuf[FRAMES_PER_READ*CDDA_DATASIZE]; 
static int  playoffs = 0;


static ALport audioport;
static AFfilehandle outfile;
static AFfilesetup aiffsetup;

static int verbose_mode = 0;
static int quiet_mode = 0;
static int all_mode = 0;
static int sound_on = 0;

static char *outfname = NULL;
static long start,stop,len,pos;
static int skip;
static int counter = 0;

/*****************************************************************/

void warn(char *format, ...)
{
  va_list args;

  fprintf(stderr, PRGNAME ": ");
  va_start(args,format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr,"\n");
  fflush(stderr);
}

void die(char *format, ...)
{
  va_list args;

  fprintf(stderr, PRGNAME ": ");
  va_start(args,format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr,"\n");
  fflush(stderr);
  exit(1);
}


/****************************************************************/

void no_memory(void)
{
  if (!quiet_mode) warn("not enough memory.");
  exit(3);
}


static struct option long_options[] = {
  {"ALL",0,0,'A'},
  {"aiff",0,0,'f'},
  {"aiff-c",0,0,'c'},
#ifdef IRIX6
  {"next",0,0,'n'},
  {"mpeg-1",0,0,'m'},
  {"mpeg-2",0,0,'M'},
#endif
  {"all",0,0,'a'},
  {"device",1,0,'d'},
  {"help",0,0,'h'},
  {"info",0,0,'i'},
  {"no-of-tracks",0,0,'I'},
  {"quiet",0,0,'q'},
  {"sound",0,0,'s'},
  {"track",1,0,'t'},
  {"verbose",0,0,'v'},
  {"version",0,0,'z'},
  {NULL,0,0,0}
};

void p_usage(void) 
{
 if (!quiet_mode) {
  fprintf(stderr, PRGNAME " v"  VERSION "  " HOST_TYPE
	  "  Copyright (c) Timo Kokkonen, 1996-1998.\n"); 

  fprintf(stderr,
       "Usage:  " PRGNAME " [options] <targetfile> \n\n"
       "  -d<device>, --device=<device>\n"
       "                   specify scsi device to use (default: first cd-rom found)\n"
       "  -a, --all        read all tracks into one file\n"
       "  -A, --ALL        read all tracks into separate files\n"
       "  -h, --help       display this help and exit\n"
       "  -i, --info       display disc info and exit\n"
       "  -q, --quiet      quiet mode (display only fatal errors)\n"
       "  -s, --sound      enable audio\n"
       "  -v, --verbose    enable verbose mode (positively chatty)\n"
       "  -t<number>[,<number>,...], --track=<number>[,<number>,...]\n"
       "                   read specified track(s)\n"
       "  --no-of-tracks   display only no. of tracks on disc and exit\n"
       "  --version        display program version and exit\n"
       " Set output file format to:\n"
       "  --aiff           AIFF, Audio Interchange File Format\n"
       "  --aiff-c         AIFF-C, extended Audio Interchange File Format (default)\n"
#ifdef IRIX6
       "  --mpeg-1         ISO/MPEG-1 audio layer I, 192Kbps/channel\n"
       "  --mpeg-2         ISO/MPEG-1 audio layer II, 128Kbps/channel\n"
       "  --next           NeXT .snd and Sun .au format\n"
#endif
       "");
 }

 exit(1);
}


long file_size(FILE *fp)
{
  struct stat buf;
  
  if (fstat(fileno(fp),&buf)) return -1;
  return buf.st_size;
}



void own_signal_handler(int a)
{
  if (verbose_mode) warn("\ngot signal (%d)\nAborting...",a);
  if (outfile) AFclosefile(outfile);
  exit(1);
}

/*****************************************************************/

void parseaudiodata(void *arg, CDDATATYPES type, short *audio)
{
  counter++;
  if ((counter%100==1) && !quiet_mode) { printf("."); fflush(stdout); }

  if ((counter>len) && !(all_mode==1)) return;

  if (sound_on) {
    memcpy(&playbuf[playoffs*CDDA_DATASIZE],audio,CDDA_DATASIZE);
    playoffs++;
    if (playoffs >= FRAMES_PER_READ) {
      ALwritesamps(audioport, playbuf, CDDA_NUMSAMPLES*FRAMES_PER_READ); 
      playoffs=0; 
    }
  }
  
  /* if (counter>skip) */
  AFwriteframes(outfile,AF_DEFAULT_TRACK,audio,CDDA_NUMSAMPLES/2);
}


/*****************************************************************/
int main(int argc, char **argv) 
{
  CDPLAYER *cd;
  CDSTATUS status;
  CDPARSER *cdp = CDcreateparser();
  CDTRACKINFO info;
  CDFRAME *buf;
  FILE *fp;
  int tracks[MAX_TRACKS+1],tcount;
  int file_format = AF_FILE_AIFFC;  /* set default fileformat to AIFF-C */
  int file_compression = AF_COMPRESSION_NONE;
  int opt_index = 0;
  int info_mode = 0;
  int c, i, j;
  char *devname = NULL, *s;
  int retries;
  int track = 0;
  int blocks = 12;
  long sum_lba=0,sum_filesize=0;
  char namebuf[1024];

  if (rcsid); /* just to keep compiler happy :) */

  memset(tracks,0,sizeof(tracks));

  if (argc<2) {
    if (!quiet_mode) warn("arguments missing\n"
			  "Try '" PRGNAME " --help' for more information.");
    exit(1);
  }
 
  /* parse command line parameters */
  while(1) {
    opt_index=0;
    if ((c=getopt_long(argc,argv,"d:t:hqvsiaA",long_options,&opt_index))
	 == -1) break;
    switch (c) {
    case 'a':
      all_mode=1;
      break;
    case 'A':
      all_mode=2;
      break;
    case 'd':
      devname=strdup(optarg);
      break;
    case 'f':
      file_format=AF_FILE_AIFF;
      break;
    case 'c':
      file_format=AF_FILE_AIFFC;
      break;
#ifdef IRIX6
    case 'n':
      file_format=AF_FILE_NEXTSND;
      break;
    case 'm':
      file_format=AF_FILE_MPEG1BITSTREAM;
      file_compression=AF_COMPRESSION_DEFAULT_MPEG_I;
      break;
    case 'M':
      file_format=AF_FILE_MPEG1BITSTREAM;
      file_compression=AF_COMPRESSION_DEFAULT_MPEG_II;
      break;
#endif
    case 't':
      s=strtok(optarg,",");
      while (s) {
	if (sscanf(s,"%d",&track)!=1) 
	  die("Invalid option argument for option -t");
	if (track > 0 && track <= MAX_TRACKS) tracks[track]=1;
	else warn("Invalid track number '%d'",track);
	s=strtok(NULL,",");
      }
      break;
    case 'v':
      verbose_mode=1;
      break;
    case 'i':
      info_mode=2;
      break;
    case 'I':
      info_mode=1;
      break;
    case 'h':
      p_usage();
      break;
    case 'q':
      quiet_mode=1;
      break;
    case 's':
      sound_on=1;
      break;
    case 'z':
      printf(PRGNAME " v"  VERSION "  " HOST_TYPE
	     "\nCopyright (c) Timo Kokkonen, 1996-1998.\n\n"); 
      exit(0);
      break;
    case '?':
      exit(1);
      break;

    default:
      if (!quiet_mode) warn("error parsing parameters.");
    }
  }

  if (quiet_mode) verbose_mode=0;
  if (optind>=argc && !info_mode) die("no target file specified.");
  else if (!info_mode) outfname=strdup(argv[optind]);

  if (verbose_mode) {
    printf("CDDAread v" VERSION "\n");
    printf("Using CD-ROM device: %s\n",(devname?devname:"default"));
    printf("Audio: %s\n",(sound_on?"on":"off"));
    printf("Target file: %s (AIFF%s)\n",outfname?outfname:"N/A",
	   file_format==AF_FILE_AIFFC?"-C":"");
    fflush(stdout);
  }


  /* open the device */
  cd = CDopen(devname,"r");
  if (!cd) {
    if (errno==ENODEV) die("No AudioCD in drive.");
    if (errno==EACCES) die("Permission dedied. Cannot open device.");
    die("Cannot open device (%d)",errno);
  }
  if (!CDgetstatus(cd,&status)) die("Cannot get device status (%d).",errno);
  if (!status.scsi_audio) die("This drive doesn't support audio xfers.");

  /* check for Audio CD */
  if (status.state!=CD_READY) {
    retries=4;
    while (--retries > 0) {
      sleep(2);
      CDgetstatus(cd,&status);
      if (status.state==CD_READY) break;
    }
    if (status.state!=CD_READY) die("No Audio CD in drive.");
  }
  

  /* blocks=CDbestreadsize(cd); */
  blocks=FRAMES_PER_READ;
  buf=malloc(sizeof(CDFRAME)*blocks);
  if (!buf) no_memory();
  

  if (verbose_mode) {
    printf("Disc info: total len   = %2d:%02d.%02d\n"
           "           first track = %d\n"
           "           last track  = %d\n",
	   status.total_min,status.total_sec,status.total_frame,
	   status.first,status.last);
  }
  
  if (info_mode) {
    if (info_mode>1) {
      printf("Audio             Size     Size     Projected    \n");
      printf("Track   Length    LBAs     Kbytes   AIFF-C file size\n");
      printf("----------------------------------------------------\n");
      for(i=status.first;i<=status.last;i++) {
	if (!CDgettrackinfo(cd,i,&info)) die("Cannot get track info");
	len=CDmsftoframe(info.total_min,info.total_sec,info.total_frame);
	printf(" %02d.   %2d:%02d.%02d  %6ld %8ldk %11ld bytes\n",i,
	       info.total_min,info.total_sec,info.total_frame,
	       len,len*CDDA_DATASIZE/1024,
	       len*CDDA_DATASIZE+AIFFC_HEADER_SIZE);
	sum_lba+=len; 
	sum_filesize+=len*CDDA_DATASIZE+AIFFC_HEADER_SIZE;
	fflush(stdout);
      }
      printf("----------------------------------------------------\n");
      printf(" %02d.   %2ld:%02ld.%02ld  %6ld %8ldk %11ld bytes\n",
	     status.last,
	     sum_lba/(75*60),(sum_lba%(75*60))/75,sum_lba%75,
	     sum_lba,
	     sum_lba*CDDA_DATASIZE/1024,
	     sum_filesize); 
    } else
      printf("Audio tracks on the disc: %d\n",1+status.last-status.first);

    exit(0);
  }

  /* initialize audio output */
  if (sound_on) {
    audioport=ALopenport("CDDAread","w",0);
    if (!audioport) {
      sound_on=0;
      warn("Cannot access audio hardware.");
    }
  }

  /* make sure we get sane underflow exception handling */
  sigfpe_[_UNDERFL].repls = _ZERO;
  handle_sigfpes(_ON, _EN_UNDERFL, NULL, _ABORT_ON_ERROR, NULL);

  if (cdp) {
    CDaddcallback(cdp, cd_audio, (CDCALLBACKFUNC)parseaudiodata, 0);
  } else die("Cannot initialize audioparser.");

  /* initialize signal handlers */
  signal(SIGINT,own_signal_handler);
  signal(SIGHUP,own_signal_handler);
  signal(SIGTERM,own_signal_handler);


  /* check selected tracks */
  tcount=0;
  for(i=1;i<=MAX_TRACKS;i++) {
    if (i<status.first || i>status.last) tracks[i]=0;
    else if (all_mode) tracks[i]=1;
    if (tracks[i]) tcount++;
  }
  if (tcount==0) {
    tracks[status.first]=1;
    tcount++;
  }
  if (verbose_mode) printf("No. of tracks scheduled to be read: %d\n",tcount);


  /* read audio track(s)... */
  for(track=status.first;track<=status.last;track++) {
    if (!tracks[track]) continue;

    /* Open output AIFF-C file */
    if (all_mode==1 || tcount==1) sprintf(namebuf,"%s",outfname);
    else sprintf(namebuf,"%s.%02d",outfname,track);
    aiffsetup=AFnewfilesetup();
    AFinitrate(aiffsetup,AF_DEFAULT_TRACK,44100.0); /* 44.1 kHz */
    AFinitfilefmt(aiffsetup,file_format);           /* set file format */
    AFinitchannels(aiffsetup,AF_DEFAULT_TRACK,2);   /* stereo */
    AFinitsampfmt(aiffsetup,AF_DEFAULT_TRACK,
		  AF_SAMPFMT_TWOSCOMP,16);          /* 16bit */
#ifdef IRIX6
    if (file_format == AF_FILE_MPEG1BITSTREAM)      /* set compression */
      afInitCompression(aiffsetup,AF_DEFAULT_TRACK,file_compression);
#endif

    outfile=AFopenfile(namebuf,"w",aiffsetup);
    if (!outfile) die("Cannot open target file (%s).",namebuf);
    counter=0;

    if (!CDgettrackinfo(cd,track,&info)) 
      die("Cannot get track info (%d).",errno);

    start=CDmsftoframe(info.start_min,info.start_sec,info.start_frame);
    len=CDmsftoframe(info.total_min,info.total_sec,info.total_frame);
    stop=start+len;

    if (verbose_mode && !(all_mode==1)) {
      printf("Track info: %d\n"
	     "            length = %2d:%02d.%02d (frames=%ld  bytes=%ld)\n"
	     "            start  = %2d:%02d.%02d (LBA=%ld)\n",
	     track,
	     info.total_min,info.total_sec,info.total_frame,len,
	     len*CDDA_DATASIZE,
	     info.start_min,info.start_sec,info.start_frame,start);
    }
    
    if (!quiet_mode) {
      if (!(all_mode==1))  printf("Reading audio track: %d\n",track);
      else printf("Reading audio cd starting at track: %d\n",track);
    }

    pos=CDseek(cd,info.start_min,info.start_sec,info.start_frame);
    if (pos<start) skip=start-pos; else skip=0;
    /* printf("pos=%ld : skip=%d\n",pos,skip); */
 
    /* read the audio data from disc */
    while (1) {
      i=CDreadda(cd,buf,blocks);
      if (i<0) die("Error reading audio track (%d).",errno);
      if (i==0) break; /* end of disc */
      if ((counter> len) && !(all_mode==1)) break; /* track finished */
      for (j=0;j<i;j++) CDparseframe(cdp,&buf[j]);
    }
    
    /* 'recording' done, time to clean up... */
    AFclosefile(outfile);

    if (!quiet_mode) {
      long size;
      printf("\nDone.\n");
      fp=fopen(namebuf,"r");
      if (fp) {
	size=file_size(fp);
	printf("Output filesize: %ldbytes (%ldk)\n",size,size/1024);
	fclose(fp);
      }
    }

    if (all_mode==1) break;
  } /* end of for loop */


  CDclose(cd);
  return 0;
}

/* :-) */
