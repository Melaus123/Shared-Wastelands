# build-package.ps1 - T-254: assemble the player's release package from ONE source tree, checked, or refuse.
#
# The mod-package folder was hand-kept and went stale (a 99 KB plugin DLL from Aug 5, no addresses\,
# no world server): no tool built it. This script is that tool. It copies, from the SAME places tools\deploy.ps1 ships
# from, every file a player's mod folder needs, hash-checks each copy, and writes a manifest. It never writes into any
# game's mods folder: the output is <this repo>\build\package\<Name>\<mod folder>\ (and, with -Zip, a zip beside it).
#
# NOTHING IS LEFT HALF-WRITTEN. Every check that can be asked of the source tree is asked BEFORE anything is written, and
# every failure is listed together. The package is then built in a staging folder beside the output, checked again there
# (copy hashes, tools\check_tables.py on the copied tables), and only then moved into place. Any failure sends the staging
# folder to the Recycle Bin and exits 1; a zip is likewise made under a temporary name, re-read, and only then renamed.
#
# What a player's mod folder holds, and why (Read, 2026-09-29):
#   SharedWastelands.dll                 the mod (src\coop-plugin\build.bat). Must be newer than every source it is built from,
#                                  and must carry the start export the loader marker names (start=coopEarlyStart).
#   SharedWastelandsLoader.dll           the game's Plugins_x64.cfg plugin (src\kenshi-loader); the Setup copies it from here
#                                  into the Kenshi folder (src\installer\setup_main.cpp header, item 1).
#   SharedWastelandsSetup.exe     that Setup (src\installer); shipped beside the loader, which it copies from its own folder.
#   shared-wastelands.loader.txt  the loader finds the mod folder by this file (src\common\loadermarker.h).
#   SharedWastelandsServer.exe                 the world server; the MULTIPLAYER panel starts it from beside the plugin by exactly this
#                                  name (src\coop-plugin\ui.cpp: PathNextToDll("SharedWastelandsServer.exe")).
#   addresses\*.txt + signatures.sig   the plugin installs nothing without a table for the game's fingerprint
#                                  (addresses.cpp); signatures.sig is the pattern road for a build with no table.
#   Shared Wastelands.mod, _Shared Wastelands.info   the mod list entry; SharedWastelandsLoader and coopEarlyStart start the mod only
#                                  when a .mod in its folder is switched on in Kenshi's mod list (coop.cpp modSwitchedOn).
#   title-background.png          the title screen's art (owner 446/447): the plugin shows it in place of Kenshi's title
#                                  background (src\coop-plugin\titleart.cpp; its name is src\common\titleart.h kTitleArtFile).
#                                  Without it Kenshi's own art stays.
#   RE_Kenshi.json                 RE_Kenshi's road is still in the code (coop.cpp startPlugin, which waits for the
#                                  launcher's OK as an Ogre plugin object), so a player with RE_Kenshi and no Setup still starts the mod. WRITTEN here,
#                                  naming only SharedWastelands.dll, the same as mod-package's copy. The start-once guard (coop.cpp) makes both roads safe together.
#   licenses\                      third-party code compiled into the shipped binaries: ENet (DLL + SharedWastelandsServer.exe),
#                                  MinHook (DLL; BSD-2 requires the notice with binaries), MyGUI headers (DLL).
#   PACKAGE-MANIFEST.txt           NOT in the mod folder: written BESIDE it (<Name>\PACKAGE-MANIFEST.txt, owner 244), so a
#                                  player copies only the mod folder. Every mod folder file: SHA-256, size, source, commit.
#
# Usage:
#   pwsh -File tools\build-package.ps1                          # the main tree C:\code\kenshi-coop
#   pwsh -File tools\build-package.ps1 -Tree C:\code\kenshi-coop-wt\<name>
#   pwsh -File tools\build-package.ps1 -Zip                     # also build\package\<Name>-<commit>.zip
#   pwsh -File tools\build-package.ps1 -Replace                 # an existing package / zip of the same name goes to the
#                                                               # Recycle Bin (only after the new one passed every check)
# Exit 0 = package in place and verified; 1 = refused (the reasons are printed; nothing was left behind).
# Test seam (tools\test_build_package.py only): KCOOP_PACKAGE_TEST_CORRUPT=<path in the mod folder> appends one byte to
# that file's staged copy right after it is copied, so the copy check can be shown to refuse.

