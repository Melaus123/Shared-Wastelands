@echo off
rem Build and RUN coop_test.exe - the offline suite (P7u, design-offline-tests 4b).
rem P7y: it also sweeps src\common\loadlatch.h (the world-generation latch, F610). That header is
rem header-only and pure, so it needs no extra translation unit here - only the /I ..\common below.
rem Keep this file pure ASCII (see note in tools\vc2010-env.bat). VS2010 cl wants /FeNAME with NO colon (F025).
rem NO /GL and NO /LTCG: the plugin keeps them only so its own code generation matches its earlier builds; this
rem exe links nothing but the CRT.
setlocal
call "%~dp0..\..\tools\vc2010-env.bat" || exit /b 1
cd /d "%~dp0"

cl /nologo /O2 /EHsc /MD /W4 /D_CRT_SECURE_NO_WARNINGS ^
   /I ..\common ^
   test_main.cpp ..\common\clockmath.cpp ..\common\storemeta.cpp ..\common\addrtable.cpp ..\common\sigtable.cpp ..\common\writerladder.cpp ..\common\cfgtext.cpp ..\common\steamprobe.cpp ^
   /Fecoop_test.exe ^
   /link /MACHINE:X64 /SUBSYSTEM:CONSOLE
if errorlevel 1 (
    echo BUILD FAILED ^(tests: compile^)
    exit /b 1
)

rem Run it by FULL PATH: this environment has NoDefaultCurrentDirectoryInExePath set, so a bare
rem `coop_test.exe` reports "is not recognized" (errorlevel 9009) and would fail the build for the
rem wrong reason - a build gate that cannot tell "a test failed" from "the runner was not found" is
rem worse than none.
"%~dp0coop_test.exe"
if errorlevel 1 (
    echo BUILD FAILED ^(tests: a test FAILED - the DLL was NOT relinked^)
    exit /b 1
)
endlocal
