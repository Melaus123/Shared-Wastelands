@echo off
rem vc2010-env.bat - configure a VS2010 (v100) x64 build environment.
rem Every native part of the mod builds with it. The plugin runs inside Kenshi, which was built with VS2010, and
rem calls the game's own functions and reads its objects - std::string and the containers among them - so it must
rem use the same toolset and standard library.
rem
rem Toolchain extracted from Microsoft's Windows SDK 7.1 ISO via "msiexec /a"
rem (administrative extraction - nothing installed system-wide, no registry writes).
rem
rem NOTE: keep this file pure ASCII. Non-ASCII characters (em dashes etc.) corrupt
rem cmd parsing and produce bogus "not recognized as an internal command" errors.
rem
rem Usage: call tools\vc2010-env.bat   then run cl / link normally.

set VC2010=C:\tools\vc2010
rem The Ogre 2.0.0-pre and MyGUI headers and import libs are the repo's own copies in
rem src\coop-plugin\third_party\ogre-mygui (include\ogre, include\mygui, lib; its SOURCE.txt). That path is
rem relative to THIS file, so a worktree builds its own copy.
for %%I in ("%~dp0..\src\coop-plugin\third_party\ogre-mygui") do set COOP_OGRE_MYGUI=%%~fI
rem boost 1.60 is the one outside download the build uses (headers; stage\lib, see LIB below).
rem COOP_BOOST names its boost_1_60_0 folder. When it is not set, this file looks for reference\boost_1_60_0, then
rem reference\<any folder>\boost_1_60_0, in this checkout, then in the main checkout this worktree belongs to
rem (git's common directory), so a worktree needs no copy of reference\ of its own.
if not defined COOP_BOOST call :findboost "%~dp0.."
if not defined COOP_BOOST for /f "delims=" %%G in ('git -C "%~dp0.." rev-parse --path-format^=absolute --git-common-dir 2^>nul') do call :findboost "%%G\.."
if not defined COOP_BOOST echo vc2010-env: boost_1_60_0 not found - set COOP_BOOST to its folder if this build needs boost

set PATH=%VC2010%\VC\bin\amd64;%VC2010%\VC\bin;%PATH%
rem NOTE: boost root is boost_1_60_0 (headers are included as <boost/...>), NOT boost_1_60_0\boost.
set INCLUDE=%VC2010%\VC\include;%VC2010%\SDK\Include;%COOP_OGRE_MYGUI%\include;%COOP_BOOST%
rem boost stage\lib: the plugin includes only header-only boost headers (unordered_map/unordered_set and their
rem support, measured with /showIncludes) and never boost/config/auto_link.hpp, so no boost library is linked.
rem stage\lib stays on LIB until the owner decides (the headers are not vendored either).
set LIB=%VC2010%\VC\lib\amd64;%VC2010%\SDK\Lib\x64;%COOP_OGRE_MYGUI%\lib;%COOP_BOOST%\stage\lib
goto :eof

rem findboost <checkout folder>: sets COOP_BOOST when that checkout has reference\boost_1_60_0\boost\ or
rem reference\<folder>\boost_1_60_0\boost\.
:findboost
for %%I in ("%~1\reference\boost_1_60_0") do if exist "%%~fI\boost\*" set COOP_BOOST=%%~fI
if not defined COOP_BOOST for /d %%D in ("%~1\reference\*") do if exist "%%~fD\boost_1_60_0\boost\*" set COOP_BOOST=%%~fD\boost_1_60_0
goto :eof
