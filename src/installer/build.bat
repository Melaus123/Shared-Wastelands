@echo off
rem Build SharedWastelandsSetup.exe (installer1, stage 6) with the VS2010 (v100) x64 toolset.
rem The file names below repeat src\common\names.h (a .bat cannot include it): change both together.
rem STANDALONE, like src\coop-store\build.bat: src\coop-plugin\build.bat does not call it.
rem Keep this file pure ASCII (see note in tools\vc2010-env.bat). VS2010 cl wants /FeNAME with NO colon (F025).
setlocal
call "%~dp0..\..\tools\vc2010-env.bat" || exit /b 1

rem --- Step 0: THE OFFLINE SUITE - this exe compiles src\common\installtext.h, which the suite tests --------
call "%~dp0..\coop-test\build.bat"
if errorlevel 1 (
    echo BUILD FAILED ^(offline tests - SharedWastelandsSetup.exe was NOT rebuilt^)
    exit /b 1
)

cd /d "%~dp0"
rem /MT: the C runtime is linked in, so the exe runs on a Windows 7+ machine with nothing installed.
rem No /GL, no /LTCG: the plugin keeps them only so its own code generation matches its earlier builds.
rem The toolchain has no rc.exe, so the dialog template is built in memory (setup_main.cpp).
rem The icon (file icon and window icon) is setup_icon.res, which make_icon.py writes directly (no rc.exe needed);
rem link turns it into the exe's resources with cvtres.exe, which the toolchain does have.
if not exist setup_icon.res (
    echo BUILD FAILED ^(installer: setup_icon.res missing - run python make_icon.py^)
    exit /b 1
)
cl /nologo /O2 /EHsc /MT /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 ^
   /I ..\common ^
   setup_main.cpp setup_icon.res ^
   /FeSharedWastelandsSetup.exe ^
   /link /MACHINE:X64 /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib advapi32.lib
if errorlevel 1 (
    echo BUILD FAILED ^(installer^)
    exit /b 1
)

rem --- Step 2: THE DEPENDENCY GATE - Windows system DLLs only, nothing a player would have to install ---------
set DEPTMP=%TEMP%\kenshicoop_setupdeps_%RANDOM%_%RANDOM%.txt
dumpbin /nologo /dependents SharedWastelandsSetup.exe > "%DEPTMP%"
if errorlevel 1 (
    echo BUILD FAILED ^(installer: dumpbin /dependents failed^)
    exit /b 1
)
python -c "import sys,re; t=open(sys.argv[1]).read(); d=[l.strip() for l in t.splitlines() if re.match(r'^\s+\S+\.dll\s*$', l, re.I)]; ok=set(['kernel32.dll','user32.dll','gdi32.dll','shell32.dll','ole32.dll','comctl32.dll','advapi32.dll']); bad=[x for x in d if x.lower() not in ok]; print('dependents: ' + ', '.join(d)); sys.exit(1 if bad or not d else 0)" "%DEPTMP%"
if errorlevel 1 (
    echo BUILD FAILED ^(installer: a dependency that is not a Windows system DLL^)
    exit /b 1
)
echo built SharedWastelandsSetup.exe
endlocal
