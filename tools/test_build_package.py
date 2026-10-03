"""Offline test of tools/build-package.ps1 (T-254): a fake source tree in a temp folder (its own git repo, stub binaries,
the real address tables copied in) - no game, no instance, no build.

  python tools/test_build_package.py

Covers: a clean package (manifest, zip, nothing left staged); refusal on a missing table, a stale DLL and a copy hash
mismatch, each leaving nothing behind; and a PowerShell parse check. Temp folders go to the Recycle Bin at the end.
"""
import os, shutil, subprocess, sys, tempfile, time, unittest, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "build-package.ps1")
ADDR = os.path.join(HERE, "..", "src", "coop-plugin", "addresses")
PWSH = shutil.which("pwsh")
ROOT = None   # one temp root for the whole run, recycled once at the end


def setUpModule():
    global ROOT
    ROOT = tempfile.mkdtemp(prefix="t254-")


def tearDownModule():
    if ROOT and PWSH:
        p = ROOT.replace("'", "''")
        subprocess.run([PWSH, "-NoProfile", "-Command",
                        "Add-Type -AssemblyName Microsoft.VisualBasic; "
                        "[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory('%s', 'OnlyErrorDialogs', 'SendToRecycleBin')" % p],
                        capture_output=True, text=True)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def git(tree, *args):
    subprocess.run(["git", "-C", tree] + list(args), check=True, capture_output=True, text=True)


def make_tree(name):
    """A tree shaped like the real one: tracked sources, the real address tables, and build outputs written AFTER the
    sources (so they are current)."""
    t = os.path.join(ROOT, name, "tree")
    src = {
        ".gitignore": "*.dll\nsrc/installer/*.exe\n",
        "src/coop-plugin/coop.cpp": '#include "loadermarker.h"\nint x;\n',
        "src/coop-plugin/build.bat": "rem build\n",
        "src/coop-plugin/third_party/enet/LICENSE": "ENet MIT licence text\n",
        "src/coop-plugin/third_party/minhook/LICENSE.txt": "MinHook BSD licence text\n",
        "src/coop-plugin/third_party/ogre-mygui/include/mygui/COPYING.MIT": "MyGUI MIT licence text\n",
        "src/common/loadermarker.h": '#include "modsenabled.h"\n',
        "src/common/modsenabled.h": "/* mods */\n",
        "src/common/installtext.h": "/* install text */\n",
        "src/kenshi-loader/loader.cpp": '#include "loadermarker.h"\n',
        "src/installer/setup_main.cpp": '#include "installtext.h"\n',
        "src/coop-store/store_main.cpp": '#include "../common/clockmath.h"\nint main(){}\n',
        "src/coop-store/build.bat": "cl store_main.cpp ..\\common\\clockmath.cpp\n",
        "src/common/clockmath.cpp": '#include "clockmath.h"\n',
        "src/common/clockmath.h": "/* clock */\n",
        "src/common/shopwire.h": "/* plugin only */\n",
        "src/coop-store/SharedWastelandsServer.exe": b"MZ store stub",
        "mod-package/Shared Wastelands/Shared Wastelands.mod": b"\x10\x00mod",
        "mod-package/Shared Wastelands/_Shared Wastelands.info": "<info/>\n",
        "mod-package/Shared Wastelands/shared-wastelands.loader.txt": "# marker\nformat=1\ndll=SharedWastelands.dll\nstart=coopEarlyStart\n",
        "mod-package/Shared Wastelands/title-background.png": b"\x89PNG\r\n\x1a\n title art stub",
    }
    for rel, data in src.items():
        write(os.path.join(t, rel), data)
    addr = os.path.join(t, "src", "coop-plugin", "addresses")
    os.makedirs(addr, exist_ok=True)
    for f in os.listdir(ADDR):
        if f.endswith(".txt") or f == "signatures.sig":
            shutil.copyfile(os.path.join(ADDR, f), os.path.join(addr, f))
    subprocess.run(["git", "init", "-q", t], check=True)
    git(t, "config", "user.email", "t@t")
    git(t, "config", "user.name", "t")
    git(t, "add", "-A")
    git(t, "commit", "-q", "-m", "fixture")
    time.sleep(0.05)
    write(os.path.join(t, "src/coop-plugin/SharedWastelands.dll"), b"MZ plugin stub\x00coopEarlyStart\x00startPlugin\x00")
    write(os.path.join(t, "src/kenshi-loader/SharedWastelandsLoader.dll"), b"MZ loader stub")
    write(os.path.join(t, "src/installer/SharedWastelandsSetup.exe"), b"MZ setup stub")
    now = time.time() + 5   # build outputs clearly newer than every source
    for rel in ("src/coop-plugin/SharedWastelands.dll", "src/kenshi-loader/SharedWastelandsLoader.dll",
                "src/installer/SharedWastelandsSetup.exe", "src/coop-store/SharedWastelandsServer.exe"):
        os.utime(os.path.join(t, rel), (now, now))
    return t


