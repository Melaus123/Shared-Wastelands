@echo off
rem Build SharedWastelands.dll with the VS2010 (v100) x64 toolset.
rem The file names below repeat src\common\names.h (a .bat cannot include it): change both together.
rem Keep this file pure ASCII (see note in tools\vc2010-env.bat).
setlocal
rem mig5: THIS tree's env script (relative to this file), so a worktree builds with its own env change.
call "%~dp0..\..\tools\vc2010-env.bat" || exit /b 1
cd /d "%~dp0"

rem NOTE: VS2010 cl wants /FeNAME (no colon). "/Fe:name" silently produces ":name".
rem UNICODE/_UNICODE select the W (wide-character) variants of the Windows API; every build so far has used
rem them, so dropping them is its own change.
rem
rem /GL + /LTCG (whole program optimization) are kept so the code generation matches every build run so far -
rem dropping them is its own change.
rem OgreMain_x64.lib is required for Ogre::Vector3's dllimport ctor; it and MyGUIEngine_x64.lib are import
rem libraries made from the game's own DLLs (third_party\ogre-mygui\lib, SOURCE.txt - T-373).
rem --- Step 0: THE OFFLINE SUITE, and it is a GATE and not a report -----------------
rem It compiles src\common\clockmath.cpp with this same cl.exe and runs the tests. A failing test exits
rem non-zero and STOPS THIS BUILD BEFORE THE DLL IS LINKED, so a DLL carrying a failed assertion cannot
rem reach tools\deploy.ps1. src\coop-store\build.bat runs the SAME step 0 itself (P7w, review-p7u M-1):
rem it did NOT inherit the gate - its host.obj check only asks whether a plugin build ever happened.
call "%~dp0..\coop-test\build.bat"
if errorlevel 1 (
    echo BUILD FAILED ^(offline tests - the DLL was NOT rebuilt^)
    exit /b 1
)

rem --- Step 1: ENet (vendored, zlib licence - GPLv3-compatible) ---------------------
rem Compiled as C++ (/TP), NOT C: ENet 1.3.18 uses C99 for-loop declarations
rem ("for (ENetPeer * p = ...)") which the VS2010 C compiler rejects (C89 only).
rem enet.h carries extern "C" guards, so building the definitions as C++ keeps the
rem linkage identical - the symbols stay unmangled and our C++ links against them.
rem WINVER/_WIN32_WINNT 0x0501: ENet needs the XP-era socket API surface under v100.
rem win32.c/unix.c self-guard on _WIN32, so both may be listed; only win32 compiles.
cl /nologo /c /TP /O2 /GL /MD /D_CRT_SECURE_NO_WARNINGS ^
   /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 ^
   /I third_party\enet\include ^
   third_party\enet\callbacks.c third_party\enet\compress.c third_party\enet\host.c ^
   third_party\enet\list.c third_party\enet\packet.c third_party\enet\peer.c ^
   third_party\enet\protocol.c third_party\enet\win32.c
if errorlevel 1 (
    echo BUILD FAILED ^(enet^)
    exit /b 1
)

rem --- Step 1b: MinHook "multihook" 4f18d18 (vendored, BSD-2) - coop::AddHook in hooks.cpp (mig1) ------
rem Compiled as C++ (/TP), NOT C: hook.c declares variables after statements (C99), which the VS2010 C
rem compiler rejects (C89 only; a /TC trial on 2026-09-28 stopped in hook.c with C2275/C2065). MinHook.h
rem carries extern "C" guards, so the MH_* definitions stay unmangled. /FIstddef.h: under /TP nothing else
rem brings offsetof, which hook.c uses - the sources stay unmodified. UNICODE matches upstream's Unicode
rem character set (hook.c passes a wide string to GetModuleHandle). NDEBUG/STRICT as upstream's Release.
rem The objects go to obj\minhook\ so none can collide with the ENet objects in this folder.
if not exist obj\minhook mkdir obj\minhook
cl /nologo /c /TP /O2 /GL /EHsc /MD /DSTRICT /DNDEBUG /DUNICODE /D_UNICODE /FIstddef.h ^
   /I third_party\minhook\include /Foobj\minhook\ ^
   third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c ^
   third_party\minhook\src\hde\hde32.c third_party\minhook\src\hde\hde64.c
if errorlevel 1 (
    echo BUILD FAILED ^(minhook^)
    exit /b 1
)

rem --- Step 1b2: EVERY ADDRESS TABLE NAMES THE SAME ROWS (mig7, owner decision 99) -----------------------------
rem The mod picks addresses\<fingerprint>.txt by the game's fingerprint and refuses to start when a name it registers
rem is missing (addrBindMissing). A row added to the 1.0.65 table only would disable the mod on Steam 1.0.68 alone,
rem which no 1.0.65 bench run would notice - so the build fails here instead. Fix: re-run tools\gen_table_1068.py.
python "%~dp0..\..\tools\check_tables.py" "%~dp0addresses"
if errorlevel 1 (
    echo BUILD FAILED ^(address tables disagree - see TABLE CHECK FAILED above^)
    exit /b 1
)

