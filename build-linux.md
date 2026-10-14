How I got this building on Linux (Ubuntu, wine)

No Windows box, so I got the VS2010 toolchain running under wine. Its a bit hacky but it builds the plugin, the server and runs all the tests.

1. Compiler. The free VS2010 x64 compiler comes in the Windows SDK 7.1 iso (GRMSDKX_EN_DVD.iso, about 570MB from microsoft). Dont run the installer under wine, it falls over. Instead loop mount the iso and pull the msi's apart with msiextract (from msitools):

       sudo mount -o loop GRMSDKX_EN_DVD.iso /mnt/sdk
       msiextract -C out /mnt/sdk/Setup/vc_stdamd64/vc_stdamd64.msi
       msiextract -C out /mnt/sdk/Setup/WinSDK_amd64/*.msi      (and WinSDKBuild_amd64)

   Then lay it out so it matches what tools/vc2010-env.bat wants, C:\tools\vc2010\VC (bin\amd64, include, lib\amd64) and C:\tools\vc2010\SDK (Include, Lib\x64), inside a fresh wine prefix.

2. Boost 1.60 headers, just unpack boost_1_60_0.tar.bz2 somewhere and point COOP_BOOST at it with a Z:\ path.

3. Env:

       export WINEPREFIX=~/sw-wine WINEDEBUG=-all
       export COOP_BOOST='Z:\path\to\boost_1_60_0'
       cd src/coop-plugin && wine cmd /c build.bat

Stuff I had to change in my local copy of the bat files (didnt commit any of this, its all wine workarounds):

- wine's cmd gets `call something || exit /b 1` wrong, it bails even when the call worked. Just drop the `|| exit /b 1` after the calls.
- same deal at the end of tools/vc2010-env.bat, `goto :eof` leaves a stale errorlevel behind so the next check fails. Changed it to `exit /b 0`.
- Ogre's thread headers try to auto link the boost libs, which I dont have built. Added `/DBOOST_ALL_NO_LIB /DBOOST_ERROR_CODE_HEADER_ONLY` to the cl line and it's happy.
- GAMEMYGUI points at the Steam path on C:, I changed it to my Kenshi install through Z:.
- The python gates (check_tables, prove_signatures, scan_hardcoded_rva) and the dumpbin import/export gates I took out of the bat and ran by hand on the linux side instead, python under wine wasnt worth the trouble. Run them from the repo root with normal python3.
- The world server wont link with server_version.res, cvtres throws LNK1123 under wine. I just linked it without the .res for testing so the exe has no version info. Haven't found a real fix for that one.

The source files are CRLF, if you edit on linux keep them that way or the diffs turn into a mess.

Full plugin build + tests is around 5 or 6 minutes on my machine, the server about 2.