param(
    [string]$Tree = 'C:\code\kenshi-coop',
    [string]$Name = 'shared-wastelands',   # the file and folder names in this script repeat src\common\names.h
    [string]$OutRoot = '',
    [switch]$Zip,
    [switch]$Replace
)

$ErrorActionPreference = 'Stop'

function Send-ToRecycleBin([string]$p) {
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { return }
    Add-Type -AssemblyName Microsoft.VisualBasic
    if (Test-Path -LiteralPath $p -PathType Container) {
        [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($p, 'OnlyErrorDialogs', 'SendToRecycleBin')
    }
    else {
        [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($p, 'OnlyErrorDialogs', 'SendToRecycleBin')
    }
}

function Get-Sha256([string]$p) { return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }

$staging = $null
$zipTmp = $null
try {
    # ---- 0. the tree, its commit, and where the package goes -------------------------------------------------------
    if (-not (Test-Path -LiteralPath $Tree -PathType Container)) { throw "REFUSED: no source tree at $Tree" }
    $Tree = (Resolve-Path -LiteralPath $Tree).Path.TrimEnd('\')
    $commit = (& git -C $Tree rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $commit) { throw "REFUSED: $Tree is not a git work tree (git rev-parse HEAD failed) - the manifest must name the source commit" }
    $commit = "$commit".Trim()
    $short = $commit.Substring(0, 8)
    if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$') { throw "REFUSED: -Name '$Name' is not a plain folder name" }

    if (-not $OutRoot) { $OutRoot = Join-Path $PSScriptRoot '..\build\package' }
    $OutRoot = [System.IO.Path]::GetFullPath($OutRoot).TrimEnd('\')
    # never into a game: no 'mods' segment, and no folder on the way up holds kenshi_x64.exe
    if (@($OutRoot.Split('\') | Where-Object { $_ -ieq 'mods' }).Count -gt 0) { throw "REFUSED: the output $OutRoot is inside a mods folder - a package is never built into a game" }
    for ($d = $OutRoot; $d; $d = Split-Path $d -Parent) {
        if (Test-Path -LiteralPath (Join-Path $d 'kenshi_x64.exe')) { throw "REFUSED: the output $OutRoot is inside a game folder ($d holds kenshi_x64.exe)" }
    }
    $modDirName = 'Shared Wastelands'   # owner 244: the mod folder's name is mod-package's folder name, as shipped (src\common\names.h kModNameLower holds it lower-cased)
    $modSrc  = Join-Path $Tree "mod-package\$modDirName"
    $final   = Join-Path $OutRoot $Name
    $zipPath = Join-Path $OutRoot ("$Name-$short.zip")
    if ((Test-Path -LiteralPath $final) -and -not $Replace) { throw "REFUSED: $final already exists (pass -Replace to put the old one in the Recycle Bin once the new one has passed)" }
    if ($Zip -and (Test-Path -LiteralPath $zipPath) -and -not $Replace) { throw "REFUSED: $zipPath already exists (pass -Replace)" }

    $problems = New-Object System.Collections.Generic.List[string]
    $codeExt = '\.(c|cc|cpp|h|hpp|inl|bat|res|ico|def)$'

    function Get-Tracked([string[]]$rel) {
        $out = @(& git -C $Tree ls-files -- $rel 2>$null)
        if ($LASTEXITCODE -ne 0) { throw "REFUSED: git ls-files failed in $Tree" }
        return @($out | Where-Object { $_ })
    }
    # the src\common headers a source file reaches through #include "..." (followed through the headers too)
    function Get-CommonIncludes([string[]]$rel) {
        $seen = @{}
        $queue = New-Object System.Collections.Queue
        foreach ($r in $rel) { $queue.Enqueue($r) }
        while ($queue.Count -gt 0) {
            $r = $queue.Dequeue()
            foreach ($m in @(Select-String -LiteralPath (Join-Path $Tree $r) -Pattern '^\s*#\s*include\s+"([^"]+)"')) {
                # the include as written, beside the file that includes it (../coop-plugin/u8file.h from src/coop-store); else src/common/<name>
                $inc = $m.Matches[0].Groups[1].Value -replace '\\', '/'
                $parts = New-Object System.Collections.Generic.List[string]
                foreach ($p in (((Split-Path $r -Parent) -replace '\\', '/') + '/' + $inc).Split('/')) {
                    if ($p -eq '..') { if ($parts.Count -gt 0) { $parts.RemoveAt($parts.Count - 1) } } elseif ($p -ne '.' -and $p -ne '') { $parts.Add($p) }
                }
                $c = $parts -join '/'
                if (-not (Test-Path -LiteralPath (Join-Path $Tree $c))) { $c = 'src/common/' + (Split-Path $inc -Leaf) }
                if (-not $seen.ContainsKey($c) -and (Test-Path -LiteralPath (Join-Path $Tree $c))) { $seen[$c] = 1; $queue.Enqueue($c) }
            }
        }
        return @($seen.Keys)
    }

    # ---- 1. the four binaries: present, and NEWER than every tracked source they are built from ------------------------
    # (The build scripts stamp no commit into the binaries, so "built from this tree" is asked the only way the files allow:
    # a source changed - by an edit, a merge or a checkout - after the binary was written means the binary is stale.)
    $bins = @(
        @{ Dst = 'SharedWastelands.dll';             Src = 'src/coop-plugin/SharedWastelands.dll';             Dirs = @('src/coop-plugin', 'src/common');                                     Walk = $false; Build = 'src\coop-plugin\build.bat' }
        @{ Dst = 'SharedWastelandsLoader.dll';       Src = 'src/kenshi-loader/SharedWastelandsLoader.dll';     Dirs = @('src/kenshi-loader');                                                Walk = $true;  Build = 'src\coop-plugin\build.bat (it builds the loader too)' }
        @{ Dst = 'SharedWastelandsServer.exe';             Src = 'src/coop-store/SharedWastelandsServer.exe';              Dirs = @('src/coop-store', 'src/coop-plugin/third_party/enet');               Walk = $true; Build = 'src\coop-store\build.bat' }
        @{ Dst = 'SharedWastelandsSetup.exe'; Src = 'src/installer/SharedWastelandsSetup.exe';   Dirs = @('src/installer');                                                    Walk = $true;  Build = 'src\installer\build.bat' }
    )
    $allInputDirs = New-Object System.Collections.Generic.List[string]
    foreach ($b in $bins) {
        $src = Join-Path $Tree $b.Src
        foreach ($d in $b.Dirs) { $allInputDirs.Add($d) }
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { $problems.Add("MISSING: $($b.Src) is not built in this tree - run $($b.Build)"); continue }
        $inputs = @(Get-Tracked $b.Dirs | Where-Object { $_ -match $codeExt -and $_ -notlike 'src/coop-plugin/addresses/*' })
        if ($b.Walk) {
            # what it compiles: its own folder's sources plus every ..\common\*.cpp and ..\coop-plugin\*.cpp its build.bat names
            # (SharedWastelandsServer.exe compiles clockmath/storemeta/cfgtext and the plugin's u8file.cpp), then every header those reach through #include
            $bat = Join-Path $Tree (($b.Dirs[0] -replace '/', '\') + '\build.bat')
            $seeds = @(if (Test-Path -LiteralPath $bat) {
                    Select-String -LiteralPath $bat -Pattern '\.\.\\(common|coop-plugin)\\([A-Za-z0-9_]+\.cpp)' -AllMatches | ForEach-Object { $_.Matches } | ForEach-Object { 'src/' + $_.Groups[1].Value + '/' + $_.Groups[2].Value } })
            $inputs = @($inputs + @($seeds | Select-Object -Unique | Where-Object { Test-Path -LiteralPath (Join-Path $Tree $_) }))
            $inputs = @($inputs + (Get-CommonIncludes @($inputs | Where-Object { $_ -match '\.(c|cc|cpp|h|hpp)$' })) | Select-Object -Unique)
        }
        if ($inputs.Count -eq 0) { $problems.Add("NO SOURCES: git lists no source files under $($b.Dirs -join ', ') - cannot tell whether $($b.Src) is current"); continue }
        # A COMMITTED binary (SharedWastelandsServer.exe is tracked) that is unchanged since its commit carries git's checkout time, not
        # its build time - git writes SharedWastelandsServer.exe before store_main.cpp in the same checkout - so for it the question is
        # asked of the history: the last commit that changed any of its sources must be in the history of the last commit
        # that changed the binary. A rebuilt (locally modified) or untracked build output is asked by its write time.
        $binTracked = @(Get-Tracked @($b.Src)).Count -gt 0
        $binDirty = $binTracked -and @(& git -C $Tree status --porcelain=v1 -- $b.Src 2>$null | Where-Object { $_ }).Count -gt 0
        if ($binTracked -and -not $binDirty) {
            $cBin = "$(& git -C $Tree log -1 --format=%H -- $b.Src 2>$null)".Trim()
            $cIn = "$(& git -C $Tree log -1 --format=%H -- $inputs 2>$null)".Trim()
            if ($cBin -and $cIn) {
                & git -C $Tree merge-base --is-ancestor $cIn $cBin 2>$null
                if ($LASTEXITCODE -ne 0) {
                    $subj = "$(& git -C $Tree log -1 --format='%h %s' $cIn 2>$null)"
                    if ($subj.Length -gt 100) { $subj = $subj.Substring(0, 100) + '...' }
                    $problems.Add("STALE: the committed $($b.Src) (last changed in $($cBin.Substring(0, 8))) does not include the later change to its sources in $subj - run $($b.Build) and commit it")
                }
            }
            else { $problems.Add("STALE?: git history of $($b.Src) or its sources could not be read") }
            continue
        }
        $newest = $inputs | ForEach-Object { Get-Item -LiteralPath (Join-Path $Tree $_) } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        $built = (Get-Item -LiteralPath $src).LastWriteTimeUtc
        if ($built -lt $newest.LastWriteTimeUtc) {
            $problems.Add(("STALE: {0} (written {1:yyyy-MM-dd HH:mm:ss} UTC) is older than its source {2} ({3:yyyy-MM-dd HH:mm:ss} UTC) - it is not the build of this tree; run {4}" -f
                           $b.Src, $built, $newest.FullName.Substring($Tree.Length + 1), $newest.LastWriteTimeUtc, $b.Build))
        }
    }

    # ---- 2. the loader marker, and the DLL it names carries the export it names ------------------------------------------
    $markerName = 'shared-wastelands.loader.txt'
    $markerSrc = Join-Path $modSrc $markerName
    if (-not (Test-Path -LiteralPath $markerSrc)) { $problems.Add("MISSING: mod-package\$modDirName\$markerName - SharedWastelandsLoader.dll finds the mod only by it") }
    else {
        $mk = @{}
        foreach ($l in (Get-Content -LiteralPath $markerSrc)) { if ($l -match '^\s*([a-z]+)\s*=\s*(\S+)\s*$') { $mk[$Matches[1]] = $Matches[2] } }
        if ($mk['dll'] -ne 'SharedWastelands.dll') { $problems.Add("MARKER: $markerName names dll=$($mk['dll']), but the package ships SharedWastelands.dll") }
        $dllSrc = Join-Path $Tree 'src\coop-plugin\SharedWastelands.dll'
        if (-not $mk['start']) { $problems.Add("MARKER: $markerName names no start= export") }
        elseif (Test-Path -LiteralPath $dllSrc) {
            $bytes = [System.IO.File]::ReadAllBytes($dllSrc)
            $text = [System.Text.Encoding]::ASCII.GetString($bytes)
            if ($text.IndexOf($mk['start'] + [char]0, [System.StringComparison]::Ordinal) -lt 0) {
                $problems.Add("OLD DLL: src\coop-plugin\SharedWastelands.dll has no '$($mk['start'])' export name - the loader could not start it (a pre-loader build)")
            }
        }
    }

    # ---- 3. the address tables: every tracked table on disk, nothing extra, and signatures.sig -------------------------
    $addrRel = 'src/coop-plugin/addresses'
    $addrSrc = Join-Path $Tree 'src\coop-plugin\addresses'
    $trackedTables = @(Get-Tracked @($addrRel) | Where-Object { $_ -like '*.txt' } | ForEach-Object { Split-Path $_ -Leaf } | Sort-Object)
    $diskTables = @(if (Test-Path -LiteralPath $addrSrc) { Get-ChildItem -LiteralPath $addrSrc -Filter *.txt -File | ForEach-Object { $_.Name } | Sort-Object })
    if ($trackedTables.Count -eq 0) { $problems.Add("NO ADDRESS TABLE: git tracks no $addrRel/*.txt - the plugin would install nothing") }
    foreach ($t in $trackedTables) { if ($diskTables -notcontains $t) { $problems.Add("MISSING TABLE: $addrRel/$t is tracked but not on disk") } }
    foreach ($t in $diskTables) { if ($trackedTables -notcontains $t) { $problems.Add("UNTRACKED TABLE: $addrRel/$t is on disk but not in git - the manifest's commit would not describe it") } }
    if (-not (Test-Path -LiteralPath (Join-Path $addrSrc 'signatures.sig'))) { $problems.Add("MISSING: $addrRel/signatures.sig (tools\gen_signatures.py makes it)") }

    # ---- 4. the plain files copied as they are -----------------------------------------------------------------------
    $plain = @(
        @{ Dst = 'Shared Wastelands.mod';        Src = "mod-package/$modDirName/Shared Wastelands.mod" }
        @{ Dst = '_Shared Wastelands.info';      Src = "mod-package/$modDirName/_Shared Wastelands.info" }
        @{ Dst = $markerName;              Src = "mod-package/$modDirName/$markerName" }
        @{ Dst = 'title-background.png';   Src = "mod-package/$modDirName/title-background.png" }
        @{ Dst = 'licenses\ENet-LICENSE.txt';     Src = 'src/coop-plugin/third_party/enet/LICENSE' }
        @{ Dst = 'licenses\MinHook-LICENSE.txt';  Src = 'src/coop-plugin/third_party/minhook/LICENSE.txt' }
        @{ Dst = 'licenses\MyGUI-COPYING.MIT.txt'; Src = 'src/coop-plugin/third_party/ogre-mygui/include/mygui/COPYING.MIT' }
    )
    $trackedPlain = @(Get-Tracked @($plain | ForEach-Object { $_.Src }))
    foreach ($p in $plain) { if ($trackedPlain -notcontains $p.Src) { $problems.Add("MISSING: $($p.Src) is not a tracked file of this tree") } }

    # ---- 5. the tree is clean where the package comes from (else the commit in the manifest does not describe it) ------
    $binRel = @($bins | ForEach-Object { $_.Src })
    $cleanPaths = @($allInputDirs | Select-Object -Unique) + @($addrRel, "mod-package/$modDirName", 'src/coop-plugin/third_party')
    $dirty = @(& git -C $Tree status --porcelain=v1 --untracked-files=all -- $cleanPaths 2>$null)
    foreach ($l in $dirty) {
        if (-not $l) { continue }
        $path = $l.Substring(3).Trim('"')
        if ($binRel -contains $path) { continue }   # a rebuilt build output (SharedWastelandsServer.exe is tracked) is the point, not a change
        $problems.Add("UNCOMMITTED: '$l' - commit or remove it; the manifest names commit $short")
    }

    if ($problems.Count -gt 0) { throw ("REFUSED - nothing was written:`n  " + ($problems -join "`n  ")) }

    # ---- 6. stage, copy, verify ----------------------------------------------------------------------------------------
    if (-not (Test-Path -LiteralPath $OutRoot)) { New-Item -ItemType Directory -Path $OutRoot | Out-Null }
    $staging = Join-Path $OutRoot (".staging-$Name-" + [System.Diagnostics.Process]::GetCurrentProcess().Id + '-' + [DateTime]::UtcNow.Ticks)
    $stageMod = Join-Path $staging $modDirName
    New-Item -ItemType Directory -Path (Join-Path $stageMod 'addresses') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $stageMod 'licenses') -Force | Out-Null

    $rows = New-Object System.Collections.Generic.List[object]
    $corrupt = $env:KCOOP_PACKAGE_TEST_CORRUPT
    function Copy-Checked([string]$srcRel, [string]$dstRel) {
        $s = Join-Path $Tree ($srcRel -replace '/', '\')
        $d = Join-Path $stageMod $dstRel
        $h1 = Get-Sha256 $s
        Copy-Item -LiteralPath $s -Destination $d -Force
        if ($corrupt -and $corrupt -ieq $dstRel) { [System.IO.File]::AppendAllText($d, 'x') }
        $h2 = Get-Sha256 $d
        $h3 = Get-Sha256 $s
        if ($h2 -ne $h1) { throw "REFUSED: COPY HASH MISMATCH: $dstRel (source $h1, copy $h2) - the package was not kept" }
        if ($h3 -ne $h1) { throw "REFUSED: SOURCE CHANGED WHILE PACKAGING: $srcRel (was $h1, now $h3) - a build is writing it?" }
        $rows.Add([pscustomobject]@{ Path = $dstRel; Size = (Get-Item -LiteralPath $d).Length; Sha = $h1; Src = $srcRel })
    }
    foreach ($b in $bins)  { Copy-Checked $b.Src $b.Dst }
    foreach ($p in $plain) { Copy-Checked $p.Src $p.Dst }
    foreach ($t in $trackedTables) { Copy-Checked "$addrRel/$t" "addresses\$t" }
    Copy-Checked "$addrRel/signatures.sig" 'addresses\signatures.sig'

    # RE_Kenshi's road (see the header): written, naming exactly the one plugin this package ships
    $rkj = Join-Path $stageMod 'RE_Kenshi.json'
    [System.IO.File]::WriteAllText($rkj, "{`r`n    `"PreloadPlugins`": [`"SharedWastelands.dll`"]`r`n}`r`n", (New-Object System.Text.UTF8Encoding($false)))
    $rows.Add([pscustomobject]@{ Path = 'RE_Kenshi.json'; Size = (Get-Item -LiteralPath $rkj).Length; Sha = (Get-Sha256 $rkj); Src = '(written by tools/build-package.ps1: RE_Kenshi road, PreloadPlugins = SharedWastelands.dll)' })

    # the copied tables pass the same check the build runs on the source
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { $py = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $py) { throw "REFUSED: no python on PATH - tools\check_tables.py cannot be run on the copied tables" }
    $ct = & $py.Source (Join-Path $PSScriptRoot 'check_tables.py') (Join-Path $stageMod 'addresses') 2>&1
    if ($LASTEXITCODE -ne 0) { throw ("REFUSED: tools\check_tables.py failed on the copied tables:`n  " + (($ct | ForEach-Object { "$_" }) -join "`n  ")) }

    # ---- 7. the manifest ----------------------------------------------------------------------------------------------
    $manifestName = 'PACKAGE-MANIFEST.txt'
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Shared Wastelands package manifest - written by tools/build-package.ps1 (T-254). Every file of the mod folder')
    $lines.Add('# beside this file: SHA-256, size in bytes, path in the mod folder, and where it came from in the source tree.')
    $lines.Add("package: $Name")
    $lines.Add("mod folder: $modDirName")
    $lines.Add("source tree: $Tree")
    $lines.Add("source commit: $commit")
    $lines.Add("built: " + [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss') + ' UTC')
    $lines.Add("address tables: $($trackedTables.Count) + signatures.sig (tools/check_tables.py: " + (($ct | Select-Object -Last 1) -as [string]) + ')')
    $lines.Add("files: $($rows.Count)")
    $lines.Add('# sha256<TAB>bytes<TAB>path<TAB>source')
    foreach ($r in ($rows | Sort-Object Path)) { $lines.Add(("{0}`t{1}`t{2}`t{3}" -f $r.Sha, $r.Size, ($r.Path -replace '\\', '/'), $r.Src)) }
    [System.IO.File]::WriteAllText((Join-Path $staging $manifestName), (($lines -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))

    # nothing but the listed files is in the staged mod folder, and nothing but it and the manifest beside it
    $staged = @(Get-ChildItem -LiteralPath $stageMod -Recurse -File | ForEach-Object { $_.FullName.Substring($stageMod.Length + 1) })
    $expect = @($rows | ForEach-Object { $_.Path })
    $top = @(Get-ChildItem -LiteralPath $staging | ForEach-Object { $_.Name } | Sort-Object)
    $wantTop = (@($manifestName, $modDirName) | Sort-Object) -join '|'
    if (($top -join '|') -ne $wantTop) { throw "REFUSED: the staged package holds $($top -join ', ') - expected only the mod folder and $manifestName" }
    if ($staged.Count -ne $expect.Count -or @($staged | Where-Object { $expect -notcontains $_ }).Count -gt 0) { throw "REFUSED: the staged folder holds files the manifest does not list" }

    # ---- 8. into place ------------------------------------------------------------------------------------------------
    if (Test-Path -LiteralPath $final) { Send-ToRecycleBin $final }   # only reachable with -Replace (checked at the top)
    Move-Item -LiteralPath $staging -Destination $final
    $staging = $null

    if ($Zip) {
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zipTmp = Join-Path $OutRoot (".staging-$Name-$short-" + [DateTime]::UtcNow.Ticks + '.zip')
        [System.IO.Compression.ZipFile]::CreateFromDirectory($final, $zipTmp, [System.IO.Compression.CompressionLevel]::Optimal, $false)
        # re-read every entry and compare it with the manifest
        $want = @{}
        foreach ($r in $rows) { $want["$modDirName/" + ($r.Path -replace '\\', '/')] = $r.Sha }
        $want[$manifestName] = Get-Sha256 (Join-Path $final $manifestName)
        $za = [System.IO.Compression.ZipFile]::OpenRead($zipTmp)
        try {
            $seen = 0
            $sha = [System.Security.Cryptography.SHA256]::Create()
            foreach ($e in $za.Entries) {
                if ($e.FullName.EndsWith('/')) { continue }
                $k = $e.FullName -replace '\\', '/'
                $st = $e.Open(); try { $h = ([System.BitConverter]::ToString($sha.ComputeHash($st))) -replace '-', '' } finally { $st.Dispose() }
                if (-not $want.ContainsKey($k) -or $want[$k] -ne $h) { throw "REFUSED: ZIP CHECK: entry $k does not match the manifest" }
                $seen++
            }
            if ($seen -ne $want.Count) { throw "REFUSED: ZIP CHECK: $seen entries, the manifest lists $($want.Count)" }
        }
        finally { $za.Dispose() }
        if (Test-Path -LiteralPath $zipPath) { Send-ToRecycleBin $zipPath }
        Move-Item -LiteralPath $zipTmp -Destination $zipPath
        $zipTmp = $null
    }

    Write-Host ("PACKAGE OK: {0}\{1}\ - {2} files, manifest beside it ({0}\{3}), commit {4}" -f $final, $modDirName, $rows.Count, $manifestName, $short) -ForegroundColor Green
    foreach ($r in ($rows | Sort-Object Path)) { Write-Host ("  {0,-34} {1,9} bytes  {2}..." -f ($r.Path -replace '\\', '/'), $r.Size, $r.Sha.Substring(0, 12)) }
    if ($Zip) { Write-Host ("ZIP OK: {0} ({1} bytes)" -f $zipPath, (Get-Item -LiteralPath $zipPath).Length) -ForegroundColor Green }
    exit 0
}
catch {
    $msg = "$($_.Exception.Message)"
    if ($msg -notlike 'REFUSED*') { $msg = "REFUSED: unexpected error - $msg" }
    try { Send-ToRecycleBin $staging } catch { $msg += "`n  (the staging folder $staging could not be sent to the Recycle Bin: $($_.Exception.Message))" }
    try { Send-ToRecycleBin $zipTmp } catch { $msg += "`n  (the temporary zip $zipTmp could not be sent to the Recycle Bin: $($_.Exception.Message))" }
    Write-Host $msg -ForegroundColor Red
    exit 1
}