rem --- Step 1b2b: THE SIGNATURES REPRODUCE BOTH STEAM TABLES (T-63 gog1 fold, 2026-09-29) --------------------------------
rem tools\prove_signatures.py runs the plugin's pattern resolver (src\common\sigtable.cpp, re-implemented step for step) on
rem both Steam executables with every cross-check (rank order, distinct, globals twice, call targets). It exits 0 only on
rem "PROOF: PASS"; anything else - a row that differs, one not found, one with no signature, a failed cross-check, a
rem missing executable - fails the build, so a signatures.sig that no longer reproduces both tables cannot ship.
rem READ-ONLY on both executables. Fix: re-run tools\gen_signatures.py.
python "%~dp0..\..\tools\prove_signatures.py"
if errorlevel 1 (
    echo BUILD FAILED ^(signatures do not reproduce both Steam tables - see PROOF above^)
    exit /b 1
)

rem --- Step 1b3: NO ENGINE ADDRESS WRITTEN INTO THE CODE (stage 7/9, 2026-09-29) ---------------------------------
rem Every engine address comes from the address table (AddrReg); a number typed into the code is a 1.0.65 number and
rem calls the wrong code on Steam 1.0.68. tools\scan_hardcoded_rva.py lists every such literal (its rules are in its
rem header; tools\scan_hardcoded_rva.allow holds only proven non-addresses) and fails the build on any hit.
python "%~dp0..\..\tools\scan_hardcoded_rva.py"
if errorlevel 1 (
    echo BUILD FAILED ^(an engine address is written into the code - see RVA SCAN above^)
    exit /b 1
)

rem --- Step 1c: THE LAYOUT CHECK -----------------------------------------------------------------------------
rem game\decl_check.cpp static_asserts OUR declarations (game\*.h) against the numbers in game\gamelayout.h (the
rem list is game\layout_asserts.inl). Compiled with the DLL's flags plus /c; the object goes to obj\layout\ and is
rem NEVER linked. A declaration that drifts from its number stops the build here, before the DLL step.
if not exist obj\layout mkdir obj\layout
cl /nologo /c /O2 /GL /EHsc /MD /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /wd4482 ^
   /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 /Foobj\layout\ game\decl_check.cpp
if errorlevel 1 (
    echo BUILD FAILED ^(layout check: our declarations disagree with game\gamelayout.h^)
    exit /b 1
)

rem --- Step 2: our plugin, linked against the ENet objects --------------------------
cl /nologo /LD /O2 /GL /EHsc /MD /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /wd4482 ^
   /DWINVER=0x0501 /D_WIN32_WINNT=0x0501 ^
   /I third_party\enet\include /I third_party\minhook\include ^
   coop.cpp hooks.cpp coop_log.cpp config.cpp addresses.cpp command_channel.cpp ai_spike.cpp spawn.cpp replicate.cpp combat.cpp medical.cpp ^
   identity.cpp soak.cpp appearance.cpp appearance_record.cpp clothing.cpp worldgen.cpp worldsync.cpp zones.cpp handoff.cpp hire.cpp store.cpp playerfaction.cpp relations.cpp peace.cpp towngen.cpp worldstate.cpp items.cpp policy.cpp ui.cpp doors.cpp speech.cpp stats.cpp crime.cpp build.cpp farm.cpp tags.cpp settings.cpp ownstore.cpp gamecalls.cpp effect.cpp bugreport.cpp titleart.cpp upnp.cpp team.cpp playerstab.cpp resurrect.cpp fallentab.cpp ^
   net\enet_transport.cpp net\session.cpp net\homeaddr.cpp net\steam_probe.cpp ^
   ..\common\clockmath.cpp ..\common\storemeta.cpp ..\common\addrtable.cpp ..\common\sigtable.cpp ..\common\writerladder.cpp ..\common\cfgtext.cpp ..\common\steamprobe.cpp ^
   callbacks.obj compress.obj host.obj list.obj packet.obj peer.obj protocol.obj win32.obj ^
   obj\minhook\buffer.obj obj\minhook\hook.obj obj\minhook\trampoline.obj obj\minhook\hde32.obj obj\minhook\hde64.obj ^
   /FeSharedWastelands.dll ^
   /link /LTCG /MAP:SharedWastelands.map /MACHINE:X64 OgreMain_x64.lib MyGUIEngine_x64.lib ws2_32.lib winmm.lib user32.lib ole32.lib oleaut32.lib
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