def run(tree, out, *extra, env_add=None):
    env = dict(os.environ)
    env.pop("KCOOP_PACKAGE_TEST_CORRUPT", None)
    env.update(env_add or {})
    r = subprocess.run([PWSH, "-NoProfile", "-File", SCRIPT, "-Tree", tree, "-OutRoot", out, "-Name", "pkg"] + list(extra),
                       capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


@unittest.skipUnless(PWSH, "pwsh not on PATH")
class BuildPackage(unittest.TestCase):
    def assert_nothing_left(self, out):
        left = os.listdir(out) if os.path.isdir(out) else []
        self.assertEqual(left, [], "a refusal left files behind: %s" % left)

    def test_clean_package_and_zip(self):
        t = make_tree("ok")
        out = os.path.join(ROOT, "ok", "out")
        rc, text = run(t, out, "-Zip")
        self.assertEqual(rc, 0, text)
        mod = os.path.join(out, "pkg", "Shared Wastelands")
        manifest = os.path.join(out, "pkg", "PACKAGE-MANIFEST.txt")   # beside the mod folder, not in it (owner 244)
        for f in ("SharedWastelands.dll", "SharedWastelandsLoader.dll", "SharedWastelandsSetup.exe", "SharedWastelandsServer.exe", "Shared Wastelands.mod",
                  "_Shared Wastelands.info", "shared-wastelands.loader.txt", "title-background.png", "RE_Kenshi.json", "addresses/signatures.sig",
                  "licenses/ENet-LICENSE.txt", "licenses/MinHook-LICENSE.txt", "licenses/MyGUI-COPYING.MIT.txt"):
            self.assertTrue(os.path.isfile(os.path.join(mod, f)), f)
        self.assertFalse(os.path.exists(os.path.join(mod, "PACKAGE-MANIFEST.txt")), "the manifest is inside the mod folder")
        self.assertEqual(sorted(os.listdir(os.path.join(out, "pkg"))), ["PACKAGE-MANIFEST.txt", "Shared Wastelands"])
        with open(manifest, encoding="utf-8") as f:
            man = f.read()
        head = subprocess.run(["git", "-C", t, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        self.assertIn("source commit: " + head, man)
        self.assertIn("SharedWastelands.dll\tsrc/coop-plugin/SharedWastelands.dll", man)
        with open(os.path.join(mod, "RE_Kenshi.json"), encoding="utf-8") as f:
            self.assertEqual(f.read().split(), ["{", '"PreloadPlugins":', '["SharedWastelands.dll"]', "}"])
        self.assertEqual(sorted(os.listdir(out)), ["pkg", "pkg-%s.zip" % head[:8]])   # no staging left
        with zipfile.ZipFile(os.path.join(out, "pkg-%s.zip" % head[:8])) as z:
            names = z.namelist()
        self.assertIn("Shared Wastelands/SharedWastelands.dll", [n.replace("\\", "/") for n in names])
        self.assertIn("PACKAGE-MANIFEST.txt", [n.replace("\\", "/") for n in names])
        rc2, text2 = run(t, out)   # the same name again without -Replace refuses before doing anything
        self.assertEqual(rc2, 1, text2)
        self.assertIn("already exists", text2)

    def test_refuses_missing_table(self):
        t = make_tree("table")
        addr = os.path.join(t, "src", "coop-plugin", "addresses")
        tbl = sorted(f for f in os.listdir(addr) if f.endswith(".txt"))[0]
        os.rename(os.path.join(addr, tbl), os.path.join(addr, tbl + ".off"))   # tracked, but gone from where it ships from
        out = os.path.join(ROOT, "table", "out")
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("MISSING TABLE", text)
        self.assert_nothing_left(out)

    def test_refuses_stale_dll(self):
        t = make_tree("stale")
        old = time.time() - 3600
        os.utime(os.path.join(t, "src/coop-plugin/SharedWastelands.dll"), (old, old))   # a source changed after the build
        out = os.path.join(ROOT, "stale", "out")
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("STALE: src/coop-plugin/SharedWastelands.dll", text)
        self.assert_nothing_left(out)

    def test_store_staleness_follows_its_includes(self):
        t = make_tree("walk")
        out = os.path.join(ROOT, "walk", "out")
        # a later commit to a header the plugin compiles and SharedWastelandsServer.exe does not: the (untracked, by write time) DLL is
        # stale, the (committed, by history) SharedWastelandsServer.exe is not
        write(os.path.join(t, "src/common/shopwire.h"), "/* plugin only, changed */\n")
        git(t, "commit", "-q", "-am", "shopwire")
        later = time.time() + 60   # written after the stub build outputs (make_tree dates them 5 s ahead)
        os.utime(os.path.join(t, "src/common/shopwire.h"), (later, later))
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("STALE: src/coop-plugin/SharedWastelands.dll", text)
        self.assertNotIn("SharedWastelandsServer.exe", text)
        # a later commit to a header reached from a .cpp the store's build.bat names: the committed exe is stale
        write(os.path.join(t, "src/common/clockmath.h"), "/* clock, changed */\n")
        git(t, "commit", "-q", "-am", "clockmath change")
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("STALE: the committed src/coop-store/SharedWastelandsServer.exe", text)
        self.assertIn("clockmath change", text)
        self.assert_nothing_left(out)

    def test_committed_exe_checked_out_after_is_not_stale(self):
        t = make_tree("order")
        old = time.time() - 3600   # git wrote the exe BEFORE its source in the same checkout: not a stale build
        os.utime(os.path.join(t, "src/coop-store/SharedWastelandsServer.exe"), (old, old))
        out = os.path.join(ROOT, "order", "out")
        rc, text = run(t, out)
        self.assertEqual(rc, 0, text)

    def test_refuses_pre_loader_dll(self):
        t = make_tree("old")
        write(os.path.join(t, "src/coop-plugin/SharedWastelands.dll"), b"MZ old plugin\x00startPlugin\x00")
        now = time.time() + 5
        os.utime(os.path.join(t, "src/coop-plugin/SharedWastelands.dll"), (now, now))
        out = os.path.join(ROOT, "old", "out")
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("OLD DLL", text)
        self.assert_nothing_left(out)

    def test_refuses_hash_mismatch(self):
        t = make_tree("hash")
        out = os.path.join(ROOT, "hash", "out")
        rc, text = run(t, out, "-Zip", env_add={"KCOOP_PACKAGE_TEST_CORRUPT": "SharedWastelands.dll"})
        self.assertEqual(rc, 1, text)
        self.assertIn("COPY HASH MISMATCH: SharedWastelands.dll", text)
        self.assert_nothing_left(out)   # the staging folder went to the Recycle Bin; no package, no zip

    def test_refuses_uncommitted_source(self):
        t = make_tree("dirty")
        write(os.path.join(t, "src/common/new.h"), "/* not committed */\n")
        out = os.path.join(ROOT, "dirty", "out")
        rc, text = run(t, out)
        self.assertEqual(rc, 1, text)
        self.assertIn("UNCOMMITTED", text)
        self.assert_nothing_left(out)

    def test_script_parses(self):
        p = SCRIPT.replace("'", "''")
        r = subprocess.run([PWSH, "-NoProfile", "-Command",
                            "$e = $null; $null = [System.Management.Automation.Language.Parser]::ParseFile('%s', [ref]$null, [ref]$e); $e.Count" % p],
                           capture_output=True, text=True)
        self.assertEqual(r.stdout.strip(), "0", r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
