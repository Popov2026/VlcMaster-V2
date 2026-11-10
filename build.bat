@echo off
rem Compilation de VLC Master (MinGW-w64 64 bits : gcc + windres)
setlocal
cd /d "%~dp0"
if not exist build mkdir build
windres -O coff res\app.rc -o build\app.res.o || goto :err
gcc -std=c11 -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static ^
    src\vlcmaster.c build\app.res.o -o build\VlcMaster.exe ^
    -lcomctl32 -lcomdlg32 -lwininet -luxtheme -lgdi32 -luser32 -ladvapi32 || goto :err
echo OK : build\VlcMaster.exe
exit /b 0
:err
echo ECHEC de la compilation
exit /b 1
