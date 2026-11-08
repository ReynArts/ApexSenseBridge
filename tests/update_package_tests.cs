using ApexSenseBridge.Security;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

internal static class UpdatePackageTests
{
    private const string Publisher = "CN=ApexSenseBridge Test Publisher";

    private static int assertions;
    private static string root;
    private static int caseIndex;

    private sealed class Spec
    {
        public string Section;
        public string Path;
        public byte[] Data;
        public string EntryName;
        public string ManifestSha;
        public long? ManifestSize;
        public bool Listed = true;
        public bool InZip = true;

        public Spec(string section, string path, string text)
        {
            Section = section;
            Path = path;
            Data = Encoding.ASCII.GetBytes(text);
        }
    }

    public static int Run(string testRoot)
    {
        assertions = 0;
        root = System.IO.Path.Combine(testRoot, "update-package");
        Directory.CreateDirectory(root);

        var trust = UpdatePackage.SignatureTrustCheck;
        var installedTrust = UpdatePackage.InstalledTrustCheck;
        var subject = UpdatePackage.SignerSubjectReader;
        var version = UpdatePackage.InstalledVersionReader;
        var registry = UpdatePackage.InstalledRegistryReader;
        try
        {
            UseFakeEnvironment();
            TestAssetPolicy();
            TestValidPackage();
            TestExtractSections();
            TestExtractOverwritesOnlyListedFiles();
            TestTamperedAndMismatchedContent();
            TestExtraAndMissingEntries();
            TestPathTraversalRejected();
            TestDuplicateEntriesRejected();
            TestSizeLimits();
            TestVersionRules();
            TestSignerRules();
            TestManifestStrictness();
            TestDriversMatch();
            TestExtractRechecksPackage();
            TestRealAuthenticodeRejectsUnsignedPackage();
            TestRealDefaultsRejectUnsignedInstalledEngine();
        }
        finally
        {
            UpdatePackage.SignatureTrustCheck = trust;
            UpdatePackage.InstalledTrustCheck = installedTrust;
            UpdatePackage.SignerSubjectReader = subject;
            UpdatePackage.InstalledVersionReader = version;
            UpdatePackage.InstalledRegistryReader = registry;
        }

        return assertions;
    }

    private static void UseFakeEnvironment()
    {
        UpdatePackage.SignatureTrustCheck = path => true;
        UpdatePackage.InstalledTrustCheck = path => true;
        UpdatePackage.SignerSubjectReader = path => Publisher;
        UpdatePackage.InstalledVersionReader = path => new Version(1, 0, 0);
        UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0", "1.5.230");
    }

    private static Func<string, string> DriverRegistry(string usbip, string hidHide)
    {
        return name =>
            name == "UsbipVersion" ? usbip :
            name == "HidHideVersion" ? hidHide : null;
    }

    private static List<Spec> StandardFiles()
    {
        return new List<Spec>
        {
            new Spec("app", "ApexSenseBridgeTray.exe", "tray exe 1.1.0"),
            new Spec("app", "ApexSenseBridge.exe", "engine exe 1.1.0"),
            new Spec("app", "Data/supported_games.json", "{\"games\":[0]}"),
            new Spec("playnite", "ApexSenseBridge.dll", "playnite dll 1.1.0"),
            new Spec("playnite", "extension.yaml", "Id: test")
        };
    }

    private static string NewCaseDirectory(string name)
    {
        string directory = System.IO.Path.Combine(root, (++caseIndex) + "-" + name);
        Directory.CreateDirectory(directory);
        return directory;
    }

    private static string Sha256Hex(byte[] data)
    {
        using (var sha = SHA256.Create())
        {
            return string.Concat(sha.ComputeHash(data).Select(b => b.ToString("x2")));
        }
    }

    private static string JsonString(string value)
    {
        return "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
    }

    private static string ManifestJson(string version, bool driversChanged,
        string usbip, string hidHide, IEnumerable<Spec> files, string schema = "1")
    {
        var builder = new StringBuilder();
        builder.Append("{\"schema\":").Append(schema);
        builder.Append(",\"version\":").Append(JsonString(version));
        builder.Append(",\"driversChanged\":").Append(driversChanged ? "true" : "false");
        builder.Append(",\"usbipVersion\":").Append(JsonString(usbip));
        builder.Append(",\"hidHideVersion\":").Append(JsonString(hidHide));
        builder.Append(",\"files\":[");
        bool first = true;
        foreach (var file in files.Where(f => f.Listed))
        {
            if (!first) builder.Append(",");
            first = false;
            builder.Append("{\"section\":").Append(JsonString(file.Section));
            builder.Append(",\"path\":").Append(JsonString(file.Path));
            builder.Append(",\"sha256\":").Append(JsonString(file.ManifestSha ?? Sha256Hex(file.Data)));
            builder.Append(",\"size\":").Append(file.ManifestSize ?? file.Data.Length);
            builder.Append("}");
        }

        builder.Append("]}");
        return builder.ToString();
    }

