# this is a makefile for consumption with nmake.
# You need the MS compiler tools to build this software.
CYGBIN=C:\cygwin\bin
WIX=c:\program files\wix-2.0.4820.0
CANDLE="$(WIX)\candle.exe" /nologo
LIGHT="$(WIX)\light.exe" /nologo
VC32=$(VCINSTALLDIR)\bin
PCRE_MAJOR=5
PCRE_MINOR=0
PCRE_DATE=13-Sep-2004
DBG=$(PROGRAMFILES)\Debugging Tools for Windows\sdk
DBGINC=$(DBG)\inc
REDIST32=$(VCINSTALLDIR)\redist\x86
REDIST64=$(VCINSTALLDIR)\redist\amd64

!if "$(PROCESSOR_ARCHITECTURE)" == "AMD64"
VC64=$(VCINSTALLDIR)\bin\amd64
VARS64="$(VC64)\vcvarsamd64.bat"
ARCH=amd64
DBGLIB=$(DBG)\lib\amd64
!else
VC64=$(VCINSTALLDIR)\bin\x86_amd64
VARS64="$(VC64)\vcvarsx86_amd64.bat"
DBGLIB=$(DBG)\lib\i386
!endif

!if "$(ARCH)" == "amd64"
#PATH=$(VC64);$(PATH)
OUTDIR=amd64
MACHINE=amd64
ABITS=64
!else
#PATH=$(VC32);$(PATH)
OUTDIR=x86
MACHINE=x86
ABITS=32
!endif

# Base version for a release.
VERSION=0.9.0

HDRS=wezdesk.h wezdeskapi.h wezdeskres.h wezdeskres-symbols.h
LIBS=shell32.lib advapi32.lib user32.lib shlwapi.lib ole32.lib \
	comctl32.lib gdi32.lib gdiplus.lib wintrust.lib Wtsapi32.lib \
	psapi.lib strsafe.lib \
	dbghelp.lib

#"$(DBGLIB)\dbghelp.lib"

DEBUG=0
!if "$(DEBUG)" == "1"
CPPFLAGS=/nologo /GA /Zi /EHsc /MDd /Ipcre /DDEBUG_MEM
#/I"$(DBGINC)"
!else
CPPFLAGS=/nologo /GA /Zi /EHsc /MD /Ipcre
#/I"$(DBGINC)"
!endif

#CPPFLAGS=/nologo /Zi /EHsc /MDd /DDEBUG_MEM
CFLAGS=$(CPPFLAGS)
PCRE_CFLAGS=$(CFLAGS) \
	-DSTATIC -DNEWLINE='\n' -DHAVE_MEMMOVE=1 -DHAVE_STERROR=1 \
	-DLINK_SIZE=2 -DEXPORT= -DSUPPORT_UTF8 -DPOSIX_MALLOC_THRESHOLD=5 \
	-DMATCH_LIMIT=10000000 -DPCRE_STATIC=extern \
	/Fd"$(OUTDIR)\wezdesk.pdb"

CORE_CFLAGS=$(CFLAGS) \
	/Fd"$(OUTDIR)\wezdesk.pdb" -DPCRE_STATIC=extern

DLLFLAGS=$(CFLAGS) /LD
COOK_EXE_MANIFEST=if exist $@.manifest mt.exe -nologo -manifest $@.manifest -outputresource:$@;1 & del $@.manifest
COOK_DLL_MANIFEST=if exist $@.manifest mt.exe -nologo -manifest $@.manifest -outputresource:$@;2 & del $@.manifest

EXTENSIONS=\
	$(OUTDIR)\tray.dll \
	$(OUTDIR)\clock.dll \
	$(OUTDIR)\flasher.dll \
	$(OUTDIR)\quicklaunch.dll \
	$(OUTDIR)\putty.dll \
	$(OUTDIR)\overview.dll \
	$(OUTDIR)\dock.dll \
	$(OUTDIR)\taskswitch.dll \
	$(OUTDIR)\launcher.dll \