rem --- Step 3: THE MyGUI IMPORT NAME GATE, AFTER THE LINK (P8i / design-ui-panel V1h) ---------
rem F654 checked, by hand and once, that all 18 MyGUI symbols the one button imported exist by name
rem in the MyGUIEngine_x64.dll the game ships. The panel imports Window, EditBox, TextBox,
rem InputManager and UString calls on top of those, and a name that is not there does not make the
rem panel look wrong - IT STOPS THE DLL LOADING, which removes every co-op feature at once. So the
rem check is a GATE and it runs here, before tools\deploy.ps1 can ever see this DLL.
rem The game path is .modding\01-environment.md's.
set GAMEMYGUI=C:\Program Files (x86)\Steam\steamapps\common\Kenshi\MyGUIEngine_x64.dll
if not exist "%GAMEMYGUI%" (
    echo BUILD FAILED ^(import gate: the game's MyGUIEngine_x64.dll is not at "%GAMEMYGUI%"^)
    exit /b 1
)
rem Each build gets its own gate files: two compile slots can build at once, and a shared %%TEMP%% name let one build read
rem the other's half-written export list (T490, 2026-09-28: "no exports parsed").
set GATETMP=%TEMP%\kenshicoop_gate_%RANDOM%_%RANDOM%
dumpbin /nologo /imports SharedWastelands.dll > "%GATETMP%_imports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(import gate: dumpbin /imports failed^)
    exit /b 1
)
dumpbin /nologo /exports "%GAMEMYGUI%" > "%GATETMP%_exports.txt"
if errorlevel 1 (
    echo BUILD FAILED ^(import gate: dumpbin /exports failed^)
    exit /b 1
)
python "%~dp0..\..\tools\check-mygui-imports.py" "%GATETMP%_imports.txt" "%GATETMP%_exports.txt" MyGUIEngine_x64.dll
if errorlevel 1 (
    echo BUILD FAILED ^(import gate: a MyGUI import is not present by name in the game's MyGUIEngine_x64.dll^)
    exit /b 1
)

rem --- Step 4: THE IMPORT GATE, AFTER THE LINK ---------------------------------------------------------------
rem The DLL may import only from the DLLs named here: the game's own Ogre and MyGUI DLLs, the VS2010 runtime and
rem Windows system DLLs. A DLL not on the list stops the build, so a library that comes onto the link line (or a
rem header that asks for one by pragma) cannot add a dependency silently; a new one is added to the list on purpose.
rem The line it prints lists every DLL the import list names.
python -c "import sys,re; t=open(sys.argv[1]).read(); ok=set(sys.argv[2].lower().split(',')); d=sorted(set(x.lower() for x in re.findall(r'(?m)^[ \t]+(\S+\.dll)[ \t]*$', t, re.I))); bad=[x for x in d if x not in ok]; print('imports from: ' + ' '.join(d)); print('not on the list: ' + (' '.join(bad) if bad else 'none')); sys.exit(1 if bad or not d else 0)" "%GATETMP%_imports.txt" "ogremain_x64.dll,myguiengine_x64.dll,msvcp100.dll,msvcr100.dll,kernel32.dll,user32.dll,advapi32.dll,shell32.dll,ole32.dll,oleaut32.dll,ws2_32.dll,winmm.dll,winhttp.dll,iphlpapi.dll"
if errorlevel 1 (
    echo BUILD FAILED ^(import gate: the DLL imports from a DLL that is not on the list, or the list could not be read^)
    exit /b 1
)

echo === exports ===
rem mig6: both starts must be exported - startPlugin (RE_Kenshi's road, C++ name) and coopEarlyStart (SharedWastelandsLoader's
rem road, a C name: the marker file names it and the loader finds it by exactly that name).
dumpbin /nologo /exports SharedWastelands.dll > "%GATETMP%_ownexports.txt"
findstr /i "startPlugin coopEarlyStart" "%GATETMP%_ownexports.txt"
findstr /r /c:" coopEarlyStart$" "%GATETMP%_ownexports.txt" >nul
if errorlevel 1 (
    echo BUILD FAILED ^(export gate: coopEarlyStart is not exported by that exact name^)
    exit /b 1
)
findstr /c:"startPlugin" "%GATETMP%_ownexports.txt" >nul
if errorlevel 1 (
    echo BUILD FAILED ^(export gate: startPlugin is not exported^)
    exit /b 1
)

rem --- Step 5: SharedWastelandsLoader.dll (mig6, RE_Kenshi migration stage 6) - its own build.bat, the same VS2010 toolset -----
rem The Ogre plugin the game's Plugins_x64.cfg names (owner 117). Its own gate runs inside: KERNEL32.dll the only import,
rem dllStartPlugin and dllStopPlugin exported. A failure there fails this build.
call "%~dp0..\kenshi-loader\build.bat"
if errorlevel 1 (
    echo BUILD FAILED ^(SharedWastelandsLoader.dll^)
    exit /b 1
)
endlocal