    private static string BuildPackage(string directory, IEnumerable<Spec> files,
        string version = "1.1.0", bool driversChanged = false,
        string usbip = "0.9.8.0", string hidHide = "1.5.230",
        string manifestJson = null, string fileName = "package.zip")
    {
        var list = files.ToList();
        var patches = new Dictionary<string, string>();
        string path = System.IO.Path.Combine(directory, fileName);
        using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write))
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create))
        {
            var manifestEntry = archive.CreateEntry("manifest.json");
            using (var output = manifestEntry.Open())
            {
                byte[] bytes = new UTF8Encoding(false).GetBytes(manifestJson ??
                    ManifestJson(version, driversChanged, usbip, hidHide, list));
                output.Write(bytes, 0, bytes.Length);
            }

            foreach (var file in list.Where(f => f.InZip))
            {
                string name = file.EntryName ?? (file.Section + "/" + file.Path);
                string safe = new string(name.Select(c => c < 32 || "<>|?*\"".IndexOf(c) >= 0 ? (char)81 : c).ToArray());
                if (safe != name) patches[safe] = name;
                var entry = archive.CreateEntry(safe);
                using (var output = entry.Open())
                {
                    output.Write(file.Data, 0, file.Data.Length);
                }
            }
        }

        if (patches.Count > 0)
        {
            byte[] bytes = File.ReadAllBytes(path);
            foreach (var patch in patches)
            {
                byte[] from = Encoding.ASCII.GetBytes(patch.Key);
                byte[] to = Encoding.ASCII.GetBytes(patch.Value);
                for (int i = 0; i + from.Length <= bytes.Length; i++)
                {
                    int k = 0;
                    while (k < from.Length && bytes[i + k] == from[k]) k++;
                    if (k == from.Length) Array.Copy(to, 0, bytes, i, to.Length);
                }
            }

            File.WriteAllBytes(path, bytes);
        }

        return path;
    }

    private static bool Verify(string zip, string expectedVersion, out UpdatePackageManifest manifest,
        out string error)
    {
        string engine = System.IO.Path.Combine(System.IO.Path.GetDirectoryName(zip), "ApexSenseBridge.exe");
        if (!File.Exists(engine)) File.WriteAllText(engine, "installed engine");
        return UpdatePackage.Verify(zip, expectedVersion, engine, out manifest, out error);
    }

    private static bool VerifyRejects(string zip, string expectedVersion, string expectedFragment,
        string message)
    {
        UpdatePackageManifest manifest;
        string error;
        bool ok = Verify(zip, expectedVersion, out manifest, out error);
        Assert(!ok, message + " (was accepted)");
        Assert(manifest == null, message + " (manifest leaked)");
        Assert(!string.IsNullOrEmpty(error), message + " (no error text)");
        if (expectedFragment != null)
        {
            Assert(error.IndexOf(expectedFragment, StringComparison.OrdinalIgnoreCase) >= 0,
                message + " (unexpected error: " + error + ")");
        }

        return !ok;
    }

    private static void TestAssetPolicy()
    {
        const string url =
            "https://github.com/ReynArts/ApexSenseBridge/releases/download/v1.1.0/ApexSenseBridge-Update.zip";
        Assert(UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip", url),
            "official update asset must be accepted");
        Assert(UpdatePackage.IsExpectedUpdateAsset("apexsensebridge-update.ZIP", url),
            "asset name match ignores case");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Setup.exe", url),
            "setup name must not match the update asset");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip.exe", url),
            "name must match exactly");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip",
            url.Replace("https://", "http://")), "http must be rejected");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip",
            url.Replace("github.com", "example.com")), "other hosts must be rejected");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip",
            url.Replace("/ReynArts/", "/Someone/")), "other repositories must be rejected");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip", null),
            "null url must be rejected");
        Assert(!UpdatePackage.IsExpectedUpdateAsset(null, url), "null name must be rejected");
        Assert(!UpdatePackage.IsExpectedUpdateAsset("ApexSenseBridge-Update.zip",
            "https://github.com.evil.com/ReynArts/ApexSenseBridge/releases/download/v1/x"),
            "look-alike host must be rejected");
    }

    private static void TestValidPackage()
    {
        string directory = NewCaseDirectory("valid");
        string zip = BuildPackage(directory, StandardFiles());
        UpdatePackageManifest manifest;
        string error;
        Assert(Verify(zip, "v1.1.0", out manifest, out error), "valid package rejected: " + error);
        Assert(error == null, "error must be null on success");
        Assert(manifest != null && manifest.Schema == 1 && manifest.Version == "1.1.0",
            "manifest fields not parsed");
        Assert(!manifest.DriversChanged && manifest.UsbipVersion == "0.9.8.0" &&
               manifest.HidHideVersion == "1.5.230", "driver fields not parsed");
        Assert(manifest.Files.Count == 5, "wrong file count");
        Assert(manifest.Files.Any(f => f.Section == "app" && f.Path == "Data/supported_games.json" &&
                                       f.Size == 13 && f.Sha256.Length == 64),
            "nested file not listed correctly");

        Assert(Verify(zip, "1.1.0", out manifest, out error), "version without v prefix rejected");
        Assert(Directory.GetDirectories(directory, ".verify-*").Length == 0,
            "verification left a temporary directory behind");

        UpdatePackageManifest quick;
        Assert(UpdatePackage.ReadManifest(zip, out quick, out error) && quick.Files.Count == 5,
            "ReadManifest failed on a valid package");

        string missing;
        Assert(!UpdatePackage.ReadManifest(System.IO.Path.Combine(directory, "none.zip"), out quick, out missing) &&
               !string.IsNullOrEmpty(missing), "ReadManifest must fail on a missing file");
        string garbage = System.IO.Path.Combine(directory, "garbage.zip");
        File.WriteAllText(garbage, "this is not a zip");
        Assert(!UpdatePackage.ReadManifest(garbage, out quick, out missing) &&
               !string.IsNullOrEmpty(missing), "ReadManifest must fail on garbage");
        UpdatePackageManifest verified;
        Assert(!UpdatePackage.Verify(garbage, "1.1.0", System.IO.Path.Combine(directory, "ApexSenseBridge.exe"),
            out verified, out missing) && verified == null, "Verify must fail on garbage");
    }

    private static void TestExtractSections()
    {
        string directory = NewCaseDirectory("extract");
        string zip = BuildPackage(directory, StandardFiles());
        UpdatePackageManifest manifest;
        string error;
        Assert(Verify(zip, "1.1.0", out manifest, out error), "extract fixture rejected: " + error);

        string appDirectory = System.IO.Path.Combine(directory, "install");
        List<string> written;
        Assert(UpdatePackage.Extract(zip, manifest, "app", appDirectory, out written, out error),
            "app extract failed: " + error);
        Assert(written.Count == 3, "app extract wrote the wrong number of files");
        Assert(File.ReadAllText(System.IO.Path.Combine(appDirectory, "ApexSenseBridgeTray.exe")) == "tray exe 1.1.0",
            "app file content wrong");
        Assert(File.ReadAllText(System.IO.Path.Combine(appDirectory, "Data", "supported_games.json")) == "{\"games\":[0]}",
            "nested app file content wrong");
        Assert(written.Contains("Data\\supported_games.json"), "written paths must be relative with backslashes");
        Assert(!File.Exists(System.IO.Path.Combine(appDirectory, "extension.yaml")) &&
               !File.Exists(System.IO.Path.Combine(appDirectory, "ApexSenseBridge.dll")),
            "playnite files leaked into the app section");
        Assert(!File.Exists(System.IO.Path.Combine(appDirectory, "manifest.json")),
            "manifest.json must never be extracted");
        Assert(Directory.GetFiles(appDirectory, "*.tmp", SearchOption.AllDirectories).Length == 0,
            "temporary files left behind");

        string playniteDirectory = System.IO.Path.Combine(directory, "playnite");
        Assert(UpdatePackage.Extract(zip, manifest, "playnite", playniteDirectory, out written, out error),
            "playnite extract failed: " + error);
        Assert(written.Count == 2 && Directory.GetFiles(playniteDirectory).Length == 2,
            "playnite extract wrote the wrong files");

        string unknown = System.IO.Path.Combine(directory, "unknown");
        Assert(!UpdatePackage.Extract(zip, manifest, "other", unknown, out written, out error) &&
               written.Count == 0 && !Directory.Exists(unknown), "unknown section must be refused");
        Assert(!UpdatePackage.Extract(zip, manifest, "app", "  ", out written, out error),
            "blank destination must be refused");
        Assert(!UpdatePackage.Extract(zip, null, "app", unknown, out written, out error),
            "null manifest must be refused");
    }

    private static void TestExtractOverwritesOnlyListedFiles()
    {
        string directory = NewCaseDirectory("overwrite");
        string zip = BuildPackage(directory, StandardFiles());
        UpdatePackageManifest manifest;
        string error;
        Assert(Verify(zip, "1.1.0", out manifest, out error), "overwrite fixture rejected: " + error);

        string install = System.IO.Path.Combine(directory, "install");
        Directory.CreateDirectory(System.IO.Path.Combine(install, "Data"));
        File.WriteAllText(System.IO.Path.Combine(install, "ApexSenseBridgeTray.exe"), "old tray");
        File.WriteAllText(System.IO.Path.Combine(install, "Unlisted.txt"), "keep me");
        File.WriteAllText(System.IO.Path.Combine(install, "Data", "user.json"), "user data");

        List<string> written;
        Assert(UpdatePackage.Extract(zip, manifest, "app", install, out written, out error),
            "extract over an existing install failed: " + error);
        Assert(File.ReadAllText(System.IO.Path.Combine(install, "ApexSenseBridgeTray.exe")) == "tray exe 1.1.0",
            "existing file was not replaced");
        Assert(File.ReadAllText(System.IO.Path.Combine(install, "Unlisted.txt")) == "keep me",
            "unlisted file was modified");
        Assert(File.ReadAllText(System.IO.Path.Combine(install, "Data", "user.json")) == "user data",
            "unlisted nested file was modified");
        Assert(Directory.GetFiles(install, "*.tmp", SearchOption.AllDirectories).Length == 0,
            "temporary files left behind after replacing");

        string locked = System.IO.Path.Combine(directory, "locked");
        Directory.CreateDirectory(locked);
        string lockedFile = System.IO.Path.Combine(locked, "ApexSenseBridge.exe");
        File.WriteAllText(lockedFile, "in use");
        using (new FileStream(lockedFile, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            Assert(!UpdatePackage.Extract(zip, manifest, "app", locked, out written, out error) &&
                   !string.IsNullOrEmpty(error), "extract over a locked file must fail");
        }

        Assert(Directory.GetFiles(locked, "*.tmp", SearchOption.AllDirectories).Length == 0,
            "temporary file left behind after a failed replace");
    }

    private static void TestTamperedAndMismatchedContent()
    {
        var files = StandardFiles();
        files[0].ManifestSha = Sha256Hex(Encoding.ASCII.GetBytes("something else"));
        VerifyRejects(BuildPackage(NewCaseDirectory("badhash"), files), "1.1.0", "hash",
            "wrong sha256 must be rejected");

        files = StandardFiles();
        files[1].ManifestSize = files[1].Data.Length + 1;
        VerifyRejects(BuildPackage(NewCaseDirectory("badsize"), files), "1.1.0", "size",
            "wrong size must be rejected");

        files = StandardFiles();
        string original = Sha256Hex(files[2].Data);
        files[2].Data = Encoding.ASCII.GetBytes("{\"games\":[1]}");
        files[2].ManifestSha = original;
        files[2].ManifestSize = 13;
        VerifyRejects(BuildPackage(NewCaseDirectory("swapped"), files), "1.1.0", "hash",
            "same-size content with another hash must be rejected");

        files = StandardFiles();
        files[0].ManifestSha = files[0].ManifestSha ?? Sha256Hex(files[0].Data).ToUpperInvariant();
        VerifyRejects(BuildPackage(NewCaseDirectory("upperhash"), files), "1.1.0", "sha256",
            "upper-case sha256 in the manifest must be rejected");

        files = StandardFiles();
        files[0].ManifestSha = "abc123";
        VerifyRejects(BuildPackage(NewCaseDirectory("shorthash"), files), "1.1.0", "sha256",
            "short sha256 in the manifest must be rejected");
    }

    private static void TestExtraAndMissingEntries()
    {
        var files = StandardFiles();
        files.Add(new Spec("app", "Extra.exe", "not listed") { Listed = false });
        VerifyRejects(BuildPackage(NewCaseDirectory("extra"), files), "1.1.0", "unlisted",
            "an entry missing from the manifest must be rejected");

        files = StandardFiles();
        files[3].InZip = false;
        VerifyRejects(BuildPackage(NewCaseDirectory("missing"), files), "1.1.0", "missing",
            "a listed file missing from the zip must be rejected");

        files = StandardFiles();
        files.Add(new Spec("other", "x.txt", "unknown section"));
        VerifyRejects(BuildPackage(NewCaseDirectory("section"), files), "1.1.0", "section",
            "an unknown section must be rejected");

        files = StandardFiles();
        files.Add(new Spec("app", "Folder/", "") { EntryName = "app/Folder/", Listed = false });
        VerifyRejects(BuildPackage(NewCaseDirectory("directory"), files), "1.1.0", null,
            "a directory entry must be rejected");

        files = StandardFiles();
        files.Add(new Spec("app", "manifest.json", "{}") { EntryName = "Manifest.json", Listed = false });
        VerifyRejects(BuildPackage(NewCaseDirectory("secondmanifest"), files), "1.1.0", null,
            "a second manifest differing by case must be rejected");

        files = StandardFiles();
        string noManifest = System.IO.Path.Combine(NewCaseDirectory("nomanifest"), "package.zip");
        using (var stream = new FileStream(noManifest, FileMode.Create, FileAccess.Write))
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create))
        {
            var entry = archive.CreateEntry("app/ApexSenseBridgeTray.exe");
            using (var output = entry.Open()) output.WriteByte(1);
        }

        VerifyRejects(noManifest, "1.1.0", "manifest", "a zip without manifest.json must be rejected");
    }

    private static void TestPathTraversalRejected()
    {
        string[] badPaths =
        {
            "../evil.txt", "a/../../evil.txt", "a/../evil.txt", "/abs.txt", "C:/evil.txt",
            "C:evil.txt", "a/b:stream", "a\\evil.txt", "a//b.txt", "./a.txt", "a/./b.txt",
            "CON", "con.txt", "a/NUL.dll", "LPT1", "a/trailing.", "a/trailing ", "a/b?.txt",
            "a/b*.txt", "a/b|c.txt", "a/b<c.txt", "", "a/"
        };

        foreach (string bad in badPaths)
        {
            string directory = NewCaseDirectory("slip");
            string outside = System.IO.Path.Combine(directory, "evil.txt");

            var listed = StandardFiles();
            listed.Add(new Spec("app", bad, "evil") { EntryName = "app/" + bad });
            UpdatePackageManifest manifest;
            string error;
            string zip = BuildPackage(directory, listed);
            Assert(!UpdatePackage.ReadManifest(zip, out manifest, out error) && manifest == null,
                "manifest listing '" + bad + "' must be rejected");

            var unlisted = StandardFiles();
            unlisted.Add(new Spec("app", bad, "evil") { EntryName = "app/" + bad, Listed = false });
            zip = BuildPackage(directory, unlisted, fileName: "unlisted.zip");
            VerifyRejects(zip, "1.1.0", null, "zip entry 'app/" + bad + "' must be rejected");

            if (bad.Length > 0)
            {
                var rooted = StandardFiles();
                rooted.Add(new Spec("app", "x.txt", "evil") { EntryName = bad, Listed = false });
                zip = BuildPackage(directory, rooted, fileName: "rooted.zip");
                VerifyRejects(zip, "1.1.0", null, "zip entry '" + bad + "' outside a section must be rejected");
            }

            Assert(!File.Exists(outside), "a rejected package wrote outside the destination");
        }

        string extractDirectory = NewCaseDirectory("slip-extract");
        var files = StandardFiles();
        files.Add(new Spec("app", "../escaped.txt", "evil") { EntryName = "app/../escaped.txt" });
        string slipZip = BuildPackage(extractDirectory, files);
        UpdatePackageManifest handCrafted = new UpdatePackageManifest
        {
            Schema = 1,
            Version = "1.1.0",
            UsbipVersion = "0.9.8.0",
            HidHideVersion = "1.5.230",
            Files = files.Select(f => new UpdatePackageFile
            {
                Section = f.Section,
                Path = f.Path,
                Sha256 = Sha256Hex(f.Data),
                Size = f.Data.Length
            }).ToList()
        };
        List<string> written;
        string extractError;
        string destination = System.IO.Path.Combine(extractDirectory, "install");
        Assert(!UpdatePackage.Extract(slipZip, handCrafted, "app", destination, out written, out extractError),
            "Extract must refuse a zip-slip package");
        Assert(!File.Exists(System.IO.Path.Combine(extractDirectory, "escaped.txt")) &&
               !File.Exists(System.IO.Path.Combine(destination, "..", "escaped.txt")),
            "Extract wrote outside the destination");
    }

    private static void TestDuplicateEntriesRejected()
    {
        var files = StandardFiles();
        files.Add(new Spec("app", "Dup.txt", "one") { Listed = true });
        files.Add(new Spec("app", "dup.txt", "two") { Listed = false });
        VerifyRejects(BuildPackage(NewCaseDirectory("dup-case"), files), "1.1.0", "duplicate",
            "case-insensitive duplicate entries must be rejected");

        files = StandardFiles();
        files.Add(new Spec("app", "Dup.txt", "one"));
        files.Add(new Spec("app", "Dup.txt", "one") { Listed = false });
        VerifyRejects(BuildPackage(NewCaseDirectory("dup-exact"), files), "1.1.0", "duplicate",
            "exact duplicate entries must be rejected");

        files = StandardFiles();
        files.Add(new Spec("app", "Dup.txt", "one"));
        files.Add(new Spec("app", "dup.txt", "two") { InZip = false });
        UpdatePackageManifest manifest;
        string error;
        string zip = BuildPackage(NewCaseDirectory("dup-manifest"), files);
        Assert(!UpdatePackage.ReadManifest(zip, out manifest, out error) && manifest == null,
            "case-insensitive duplicates in the manifest must be rejected");
    }

    private static void TestSizeLimits()
    {
        var files = StandardFiles();
        files[0].ManifestSize = 100L * 1024 * 1024 + 1;
        UpdatePackageManifest manifest;
        string error;
        string zip = BuildPackage(NewCaseDirectory("entry-cap"), files);
        Assert(!UpdatePackage.ReadManifest(zip, out manifest, out error) && manifest == null,
            "an entry over 100 MB must be rejected");

        files = StandardFiles();
        files[0].ManifestSize = 100L * 1024 * 1024;
        zip = BuildPackage(NewCaseDirectory("entry-at-cap"), files);
        Assert(UpdatePackage.ReadManifest(zip, out manifest, out error),
            "an entry exactly at the cap must still parse: " + error);
        VerifyRejects(zip, "1.1.0", "size", "a declared size not matching the entry must be rejected");

        files = StandardFiles();
        foreach (var file in files) file.ManifestSize = 90L * 1024 * 1024;
        files.AddRange(Enumerable.Range(0, 3).Select(i => new Spec("app", "big" + i + ".bin", "x")
        {
            ManifestSize = 90L * 1024 * 1024
        }));
        zip = BuildPackage(NewCaseDirectory("total-cap"), files);
        Assert(!UpdatePackage.ReadManifest(zip, out manifest, out error),
            "a manifest declaring more than 400 MB in total must be rejected");

        string bigDirectory = NewCaseDirectory("zip-cap");
        string bigZip = BuildPackage(bigDirectory, StandardFiles());
        using (var stream = new FileStream(bigZip, FileMode.Append, FileAccess.Write))
        {
            stream.SetLength(200L * 1024 * 1024 + 1);
        }

        Assert(!UpdatePackage.ReadManifest(bigZip, out manifest, out error) && manifest == null,
            "a zip over 200 MB must be rejected by ReadManifest");
        VerifyRejects(bigZip, "1.1.0", "too large", "a zip over 200 MB must be rejected by Verify");

        string lieDirectory = NewCaseDirectory("lying-header");
        string lieZip = BuildPackage(lieDirectory, StandardFiles());
        Assert(Verify(lieZip, "1.1.0", out manifest, out error), "lying-header fixture rejected: " + error);

        var sparse = new List<Spec> { new Spec("app", "Zeros.bin", string.Empty) };
        sparse[0].Data = new byte[3 * 1024 * 1024];
        string zeroZip = BuildPackage(NewCaseDirectory("compressible"), sparse);
        Assert(Verify(zeroZip, "1.1.0", out manifest, out error),
            "a highly compressible file within the caps must be accepted: " + error);
    }

    private static void TestVersionRules()
    {
        string directory = NewCaseDirectory("versions");
        string zip = BuildPackage(directory, StandardFiles(), version: "1.2.0");
        VerifyRejects(zip, "1.3.0", "does not match", "manifest and release versions differ");
        VerifyRejects(zip, "v1.1.0", "does not match", "older release tag than manifest");
        VerifyRejects(zip, "garbage", "does not match", "garbage release tag");
        VerifyRejects(zip, "", "expected", "empty release tag");
        VerifyRejects(zip, null, "expected", "null release tag");

        UpdatePackageManifest manifest;
        string error;
        Assert(Verify(zip, "v1.2.0", out manifest, out error), "matching version rejected: " + error);
        Assert(Verify(zip, "1.2.0.0", out manifest, out error), "release tag with a 4th field rejected: " + error);

        string engine = System.IO.Path.Combine(directory, "ApexSenseBridge.exe");
        File.WriteAllText(engine, "installed engine");

        var installedVersion = UpdatePackage.InstalledVersionReader;
        try
        {
            UpdatePackage.InstalledVersionReader = path => new Version(1, 2, 0);
            Assert(UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "the same version as installed must be accepted: " + error);

            UpdatePackage.InstalledVersionReader = path => new Version(1, 2, 0, 5);
            Assert(UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "a revision-only difference with the same build must be accepted: " + error);

            UpdatePackage.InstalledVersionReader = path => new Version(1, 3, 0);
            Assert(!UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error) &&
                   error.IndexOf("older", StringComparison.OrdinalIgnoreCase) >= 0,
                "a downgrade must be rejected: " + error);

            UpdatePackage.InstalledVersionReader = path => new Version(1, 2, 1);
            Assert(!UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "a package one patch older than the installed engine must be rejected");

            UpdatePackage.InstalledVersionReader = path => new Version(1, 1, 9);
            Assert(UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "an upgrade from 1.1.9 must be accepted: " + error);

            UpdatePackage.InstalledVersionReader = path => new Version(1, 1);
            Assert(UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "an installed version without a build field must be treated as build 0: " + error);

            UpdatePackage.InstalledVersionReader = path => null;
            Assert(!UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "an unknown installed version must be rejected");

            UpdatePackage.InstalledVersionReader = path => { throw new InvalidOperationException("boom"); };
            Assert(!UpdatePackage.Verify(zip, "1.2.0", engine, out manifest, out error),
                "a failing version reader must be rejected");
        }
        finally
        {
            UpdatePackage.InstalledVersionReader = installedVersion;
        }

        Assert(!UpdatePackage.Verify(zip, "1.2.0", System.IO.Path.Combine(directory, "missing.exe"),
            out manifest, out error), "a missing installed engine must be rejected");
        Assert(!UpdatePackage.Verify(zip, "1.2.0", null, out manifest, out error),
            "a null installed engine must be rejected");
    }

    private static void TestSignerRules()
    {
        string directory = NewCaseDirectory("signers");
        string zip = BuildPackage(directory, StandardFiles());
        string engine = System.IO.Path.Combine(directory, "ApexSenseBridge.exe");
        File.WriteAllText(engine, "installed engine");
        UpdatePackageManifest manifest;
        string error;

        var checkedPaths = new List<string>();
        var trust = UpdatePackage.SignatureTrustCheck;
        var subject = UpdatePackage.SignerSubjectReader;
        try
        {
            UpdatePackage.SignatureTrustCheck = path =>
            {
                checkedPaths.Add(System.IO.Path.GetExtension(path).ToLowerInvariant());
                return true;
            };
            Assert(UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "valid package rejected while recording signature checks: " + error);
            Assert(checkedPaths.Count == 3 && checkedPaths.All(e => e == ".exe" || e == ".dll"),
                "exactly the exe and dll files must be Authenticode checked (got " + string.Join(",", checkedPaths) + ")");

            UpdatePackage.SignatureTrustCheck = path => System.IO.Path.GetExtension(path) != ".dll";
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) &&
                   error.IndexOf("Authenticode", StringComparison.OrdinalIgnoreCase) >= 0,
                "one unsigned dll must reject the whole package: " + error);

            UpdatePackage.SignatureTrustCheck = path => true;
            UpdatePackage.SignerSubjectReader = path =>
                System.IO.Path.GetExtension(path) == ".dll" ? "CN=Someone Else" : Publisher;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) &&
                   error.IndexOf("signer differs", StringComparison.OrdinalIgnoreCase) >= 0,
                "a different signer on one file must reject the package: " + error);

            UpdatePackage.SignerSubjectReader = path => path == engine ? Publisher : Publisher.ToLowerInvariant();
            Assert(UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "signer comparison must ignore case like the setup check: " + error);

            UpdatePackage.SignerSubjectReader = path =>
            {
                if (path == engine) throw new InvalidOperationException("The installed engine is not signed.");
                return Publisher;
            };
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) &&
                   error.IndexOf("installed", StringComparison.OrdinalIgnoreCase) >= 0,
                "an unsigned installed engine must reject the package: " + error);

            UpdatePackage.SignerSubjectReader = path => path == engine ? string.Empty : Publisher;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) &&
                   error.IndexOf("signer differs", StringComparison.OrdinalIgnoreCase) >= 0,
                "an unsigned installed engine must reject a package signed by another publisher: " + error);

            const string Bootstrap = "CN=SignPath Foundation, O=SignPath Foundation, L=Lewes, S=Delaware, C=US";
            UpdatePackage.SignerSubjectReader = path => path == engine ? string.Empty : Bootstrap;
            Assert(UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "an unsigned installed engine must accept a package signed by SignPath Foundation: " + error);

            UpdatePackage.SignerSubjectReader = path => path == engine ? string.Empty
                : System.IO.Path.GetExtension(path) == ".dll" ? "CN=Someone Else, O=SignPath Foundation" : Bootstrap;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "one file signed by another publisher must reject the package even from an unsigned install");

            UpdatePackage.SignatureTrustCheck = path => false;
            UpdatePackage.SignerSubjectReader = path => path == engine ? string.Empty : Bootstrap;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) &&
                   error.IndexOf("Authenticode", StringComparison.OrdinalIgnoreCase) >= 0,
                "an unsigned installed engine must still require trusted signatures on every file: " + error);
            UpdatePackage.SignatureTrustCheck = path => true;

            UpdatePackage.SignerSubjectReader = path => path == engine ? Publisher : Bootstrap;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "a signed installed engine must not accept a different publisher, even SignPath Foundation");

            var installedTrust = UpdatePackage.InstalledTrustCheck;
            try
            {
                const string SelfSigned = "CN=Test certificate for 'ApexSenseBridge [OSS]'";
                UpdatePackage.InstalledTrustCheck = path => false;
                UpdatePackage.SignerSubjectReader = path => path == engine ? SelfSigned : Bootstrap;
                Assert(UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                    "an engine signed with an untrusted self-signed certificate must accept SignPath Foundation: " + error);

                UpdatePackage.SignerSubjectReader = path => path == engine ? SelfSigned : Publisher;
                Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                    "an engine signed with an untrusted self-signed certificate must still reject other publishers");

                UpdatePackage.InstalledTrustCheck = path => true;
                UpdatePackage.SignerSubjectReader = path => path == engine ? SelfSigned : Bootstrap;
                Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                    "a trusted installed publisher must not be replaced by SignPath Foundation");
            }
            finally
            {
                UpdatePackage.InstalledTrustCheck = installedTrust;
            }

            UpdatePackage.SignerSubjectReader = path =>
            {
                if (path == engine) return Publisher;
                throw new InvalidOperationException("no certificate");
            };
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error),
                "an unreadable package signer must reject the package");
        }
        finally
        {
            UpdatePackage.SignatureTrustCheck = trust;
            UpdatePackage.SignerSubjectReader = subject;
        }

        var noBinaries = new List<Spec> { new Spec("app", "readme.txt", "text only") };
        string textZip = BuildPackage(NewCaseDirectory("no-binaries"), noBinaries);
        Assert(Verify(textZip, "1.1.0", out manifest, out error),
            "a package without exe or dll needs no signature check: " + error);

        var upper = new List<Spec> { new Spec("app", "Tool.EXE", "x") };
        var uppercaseChecks = 0;
        UpdatePackage.SignatureTrustCheck = path => { uppercaseChecks++; return true; };
        try
        {
            string upperZip = BuildPackage(NewCaseDirectory("upper-exe"), upper);
            Assert(Verify(upperZip, "1.1.0", out manifest, out error) && uppercaseChecks == 1,
                "an upper-case .EXE extension must still be signature checked");
        }
        finally
        {
            UpdatePackage.SignatureTrustCheck = trust;
        }
    }

    private static void TestManifestStrictness()
    {
        var files = StandardFiles();
        string good = ManifestJson("1.1.0", false, "0.9.8.0", "1.5.230", files);

        Action<string, string> reject = (name, json) =>
        {
            string zip = BuildPackage(NewCaseDirectory("manifest-" + name), files, manifestJson: json);
            UpdatePackageManifest manifest;
            string error;
            Assert(!UpdatePackage.ReadManifest(zip, out manifest, out error) && manifest == null &&
                   !string.IsNullOrEmpty(error), "manifest variant '" + name + "' must be rejected");
            VerifyRejects(zip, "1.1.0", null, "package with manifest variant '" + name + "' must be rejected");
        };

        reject("schema2", ManifestJson("1.1.0", false, "0.9.8.0", "1.5.230", files, "2"));
        reject("schema-string", ManifestJson("1.1.0", false, "0.9.8.0", "1.5.230", files, "\"1\""));
        reject("schema-missing", good.Replace("\"schema\":1,", string.Empty));
        reject("version-two-fields", ManifestJson("1.1", false, "0.9.8.0", "1.5.230", files));
        reject("version-garbage", ManifestJson("one.two", false, "0.9.8.0", "1.5.230", files));
        reject("drivers-missing", good.Replace("\"driversChanged\":false,", string.Empty));
        reject("drivers-string", good.Replace("\"driversChanged\":false", "\"driversChanged\":\"false\""));
        reject("files-missing", "{\"schema\":1,\"version\":\"1.1.0\",\"driversChanged\":false}");
        reject("files-empty", "{\"schema\":1,\"version\":\"1.1.0\",\"driversChanged\":false,\"files\":[]}");
        reject("files-not-array", "{\"schema\":1,\"version\":\"1.1.0\",\"driversChanged\":false,\"files\":{}}");
        reject("not-json", "this is not json");
        reject("json-array", "[]");
        reject("size-negative", good.Replace("\"size\":14}", "\"size\":-1}"));
        reject("size-fraction", good.Replace("\"size\":14}", "\"size\":14.5}"));
        reject("size-string", good.Replace("\"size\":14}", "\"size\":\"14\"}"));
        reject("section-case", good.Replace("\"section\":\"app\"", "\"section\":\"App\""));

        string bom = "\uFEFF" + good;
        string bomZip = BuildPackage(NewCaseDirectory("manifest-bom"), files, manifestJson: bom);
        UpdatePackageManifest parsed;
        string error2;
        Assert(UpdatePackage.ReadManifest(bomZip, out parsed, out error2),
            "a manifest with a BOM should still parse: " + error2);
    }

    private static void TestDriversMatch()
    {
        var registry = UpdatePackage.InstalledRegistryReader;
        try
        {
            var manifest = new UpdatePackageManifest
            {
                Schema = 1,
                Version = "1.1.0",
                UsbipVersion = "0.9.8.0",
                HidHideVersion = "1.5.230",
                Files = new List<UpdatePackageFile>()
            };
            string reason;

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0", "1.5.230");
            Assert(UpdatePackage.DriversMatchInstalled(manifest, out reason) && reason == null,
                "identical driver versions must match");

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0 ", "1.5.230");
            Assert(UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "padding around installed versions must be tolerated");

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.7.0", "1.5.230");
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason) &&
                   reason.IndexOf("USBIP", StringComparison.OrdinalIgnoreCase) >= 0,
                "a different usbip version must not match: " + reason);

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0", "1.5.229");
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason) &&
                   reason.IndexOf("HidHide", StringComparison.OrdinalIgnoreCase) >= 0,
                "a different HidHide version must not match: " + reason);

            UpdatePackage.InstalledRegistryReader = DriverRegistry(null, "1.5.230");
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "a missing installed usbip value must not match");

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0", "");
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "an empty installed HidHide value must not match");

            UpdatePackage.InstalledRegistryReader = name => { throw new InvalidOperationException("denied"); };
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "an unreadable registry must not match");

            UpdatePackage.InstalledRegistryReader = DriverRegistry("0.9.8.0", "1.5.230");
            manifest.DriversChanged = true;
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason) && !string.IsNullOrEmpty(reason),
                "driversChanged must never match");
            manifest.DriversChanged = false;

            manifest.UsbipVersion = null;
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "a manifest without a usbip version must not match");
            manifest.UsbipVersion = "0.9.8.0";
            manifest.HidHideVersion = " ";
            Assert(!UpdatePackage.DriversMatchInstalled(manifest, out reason),
                "a manifest without a HidHide version must not match");
            Assert(!UpdatePackage.DriversMatchInstalled(null, out reason) && !string.IsNullOrEmpty(reason),
                "a null manifest must not match");
        }
        finally
        {
            UpdatePackage.InstalledRegistryReader = registry;
        }
    }

    private static void TestExtractRechecksPackage()
    {
        string directory = NewCaseDirectory("recheck");
        string zip = BuildPackage(directory, StandardFiles());
        UpdatePackageManifest manifest;
        string error;
        Assert(Verify(zip, "1.1.0", out manifest, out error), "recheck fixture rejected: " + error);

        var swapped = StandardFiles();
        swapped[0].Data = Encoding.ASCII.GetBytes("tray exe 6.6.6");
        swapped[0].ManifestSha = null;
        string swappedZip = BuildPackage(directory, swapped, fileName: "swapped.zip");
        string install = System.IO.Path.Combine(directory, "install");
        List<string> written;
        Assert(!UpdatePackage.Extract(swappedZip, manifest, "app", install, out written, out error),
            "a package swapped after verification must be refused by Extract");
        Assert(!Directory.Exists(install) || Directory.GetFiles(install, "*", SearchOption.AllDirectories).Length == 0,
            "Extract of a swapped package must not leave files behind");

        var corrupted = StandardFiles();
        corrupted[1].ManifestSha = null;
        string corruptedZip = BuildPackage(directory, corrupted, fileName: "same-manifest.zip");
        var tampered = new UpdatePackageManifest
        {
            Schema = manifest.Schema,
            Version = manifest.Version,
            DriversChanged = manifest.DriversChanged,
            UsbipVersion = manifest.UsbipVersion,
            HidHideVersion = manifest.HidHideVersion,
            Files = manifest.Files.Select(f => new UpdatePackageFile
            {
                Section = f.Section,
                Path = f.Path,
                Sha256 = f.Sha256,
                Size = f.Size
            }).ToList()
        };
        tampered.Files[0].Sha256 = new string('0', 64);
        Assert(!UpdatePackage.Extract(corruptedZip, tampered, "app", install, out written, out error),
            "a caller-supplied manifest differing from the zip must be refused");

        var tamperedAfterVerify = StandardFiles();
        tamperedAfterVerify[0].Data = Encoding.ASCII.GetBytes("tray exe 1.1.X");
        tamperedAfterVerify[0].ManifestSha = Sha256Hex(StandardFiles()[0].Data);
        tamperedAfterVerify[0].ManifestSize = StandardFiles()[0].Data.Length;
        string badZip = BuildPackage(directory, tamperedAfterVerify, fileName: "bad.zip");
        UpdatePackageManifest badManifest;
        Assert(UpdatePackage.ReadManifest(badZip, out badManifest, out error),
            "fixture manifest should parse");
        string badInstall = System.IO.Path.Combine(directory, "bad-install");
        Assert(!UpdatePackage.Extract(badZip, badManifest, "app", badInstall, out written, out error) &&
               error.IndexOf("hash", StringComparison.OrdinalIgnoreCase) >= 0,
            "Extract must recheck sha256 while writing: " + error);
        Assert(!File.Exists(System.IO.Path.Combine(badInstall, "ApexSenseBridgeTray.exe")),
            "a file failing the sha256 recheck must not be moved into place");
        Assert(Directory.GetFiles(badInstall, "*.tmp", SearchOption.AllDirectories).Length == 0,
            "a file failing the sha256 recheck left a temporary file");
    }

    private static void TestRealAuthenticodeRejectsUnsignedPackage()
    {
        string directory = NewCaseDirectory("real-authenticode");
        string unsignedDll = System.IO.Path.Combine(directory, "unsigned.dll");
        File.WriteAllBytes(unsignedDll, new byte[] { 0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00 });
        Assert(!ReleaseSecurity.HasTrustedAuthenticodeSignature(unsignedDll),
            "WinVerifyTrust must reject an unsigned file");
        string unsignedExe = System.IO.Path.Combine(directory, "unsigned.exe");
        File.Copy(System.Reflection.Assembly.GetExecutingAssembly().Location, unsignedExe);
        Assert(!ReleaseSecurity.HasTrustedAuthenticodeSignature(unsignedExe),
            "WinVerifyTrust must reject the unsigned test executable");

        var trust = UpdatePackage.SignatureTrustCheck;
        try
        {
            UpdatePackage.SignatureTrustCheck = ReleaseSecurity.HasTrustedAuthenticodeSignature;
            string zip = BuildPackage(directory, StandardFiles());
            VerifyRejects(zip, "1.1.0", "Authenticode",
                "the real signature check must reject an unsigned package");
            Assert(Directory.GetDirectories(directory, ".verify-*").Length == 0,
                "a rejected package left its temporary verification directory behind");

            string install = System.IO.Path.Combine(directory, "install");
            Assert(!Directory.Exists(install), "a rejected package must not have created the install directory");
        }
        finally
        {
            UpdatePackage.SignatureTrustCheck = trust;
        }
    }

    private static void TestRealDefaultsRejectUnsignedInstalledEngine()
    {
        string directory = NewCaseDirectory("real-defaults");
        string zip = BuildPackage(directory, StandardFiles());
        string engine = System.Reflection.Assembly.GetExecutingAssembly().Location;

        var trust = UpdatePackage.SignatureTrustCheck;
        var subject = UpdatePackage.SignerSubjectReader;
        var version = UpdatePackage.InstalledVersionReader;
        try
        {
            UpdatePackage.SignatureTrustCheck = ReleaseSecurity.HasTrustedAuthenticodeSignature;
            UpdatePackage.SignerSubjectReader = ReleaseSecurity.ReadSignerSubject;
            UpdatePackage.InstalledVersionReader = path => new Version(1, 0, 0);
            UpdatePackageManifest manifest;
            string error;
            Assert(!UpdatePackage.Verify(zip, "1.1.0", engine, out manifest, out error) && manifest == null &&
                   error.IndexOf("Authenticode", StringComparison.OrdinalIgnoreCase) >= 0,
                "an unsigned package must be rejected by the real checks even when the installed engine is unsigned: " + error);
        }
        finally
        {
            UpdatePackage.SignatureTrustCheck = trust;
            UpdatePackage.SignerSubjectReader = subject;
            UpdatePackage.InstalledVersionReader = version;
        }
    }

    private static void Assert(bool condition, string message)
    {
        assertions++;
        if (!condition) throw new InvalidOperationException("UpdatePackage: " + message);
    }
}