!if "$(ARCH)" == "amd64"
	$(OUTDIR)\wdmenu64.dll \
!else
	$(OUTDIR)\wdmenu32.dll \
!endif


NATIVE_BITS=\
	pcre\dftables.exe

all: $(OUTDIR) $(NATIVE_BITS) $(OUTDIR)\wezdesk.exe $(EXTENSIONS) i18n-dll hook-host

!if "$(ARCH)" == "amd64"
hook-host:
!else
hook-host: $(OUTDIR)\wezdesk-hh32.exe
!endif

both: $(NATIVE_BITS)
	<<build-both.bat
	call "$(VC32)\vcvars32.bat"
	nmake ARCH=x86 all
	call $(VARS64)
	nmake ARCH=amd64 all
<<NOKEEP

64bit: $(NATIVE_BITS)
	<<build-both.bat
	call $(VARS64)
	nmake ARCH=amd64 all
<<NOKEEP

$(OUTDIR):
	@if not exist $(OUTDIR) mkdir $(OUTDIR)

installer: evildesk.msi

pkgvars: mkguid.sh
	$(CYGBIN)\bash.exe mkguid.sh > pkgvars.cmd

evildesk.msi:	installer.wxs both web\*.html pkgvars
	<<make-installer.bat
	call pkgvars.cmd
	$(CANDLE) -dVERSION="$(VERSION).%SVNVERS%" -dREDIST="$(REDIST32)" -dARCH=Intel -dWIN64=no -dOUTDIR=x86 -out inst32.wixobj installer.wxs
	$(CANDLE) -dVERSION="$(VERSION).%SVNVERS%" -dREDIST32="$(REDIST32)" -dREDIST="$(REDIST64)" -dARCH=x64 -dWIN64=yes -dOUTDIR=amd64 -out inst64.wixobj installer.wxs
	$(CANDLE) -dVERSION="$(VERSION).%SVNVERS%" msi/uisample.wxs
	$(LIGHT) -v /o evildesk-$(VERSION).%SVNVERS%.msi inst32.wixobj uisample.wixobj
	$(LIGHT) -v /o evildesk-$(VERSION).%SVNVERS%-win64.msi inst64.wixobj uisample.wixobj
	copy /y evildesk-$(VERSION).%SVNVERS%.msi evildesk.msi
	rmdir /s/q dbg-$(VERSION).%SVNVERS%
	mkdir dbg-$(VERSION).%SVNVERS%
	xcopy *.cpp dbg-$(VERSION).%SVNVERS%
	xcopy *.h dbg-$(VERSION).%SVNVERS%
	mkdir dbg-$(VERSION).%SVNVERS%\pcre
	xcopy pcre\*.c dbg-$(VERSION).%SVNVERS%\pcre
	xcopy pcre\*.h dbg-$(VERSION).%SVNVERS%\pcre
	for %%D in (x86 amd64) do mkdir dbg-$(VERSION).%SVNVERS%\%%D && xcopy %%D\*.exe dbg-$(VERSION).%SVNVERS%\%%D && xcopy %%D\*.dll dbg-$(VERSION).%SVNVERS%\%%D && xcopy %%D\*.pdb dbg-$(VERSION).%SVNVERS%\%%D
	copy dumpanalyze.bat dbg-$(VERSION).%SVNVERS%
	$(CYGBIN)\tar cf evildesk-dbg-$(VERSION).%SVNVERS%.tar dbg-$(VERSION).%SVNVERS%
	$(CYGBIN)\bzip2 evildesk-dbg-$(VERSION).%SVNVERS%.tar
	$(CYGBIN)\ls -l evildesk-dbg-$(VERSION).%SVNVERS%.tar.bz2
<<NOKEEP

wezdeskres-symbols.h: wezdeskres.h
	$(CYGBIN)\gawk.exe -f make-symbols.awk < wezdeskres.h > wezdeskres-symbols.h

