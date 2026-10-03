// makemod — generates a minimal valid Kenshi .mod file so our plugin's mod folder
// appears in Kenshi's mod list (required for RE_Kenshi's plugin loader to find
// RE_Kenshi.json). Uses OpenConstructionSet, the community library for Kenshi's
// binary mod format.
//
// Usage:
//   dotnet run -- dump            reflection-dump the relevant OCS types
//   dotnet run -- make <out.mod>  write a minimal empty mod

using System.Reflection;

if (args.Length == 0) { Console.WriteLine("usage: dump | make <out.mod>"); return; }

var ocs = AppDomain.CurrentDomain.GetAssemblies()
    .Concat(new[] { Assembly.Load("OpenConstructionSet") })
    .First(a => a.GetName().Name == "OpenConstructionSet");

if (args[0] == "dump")
{
    foreach (var t in ocs.GetExportedTypes().OrderBy(t => t.FullName))
    {
        if (!t.FullName!.Contains("Mod") && !t.FullName.Contains("Data")) continue;
        Console.WriteLine($"\n=== {t.FullName} ===");
        foreach (var c in t.GetConstructors())
            Console.WriteLine("  ctor(" + string.Join(", ", c.GetParameters().Select(p => $"{p.ParameterType.Name} {p.Name}")) + ")");
        foreach (var p in t.GetProperties())
            Console.WriteLine($"  prop {p.PropertyType.Name} {p.Name}{(p.CanWrite ? " {set}" : "")}");
        foreach (var m in t.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            if (!m.IsSpecialName) Console.WriteLine($"  method {m.ReturnType.Name} {m.Name}(" + string.Join(", ", m.GetParameters().Select(p => p.ParameterType.Name)) + ")");
    }
    return;
}

if (args[0] == "make" && args.Length > 1)
{
    var outPath = args[1];
    // the text Kenshi's mod list shows for "Shared Wastelands.mod" (the name repeats src/common/names.h kModName)
    var header = new OpenConstructionSet.Data.Header(1, "Shared Wastelands",
        "Play Kenshi together: several players in one shared world.");
    OpenConstructionSet.Mods.ModInfoData? info = null;
    try { info = Activator.CreateInstance<OpenConstructionSet.Mods.ModInfoData>(); }
    catch { Console.WriteLine("note: ModInfoData has no public ctor; writing with null info"); }

    var data = new OpenConstructionSet.Mods.ModFileData(
        header, 0, Array.Empty<OpenConstructionSet.Data.Item>(), info!);
    var mod = new OpenConstructionSet.Mods.ModFile(outPath);
    await mod.WriteDataAsync(data);
    Console.WriteLine($"wrote {outPath} ({new FileInfo(outPath).Length} bytes)");

    // verify by re-read
    var verifyHeader = await new OpenConstructionSet.Mods.ModFile(outPath).ReadHeaderAsync();
    var verifyData = await new OpenConstructionSet.Mods.ModFile(outPath).ReadDataAsync();
    Console.WriteLine($"verify: version={verifyHeader.Version} author='{verifyHeader.Author}' items={verifyData.Items.Count} lastId={verifyData.LastId}");
    return;
}
