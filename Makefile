########################################################################
# $Id$
#
# Makefile for CDDAread for SGIs
#
#
Version = 1.3
PKGNAME = cddaread

DEFINES = -DSGI 

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/man/man1
USER	= root
GROUP	= root


INCLUDES =  -I/usr/local/include
LIBDIRS  =  -L/usr/local/lib

CC     = cc
CFLAGS = -g $(DEFINES) $(INCLUDES)
LIBS   = -lcdaudio -laudio -lmediad -laudiofile -laudioutil -lds -lm -lfpe  $(LIBDIRS)
STRIP  = strip



# should be no reason to modify lines below this
#########################################################################

DIRNAME = $(shell basename `pwd`) 
DISTNAME  = $(PKGNAME)-$(Version)


$(PKGNAME):	$(PKGNAME).c
	$(CC) $(CFLAGS) -o $(PKGNAME) $(PKGNAME).c $(LIBS) 

all:	$(PKGNAME) 

strip:
	$(STRIP) $(PKGNAME)

clean:
	rm -f *~ *.o core a.out make.log $(PKGNAME)

dist:	clean
	(cd .. ; tar -cvzf $(DISTNAME).tar.gz --exclude RCS $(DIRNAME))

backup:	clean
	(cd .. ; tar cvzf $(DISTNAME).full.tar.gz $(DIRNAME))

zip:	clean	
	(cd .. ; zip -r9 $(DISTNAME).zip $(DIRNAME))

install: all  install.man
	install -m 755 -u $(USER) -g $(GROUP) -f $(BINDIR) $(PKGNAME)

printable.man:
	groff -Tps -mandoc ./$(PKGNAME).1 >$(PKGNAME).ps
	groff -Tascii -mandoc ./$(PKGNAME).1 | tee $(PKGNAME).prn | sed 's/.//g' >$(PKGNAME).txt

install.man:
	install -m 0644 -u $(USER) -g $(GROUP) -f $(MANDIR) $(PKGNAME).1


# a tradition !
love:	
	@echo "Not War - Eh?"
# eof