XLATE=0409 0407 040C
i18n-dll: wezdeskres.h i18n\*.rc
	for %%D in ($(XLATE)) do $(RC) /fo$(OUTDIR)\%%D.RES i18n\%%D.rc
	for %%D in ($(XLATE)) do link /nologo /machine:$(MACHINE) /DLL /NOENTRY /OUT:$(OUTDIR)\$(ABITS)-%%D.dll $(OUTDIR)\%%D.RES

CORE_OBJS=\
	$(OUTDIR)\wezdesk.obj \
	$(OUTDIR)\coretray.obj \
	$(OUTDIR)\slit.obj \
	$(OUTDIR)\contextmenu.obj \
	$(OUTDIR)\re.obj \
	$(OUTDIR)\hotkey.obj \
	$(OUTDIR)\debug.obj \
	$(OUTDIR)\pcre-maketables.obj \
	$(OUTDIR)\pcre-get.obj \
	$(OUTDIR)\pcre-pcre.obj \
	$(OUTDIR)\pcre-study.obj


$(OUTDIR)\wezdesk.exe:	$(CORE_OBJS) $(HDRS)
	link /nologo /machine:$(MACHINE) /out:$(OUTDIR)\wezdesk.exe /manifest \
		/pdb:$(OUTDIR)\wezdesk.pdb /delayload:dbghelp.dll \
		/debug /tsaware $(CORE_OBJS) $(LIBS) delayimp.lib /opt:ref
	$(COOK_EXE_MANIFEST)

$(OUTDIR)\wezdesk-hh32.exe: menuhost.cpp
	$(CC) /Fe$(OUTDIR)\wezdesk-hh32.exe /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\wezdesk-hh32.pdb $(CFLAGS) menuhost.cpp $(LIBS)
	$(COOK_EXE_MANIFEST)

$(OUTDIR)\wezdesk.obj: wezdesk.cpp
	$(CC) /Fo"$(OUTDIR)\wezdesk.obj" /c $(CORE_CFLAGS) wezdesk.cpp

$(OUTDIR)\coretray.obj: coretray.cpp
	$(CC) /Fo"$(OUTDIR)\coretray.obj" /c $(CORE_CFLAGS) coretray.cpp

$(OUTDIR)\slit.obj: slit.cpp
	$(CC) /Fo"$(OUTDIR)\slit.obj" /c $(CORE_CFLAGS) slit.cpp

$(OUTDIR)\contextmenu.obj: contextmenu.cpp
	$(CC) /Fo"$(OUTDIR)\contextmenu.obj" /c $(CORE_CFLAGS) contextmenu.cpp

$(OUTDIR)\re.obj: re.cpp
	$(CC) /Fo"$(OUTDIR)\re.obj" /c $(CORE_CFLAGS) re.cpp

$(OUTDIR)\hotkey.obj: hotkey.cpp
	$(CC) /Fo"$(OUTDIR)\hotkey.obj" /c $(CORE_CFLAGS) hotkey.cpp

$(OUTDIR)\debug.obj: debug.cpp
	$(CC) /Fo"$(OUTDIR)\debug.obj" /c $(CORE_CFLAGS) debug.cpp

$(OUTDIR)\pcre-maketables.obj: pcre\maketables.c
	$(CC) /Fo"$(OUTDIR)\pcre-maketables.obj" /c $(PCRE_CFLAGS) pcre\maketables.c

$(OUTDIR)\pcre-get.obj: pcre\get.c
	$(CC) /Fo"$(OUTDIR)\pcre-get.obj" /c $(PCRE_CFLAGS) pcre\get.c

$(OUTDIR)\pcre-pcre.obj: pcre\pcre.c pcre\chartables.c
	$(CC) /Fo"$(OUTDIR)\pcre-pcre.obj" /c $(PCRE_CFLAGS) pcre\pcre.c

$(OUTDIR)\pcre-study.obj: pcre\study.c
	$(CC) /Fo"$(OUTDIR)\pcre-study.obj" /c $(PCRE_CFLAGS) pcre\study.c

