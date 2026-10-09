@echo off
rem Build SharedWastelandsServer.exe (the standalone sleeping-record store) with the VS2010 x64 toolset.
rem The file names below repeat src\common\names.h (a .bat cannot include it): change both together.
rem Reuses the ENet objects the plugin's build.bat compiles into src\coop-plugin (run that first).
rem Compiles the plugin's ..\coop-plugin\u8file.cpp in as well: the same UTF-8 file-call wrappers over the wide Windows calls.
setlocal
call "%~dp0..\..\tools\vc2010-env.bat" || exit /b 1

rem --- Step 0: THE OFFLINE SUITE, HERE TOO (P7w, folding review-p7u M-1 / F580) ------------------
rem This exe compiles ..\common\clockmath.cpp into itself, so a failing assertion about that file must
rem stop THIS build as well. Until P7w only src\coop-plugin\build.bat called the suite, and this file's
rem only precondition was that host.obj existed from some earlier plugin build - which says nothing about
rem whether the tests pass. The relay was ungated.
call "%~dp0..\coop-test\build.bat"
if errorlevel 1 (
    echo BUILD FAILED ^(offline tests - SharedWastelandsServer.exe was NOT rebuilt^)
    exit /b 1
)

cd /d "%~dp0"
set OBJ=..\coop-plugin
if not exist "%OBJ%\host.obj" (
    echo BUILD FAILED: ENet objects missing - build the plugin first ^(src\coop-plugin\build.bat^)
    exit /b 1
)
rem owner 244: the exe carries a version resource (Explorer and Task Manager show "Shared Wastelands world server").
rem The toolchain has no rc.exe, so make_version.py writes server_version.res directly (as src\installer\make_icon.py does).
if not exist server_version.res (
    echo BUILD FAILED ^(world server: server_version.res missing - run python make_version.py^)
    exit /b 1
)
cl /nologo /O2 /EHsc /MD /D_CRT_SECURE_NO_WARNINGS /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 ^
   /I ..\coop-plugin\third_party\enet\include ^
   store_main.cpp ..\common\clockmath.cpp ..\common\storemeta.cpp ..\common\cfgtext.cpp ..\coop-plugin\u8file.cpp server_version.res ^
   %OBJ%\callbacks.obj %OBJ%\compress.obj %OBJ%\host.obj %OBJ%\list.obj %OBJ%\packet.obj %OBJ%\peer.obj %OBJ%\protocol.obj %OBJ%\win32.obj ^
   /FeSharedWastelandsServer.exe ^
   /link /MACHINE:X64 /SUBSYSTEM:CONSOLE ws2_32.lib winmm.lib
if errorlevel 1 (
    echo BUILD FAILED ^(world server^)
    exit /b 1
)
echo built SharedWastelandsServer.exe
