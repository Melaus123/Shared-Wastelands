@echo off
rem Build SharedWastelandsLoader.dll (mig6, RE_Kenshi migration stage 6) with the VS2010 (v100) x64 toolset.
rem The file names below repeat src\common\names.h (a .bat cannot include it): change both together.
rem Called by src\coop-plugin\build.bat after the plugin's own gates; runnable alone too.
rem Keep this file pure ASCII (see note in tools\vc2010-env.bat). VS2010 cl wants /FeNAME with NO colon (F025).
rem
rem NO C RUNTIME (loader.cpp's header says why): /NODEFAULTLIB, our own entry point, /GS- (the security cookie is a
rem CRT symbol). /Zl keeps the object from naming a default CRT library at all. No /GL: nothing here needs it.
setlocal
call "%~dp0..\..\tools\vc2010-env.bat" || exit /b 1
cd /d "%~dp0"

cl /nologo /c /O1 /GS- /Zl /W4 /EHs-c- /DUNICODE /D_UNICODE ^
   /I ..\common loader.cpp
if errorlevel 1 (
    echo BUILD FAILED ^(loader: compile^)
    exit /b 1
)
link /nologo /DLL /NODEFAULTLIB /ENTRY:LoaderEntry /MACHINE:X64 /SUBSYSTEM:WINDOWS ^
   /MAP:SharedWastelandsLoader.map /OUT:SharedWastelandsLoader.dll loader.obj kernel32.lib
if errorlevel 1 (
    echo BUILD FAILED ^(loader: link^)
    exit /b 1
)

rem --- THE LOADER GATE, AFTER THE LINK --------------------------------------------------------------------------
rem The game loads this DLL itself, on every start, before anything of the mod exists: its import list must be
rem KERNEL32.dll and NOTHING else (a C runtime or any other DLL coming back onto the link would put a dependency into
rem every game start), and it must export dllStartPlugin and dllStopPlugin by those exact names (Ogre looks both up).
rem A unique name per build, a fresh random id from Python: cmd's %RANDOM% is seeded from the clock, so two builds started
rem in the same second would share these files and read each other's half-written lists.
for /f "delims=" %%G in ('python -c "import uuid;print(uuid.uuid4().hex)"') do set LGATEID=%%G
if "%LGATEID%"=="" (
    echo BUILD FAILED ^(could not make a unique temp name: python did not answer^)
    exit /b 1
)
set LGATE=%TEMP%\sw_loadergate_%LGATEID%
dumpbin /nologo /imports SharedWastelandsLoader.dll > "%LGATE%_imports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(loader gate: dumpbin /imports failed^)
    exit /b 1
)
dumpbin /nologo /exports SharedWastelandsLoader.dll > "%LGATE%_exports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(loader gate: dumpbin /exports failed^)
    exit /b 1
)
python -c "import sys,re; t=open(sys.argv[1]).read(); d=sorted(set(m.lower() for m in re.findall(r'(?m)^[ \t]+(\S+\.dll)[ \t]*$', t))); print('loader imports: ' + ', '.join(d)); sys.exit(0 if d == ['kernel32.dll'] else 1)" "%LGATE%_imports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(loader gate: SharedWastelandsLoader.dll imports something other than KERNEL32.dll^)
    exit /b 1
)
python -c "import sys,re; t=open(sys.argv[1]).read(); e=set(re.findall(r'(?m)^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)', t)); print('loader exports: ' + ', '.join(sorted(e))); sys.exit(0 if set(['dllStartPlugin', 'dllStopPlugin']) <= e else 1)" "%LGATE%_exports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(loader gate: dllStartPlugin / dllStopPlugin not exported by name^)
    exit /b 1
)
endlocal