pcre\chartables.c: pcre\dftables.exe
	pcre\dftables.exe pcre\chartables.c

pcre\dftables.exe: pcre\dftables.c pcre\config.h
	$(CC) /Fo"pcre\\" $(PCRE_CFLAGS) /Fepcre\dftables.exe pcre\dftables.c

pcre\config.h:
	echo /**/ > pcre\config.h

$(OUTDIR)\tray.dll:	tray.cpp
	$(CC) /Fe$(OUTDIR)\tray.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\tray.pdb $(DLLFLAGS) tray.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\clock.dll:	clock.cpp
	$(CC) /Fe$(OUTDIR)\clock.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\clock.pdb $(DLLFLAGS) clock.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\flasher.dll:	flasher.cpp
	$(CC) /Fe$(OUTDIR)\flasher.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\flasher.pdb $(DLLFLAGS) flasher.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\quicklaunch.dll:	quicklaunch.cpp
	$(CC) /Fe$(OUTDIR)\quicklaunch.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\quicklaunch.pdb $(DLLFLAGS) quicklaunch.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\dock.dll:	dock.cpp
	$(CC) /Fe$(OUTDIR)\dock.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\dock.pdb $(DLLFLAGS) dock.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\taskswitch.dll:	taskswitch.cpp
	$(CC) /Fe$(OUTDIR)\taskswitch.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\taskswitch.pdb $(DLLFLAGS) taskswitch.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\launcher.dll:	launcher.cpp
	$(CC) /Fe$(OUTDIR)\launcher.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\launcher.pdb $(DLLFLAGS) launcher.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)


$(OUTDIR)\wdmenu64.dll:	menuhook.cpp
	$(CC) -c /Fo"$(OUTDIR)\\menuhook64.obj" /Fd$(OUTDIR)\wdmenu64.pdb /nologo /GA /Zi menuhook.cpp 
	link /nologo /dll /debug /opt:ref /out:$(OUTDIR)\wdmenu64.dll /pdb:$(OUTDIR)\wdmenu64.pdb $(OUTDIR)\menuhook64.obj $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\wdmenu32.dll:	menuhook.cpp
	$(CC) -c /Fo"$(OUTDIR)\\menuhook32.obj" /Fd$(OUTDIR)\wdmenu32.pdb /nologo /GA /Zi menuhook.cpp 
	link /nologo /dll /debug /opt:ref /out:$(OUTDIR)\wdmenu32.dll /pdb:$(OUTDIR)\wdmenu32.pdb $(OUTDIR)\menuhook32.obj $(LIBS)
	$(COOK_DLL_MANIFEST)


$(OUTDIR)\putty.dll:	putty.cpp
	$(CC) /Fe$(OUTDIR)\putty.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\putty.pdb $(DLLFLAGS) putty.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\overview.dll:	overview.cpp
	$(CC) /Fe$(OUTDIR)\overview.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\overview.pdb $(DLLFLAGS) overview.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

$(OUTDIR)\windowmatch.dll: windowmatch.cpp
	$(CC) /Fe$(OUTDIR)\windowmatch.dll /Fo"$(OUTDIR)\\" /Fd$(OUTDIR)\windowmatch.pdb $(DLLFLAGS) windowmatch.cpp $(LIBS)
	$(COOK_DLL_MANIFEST)

clean:
	for %%D in (x86 amd64) do del /q %%D\*.RES %%D\*.manifest %%D\*.dll %%D\*.exe %%D\*.obj %%D\*.lib %%D\*.exp %%D\*.ilk %%D\*.pdb *.wixobj *.msi *.zipi %%D\*-trace.dmp > NUL

expose.exe: expose.cpp
	$(CC) -o expose.exe $(CFLAGS) expose.cpp $(LIBS) /link /subsystem:console 
	$(COOK_EXE_MANIFEST)

exposex.exe: exposex.cpp
	$(CC) -o exposex.exe $(CFLAGS) exposex.cpp $(LIBS) /link /subsystem:console 
	$(COOK_EXE_MANIFEST)

