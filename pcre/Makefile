
PCRE_MAJOR=5
PCRE_MINOR=0
PCRE_DATE=13-Sep-2004

CFLAGS=/Zi /MD -I. -DSTATIC -DNEWLINE='\n' -DHAVE_MEMMOVE=1 -DHAVE_STERROR=1 -DLINK_SIZE=2 -DEXPORT= -DSUPPORT_UTF8 -DPOSIX_MALLOC_THRESHOLD=5 -DMATCH_LIMIT=10000000

OBJS=maketables.obj get.obj pcre.obj study.obj

all: config.h pcre.h chartables.c pcre.lib

#pcre.h: pcre.in
#	sh -c "sed -e 's/@PCRE_MAJOR@/$(PCRE_MAJOR)/' -e 's/@PCRE_MINOR@/$(PCRE_MINOR)/' -e 's/@PCRE_DATE@/\"$(PCRE_DATE)\"/' < pcre.in > pcre.h"

pcre.lib: $(OBJS)
	lib -nologo /out:pcre.lib $(OBJS)

chartables.c: dftables.exe
	dftables.exe chartables.c

dftables.exe: dftables.obj
	link -nologo /out:dftables.exe dftables.obj

config.h:
	echo /**/ > config.h

clean:
	del *.obj *.lib

