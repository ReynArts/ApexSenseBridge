using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace ApexSenseBridge.Security
{
    public sealed class UpdatePackageFile
    {
        public string Section;
        public string Path;
        public string Sha256;
        public long Size;
    }

    public sealed class UpdatePackageManifest
    {
        public int Schema;
        public string Version;
        public bool DriversChanged;
        public string UsbipVersion;
        public string HidHideVersion;
        public List<UpdatePackageFile> Files;
    }

    public static class UpdatePackage
    {
        public const string AssetName = "ApexSenseBridge-Update.zip";
        public const string SectionApp = "app";
        public const string SectionPlaynite = "playnite";

        private const string ManifestEntryName = "manifest.json";
        private const string RegistrySubKey = @"SOFTWARE\ApexSenseBridge";
        private const long MaxPackageBytes = 200L * 1024 * 1024;
        private const long MaxEntryBytes = 100L * 1024 * 1024;
        private const long MaxTotalBytes = 400L * 1024 * 1024;
        private const long MaxManifestBytes = 1024 * 1024;
        private const int MaxFileCount = 2000;
        private const int MaxPathLength = 240;

        private static readonly string[] SignedExtensions =
        {
            ".exe", ".dll", ".sys", ".ocx", ".scr", ".cpl", ".com", ".msi"
        };

        private static readonly string[] ReservedNames =
        {
            "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        internal static Func<string, bool> SignatureTrustCheck =
            ReleaseSecurity.HasTrustedAuthenticodeSignature;

        internal static Func<string, string> SignerSubjectReader =
            ReleaseSecurity.ReadSignerSubject;

        internal static Func<string, bool> InstalledTrustCheck =
            ReleaseSecurity.HasTrustedAuthenticodeSignature;

        internal static Func<string, Version> InstalledVersionReader =
            ReadInstalledVersion;

        internal static Func<string, string> InstalledRegistryReader =
            ReadInstalledRegistryValue;

        public static bool IsExpectedUpdateAsset(string name, string downloadUrl)
        {
            return string.Equals(name, AssetName, StringComparison.OrdinalIgnoreCase) &&
                   ReleaseSecurity.IsGithubReleaseDownloadUrl(downloadUrl);
        }

        public static bool ReadManifest(string zipPath,
            out UpdatePackageManifest manifest, out string error)
        {
            manifest = null;
            error = null;
            try
            {
                if (!CheckPackageFile(zipPath, out error))
                {
                    return false;
                }

                using (var stream = OpenPackage(zipPath))
                using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    return ReadManifest(archive, out manifest, out error);
                }
            }
            catch (Exception ex)
            {
                manifest = null;
                error = "The update package could not be read: " + ex.Message;
                return false;
            }
        }

        public static bool Verify(string zipPath, string expectedVersion,
            string installedEnginePath, out UpdatePackageManifest manifest,
            out string error)
        {
            manifest = null;
            error = null;
            string tempRoot = null;
            try
            {
                if (string.IsNullOrWhiteSpace(expectedVersion))
                {
                    error = "The expected update version is missing.";
                    return false;
                }

                UpdatePackageManifest parsed;
                if (!ReadManifest(zipPath, out parsed, out error))
                {
                    return false;
                }

                if (!ReleaseSecurity.MatchesReleaseVersion(parsed.Version, expectedVersion))
                {
                    error = "The update package version does not match the selected GitHub release.";
                    return false;
                }

                if (!IsNotOlderThanInstalled(parsed.Version, installedEnginePath, out error))
                {
                    return false;
                }

                string installedSubject;
                if (!TryReadInstalledSubject(installedEnginePath, out installedSubject,
                    out error))
                {
                    return false;
                }

                using (var stream = OpenPackage(zipPath))
                using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    Dictionary<string, ZipArchiveEntry> entries;
                    if (!IndexEntries(archive, parsed, out entries, out error))
                    {
                        return false;
                    }

                    tempRoot = System.IO.Path.Combine(
                        System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(zipPath)),
                        ".verify-" + Guid.NewGuid().ToString("N"));
                    Directory.CreateDirectory(tempRoot);

                    int index = 0;
                    foreach (var file in parsed.Files)
                    {
                        string name = EntryName(file);
                        string signedCopy = null;
                        if (IsSignedType(file.Path))
                        {
                            signedCopy = System.IO.Path.Combine(tempRoot,
                                (index++).ToString() + System.IO.Path.GetExtension(file.Path));
                        }

                        if (!CopyAndCheck(entries[name], file, signedCopy, out error))
                        {
                            return false;
                        }

                        if (signedCopy == null)
                        {
                            continue;
                        }

                        if (!CheckSignature(signedCopy, name, ref installedSubject,
                            out error))
                        {
                            return false;
                        }
                    }
                }

                manifest = parsed;
                return true;
            }
            catch (Exception ex)
            {
                manifest = null;
                error = "The update package could not be verified: " + ex.Message;
                return false;
            }
            finally
            {
                DeleteQuietly(tempRoot);
            }
        }

        public static bool DriversMatchInstalled(UpdatePackageManifest manifest,
            out string reason)
        {
            reason = null;
            if (manifest == null)
            {
                reason = "The update package manifest is missing.";
                return false;
            }

            if (manifest.DriversChanged)
            {
                reason = "The update changes the drivers and needs the full setup.";
                return false;
            }

            return DriverVersionMatches("USBIP", manifest.UsbipVersion, "UsbipVersion",
                       out reason) &&
                   DriverVersionMatches("HidHide", manifest.HidHideVersion,
                       "HidHideVersion", out reason);
        }

        public static bool Extract(string zipPath, UpdatePackageManifest manifest,
            string section, string destinationDirectory,
            out List<string> writtenRelativePaths, out string error)
        {
            writtenRelativePaths = new List<string>();
            error = null;
            string tempPath = null;
            try
            {
                if (manifest == null || manifest.Files == null)
                {
                    error = "The update package manifest is missing.";
                    return false;
                }

                if (section != SectionApp && section != SectionPlaynite)
                {
                    error = "Unknown update package section.";
                    return false;
                }

                if (string.IsNullOrWhiteSpace(destinationDirectory))
                {
                    error = "The destination directory is missing.";
                    return false;
                }

                UpdatePackageManifest fromZip;
                if (!ReadManifest(zipPath, out fromZip, out error))
                {
                    return false;
                }

                if (!SameManifest(manifest, fromZip))
                {
                    error = "The manifest does not match the update package.";
                    return false;
                }

                string root = System.IO.Path.GetFullPath(destinationDirectory)
                    .TrimEnd('\\') + "\\";
                Directory.CreateDirectory(root);

                using (var stream = OpenPackage(zipPath))
                using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    Dictionary<string, ZipArchiveEntry> entries;
                    if (!IndexEntries(archive, fromZip, out entries, out error))
                    {
                        return false;
                    }

                    foreach (var file in fromZip.Files)
                    {
                        if (file.Section != section)
                        {
                            continue;
                        }

                        string relative = file.Path.Replace('/', '\\');
                        string destination = System.IO.Path.GetFullPath(
                            System.IO.Path.Combine(root, relative));
                        if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        {
                            error = "Unsafe destination path: " + file.Path;
                            return false;
                        }

                        if (!PathHasNoReparsePoint(root, relative, out error))
                        {
                            return false;
                        }

                        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(destination));
                        tempPath = destination + "." +
                                   Guid.NewGuid().ToString("N").Substring(0, 8) + ".tmp";
                        if (!CopyAndCheck(entries[EntryName(file)], file, tempPath,
                            out error))
                        {
                            return false;
                        }

                        if (File.Exists(destination))
                        {
                            File.Replace(tempPath, destination, null);
                        }
                        else
                        {
                            File.Move(tempPath, destination);
                        }

                        tempPath = null;
                        writtenRelativePaths.Add(relative);
                    }
                }

                return true;
            }
            catch (Exception ex)
            {
                error = "The update package could not be extracted: " + ex.Message;
                return false;
            }
            finally
            {
                if (tempPath != null)
                {
                    try { File.Delete(tempPath); } catch { }
                }
            }
        }

        internal static bool IsSafeRelativePath(string path, out string error)
        {
            error = null;
            if (string.IsNullOrEmpty(path) || path.Length > MaxPathLength)
            {
                error = "Invalid package path.";
                return false;
            }

            if (path.IndexOf('\\') >= 0 || path[0] == '/' ||
                path[path.Length - 1] == '/')
            {
                error = "Unsafe package path: " + path;
                return false;
            }

            foreach (var segment in path.Split('/'))
            {
                if (!IsSafeSegment(segment))
                {
                    error = "Unsafe package path: " + path;
                    return false;
                }
            }

            return true;
        }

        private static bool IsSafeSegment(string segment)
        {
            if (segment.Length == 0 || segment == "." || segment == ".." ||
                segment[segment.Length - 1] == ' ' ||
                segment[segment.Length - 1] == '.')
            {
                return false;
            }

            foreach (char c in segment)
            {
                if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
                    c == '|' || c == '?' || c == '*')
                {
                    return false;
                }
            }

            int dot = segment.IndexOf('.');
            string stem = (dot < 0 ? segment : segment.Substring(0, dot))
                .TrimEnd(' ').ToUpperInvariant();
            return Array.IndexOf(ReservedNames, stem) < 0;
        }

        private static bool IsSignedType(string path)
        {
            string extension = System.IO.Path.GetExtension(path);
            foreach (var signed in SignedExtensions)
            {
                if (string.Equals(extension, signed, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }

            return false;
        }

        private static string EntryName(UpdatePackageFile file)
        {
            return file.Section + "/" + file.Path;
        }

        private static FileStream OpenPackage(string zipPath)
        {
            return new FileStream(zipPath, FileMode.Open, FileAccess.Read,
                FileShare.Read);
        }

        private static bool CheckPackageFile(string zipPath, out string error)
        {
            error = null;
            if (string.IsNullOrWhiteSpace(zipPath) || !File.Exists(zipPath))
            {
                error = "The update package is missing.";
                return false;
            }

            if (new FileInfo(zipPath).Length > MaxPackageBytes)
            {
                error = "The update package is too large.";
                return false;
            }

            return true;
        }

        private static bool ReadManifest(ZipArchive archive,
            out UpdatePackageManifest manifest, out string error)
        {
            manifest = null;
            error = null;
            ZipArchiveEntry entry = null;
            int count = 0;
            foreach (var candidate in archive.Entries)
            {
                if (string.Equals(candidate.FullName, ManifestEntryName,
                    StringComparison.OrdinalIgnoreCase))
                {
                    entry = candidate;
                    count++;
                }
            }

            if (count != 1 || entry.FullName != ManifestEntryName)
            {
                error = "The update package must contain exactly one manifest.json.";
                return false;
            }

            if (entry.Length > MaxManifestBytes)
            {
                error = "The update package manifest is too large.";
                return false;
            }

            byte[] bytes;
            using (var input = entry.Open())
            using (var memory = new MemoryStream())
            {
                var buffer = new byte[8192];
                int read;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    memory.Write(buffer, 0, read);
                    if (memory.Length > MaxManifestBytes)
                    {
                        error = "The update package manifest is too large.";
                        return false;
                    }
                }

                bytes = memory.ToArray();
            }

            string text = new UTF8Encoding(false, true).GetString(bytes)
                .TrimStart('﻿');
            return ParseManifest(text, out manifest, out error);
        }

        private static bool ParseManifest(string text,
            out UpdatePackageManifest manifest, out string error)
        {
            manifest = null;
            error = "The update package manifest is invalid.";
            var serializer = new JavaScriptSerializer
            {
                MaxJsonLength = (int)MaxManifestBytes,
                RecursionLimit = 16
            };
            var root = serializer.DeserializeObject(text) as Dictionary<string, object>;
            if (root == null)
            {
                return false;
            }

            long schema;
            if (!TryGetLong(root, "schema", out schema) || schema != 1)
            {
                error = "Unsupported update package schema.";
                return false;
            }

            object driversChanged;
            string version = GetString(root, "version");
            Version parsedVersion;
            if (!root.TryGetValue("driversChanged", out driversChanged) ||
                !(driversChanged is bool) ||
                !Version.TryParse(version ?? string.Empty, out parsedVersion) ||
                parsedVersion.Build < 0)
            {
                return false;
            }

            var list = GetValue(root, "files") as IList;
            if (list == null || list.Count == 0 || list.Count > MaxFileCount)
            {
                return false;
            }

            var result = new UpdatePackageManifest
            {
                Schema = (int)schema,
                Version = version,
                DriversChanged = (bool)driversChanged,
                UsbipVersion = GetString(root, "usbipVersion"),
                HidHideVersion = GetString(root, "hidHideVersion"),
                Files = new List<UpdatePackageFile>()
            };

            var seen = new HashSet<string>();
            long total = 0;
            foreach (var item in list)
            {
                var map = item as Dictionary<string, object>;
                if (map == null)
                {
                    return false;
                }

                var file = new UpdatePackageFile
                {
                    Section = GetString(map, "section"),
                    Path = GetString(map, "path"),
                    Sha256 = GetString(map, "sha256")
                };
                long size;
                if (!TryGetLong(map, "size", out size) || size < 0 ||
                    size > MaxEntryBytes)
                {
                    error = "Invalid file size in the update package manifest.";
                    return false;
                }

                file.Size = size;
                if (file.Section != SectionApp && file.Section != SectionPlaynite)
                {
                    error = "Unknown section in the update package manifest.";
                    return false;
                }

                string pathError;
                if (!IsSafeRelativePath(file.Path, out pathError))
                {
                    error = pathError;
                    return false;
                }

                if (!IsLowerHex64(file.Sha256))
                {
                    error = "Invalid sha256 in the update package manifest.";
                    return false;
                }

                if (!seen.Add(EntryName(file).ToUpperInvariant()))
                {
                    error = "Duplicate file in the update package manifest.";
                    return false;
                }

                total += size;
                if (total > MaxTotalBytes)
                {
                    error = "The update package content is too large.";
                    return false;
                }

                result.Files.Add(file);
            }

            manifest = result;
            error = null;
            return true;
        }

        private static object GetValue(Dictionary<string, object> map, string key)
        {
            object value;
            return map.TryGetValue(key, out value) ? value : null;
        }

        private static string GetString(Dictionary<string, object> map, string key)
        {
            return GetValue(map, key) as string;
        }

        private static bool TryGetLong(Dictionary<string, object> map, string key,
            out long value)
        {
            value = 0;
            object raw = GetValue(map, key);
            if (raw is int)
            {
                value = (int)raw;
                return true;
            }

            if (raw is long)
            {
                value = (long)raw;
                return true;
            }

            return false;
        }

        private static bool IsLowerHex64(string value)
        {
            if (value == null || value.Length != 64)
            {
                return false;
            }

            foreach (char c in value)
            {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
            }

            return true;
        }

        private static bool IndexEntries(ZipArchive archive,
            UpdatePackageManifest manifest,
            out Dictionary<string, ZipArchiveEntry> entries, out string error)
        {
            error = null;
            entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.Ordinal);
            var listed = new HashSet<string>(StringComparer.Ordinal);
            foreach (var file in manifest.Files)
            {
                listed.Add(EntryName(file));
            }

            var seen = new HashSet<string>();
            foreach (var entry in archive.Entries)
            {
                string name = entry.FullName;
                if (!seen.Add(name.ToUpperInvariant()))
                {
                    error = "The update package contains a duplicate entry: " + name;
                    return false;
                }

                if (name == ManifestEntryName)
                {
                    continue;
                }

                string pathError;
                if (!IsSafeRelativePath(name, out pathError))
                {
                    error = pathError;
                    return false;
                }

                if (!listed.Contains(name))
                {
                    error = "The update package contains an unlisted entry: " + name;
                    return false;
                }

                entries[name] = entry;
            }

            foreach (var name in listed)
            {
                if (!entries.ContainsKey(name))
                {
                    error = "The update package is missing a listed file: " + name;
                    return false;
                }
            }

            return true;
        }

        private static bool CopyAndCheck(ZipArchiveEntry entry, UpdatePackageFile file,
            string copyPath, out string error)
        {
            error = null;
            if (entry.Length != file.Size || entry.Length > MaxEntryBytes)
            {
                error = "File size differs from the manifest: " + EntryName(file);
                return false;
            }

            long total = 0;
            string hash = null;
            using (var sha = SHA256.Create())
            using (var input = entry.Open())
            using (var output = copyPath == null
                ? null
                : new FileStream(copyPath, FileMode.CreateNew, FileAccess.Write,
                    FileShare.None))
            {
                var buffer = new byte[81920];
                int read;
                while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    total += read;
                    if (total > file.Size)
                    {
                        break;
                    }

                    sha.TransformBlock(buffer, 0, read, null, 0);
                    if (output != null)
                    {
                        output.Write(buffer, 0, read);
                    }
                }

                if (total <= file.Size)
                {
                    sha.TransformFinalBlock(buffer, 0, 0);
                    hash = ToHex(sha.Hash);
                }
            }

            if (total != file.Size || hash != file.Sha256)
            {
                if (copyPath != null)
                {
                    try { File.Delete(copyPath); } catch { }
                }

                error = total != file.Size
                    ? "File size differs from the manifest: " + EntryName(file)
                    : "File hash differs from the manifest: " + EntryName(file);
                return false;
            }

            return true;
        }

        private static string ToHex(byte[] bytes)
        {
            var builder = new StringBuilder(bytes.Length * 2);
            foreach (byte b in bytes)
            {
                builder.Append(b.ToString("x2"));
            }

            return builder.ToString();
        }

        private static bool CheckSignature(string path, string name,
            ref string expectedSubject, out string error)
        {
            error = null;
            if (!SignatureTrustCheck(path))
            {
                error = "The Authenticode signature is missing or untrusted: " + name;
                return false;
            }

            string subject;
            try
            {
                subject = SignerSubjectReader(path);
            }
            catch (Exception ex)
            {
                error = "The signer could not be read for " + name + ": " + ex.Message;
                return false;
            }

            if (!ReleaseSecurity.IsAcceptedPublisher(subject, expectedSubject))
            {
                error = "The signer differs from the installed ApexSenseBridge publisher: " + name;
                return false;
            }

            if (string.IsNullOrEmpty(expectedSubject))
            {
                expectedSubject = subject;
            }

            return true;
        }

        private static bool TryReadInstalledSubject(string installedEnginePath,
            out string subject, out string error)
        {
            subject = null;
            error = null;
            if (string.IsNullOrWhiteSpace(installedEnginePath) ||
                !File.Exists(installedEnginePath))
            {
                error = "The installed engine could not be identified.";
                return false;
            }

            try
            {
                subject = SignerSubjectReader(installedEnginePath);
            }
            catch (Exception ex)
            {
                error = "The installed ApexSenseBridge publisher could not be verified: " +
                        ex.Message;
                return false;
            }

            if (subject == null)
            {
                error = "The installed ApexSenseBridge publisher could not be verified.";
                return false;
            }

            if (subject.Length > 0 && !InstalledTrustCheck(installedEnginePath))
            {
                subject = string.Empty;
            }

            return true;
        }

        private static bool IsNotOlderThanInstalled(string packageVersion,
            string installedEnginePath, out string error)
        {
            error = null;
            Version installed = null;
            try
            {
                installed = InstalledVersionReader(installedEnginePath);
            }
            catch (Exception)
            {
            }

            Version package;
            if (installed == null || !Version.TryParse(packageVersion, out package))
            {
                error = "The installed version could not be determined.";
                return false;
            }

            var left = new Version(package.Major, package.Minor, package.Build);
            var right = new Version(installed.Major, installed.Minor,
                Math.Max(installed.Build, 0));
            if (left.CompareTo(right) < 0)
            {
                error = "The update package is older than the installed version.";
                return false;
            }

            return true;
        }

        private static Version ReadInstalledVersion(string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                return null;
            }

            var info = FileVersionInfo.GetVersionInfo(path);
            string product = ReleaseSecurity.NormalizeVersionField(info.ProductVersion);
            int suffix = product.IndexOfAny(new[] { '+', '-', ' ' });
            Version parsed;
            if (Version.TryParse(suffix < 0 ? product : product.Substring(0, suffix),
                    out parsed) && parsed.Build >= 0)
            {
                return parsed;
            }

            if (info.FileMajorPart == 0 && info.FileMinorPart == 0 &&
                info.FileBuildPart == 0)
            {
                return null;
            }

            return new Version(info.FileMajorPart, info.FileMinorPart,
                info.FileBuildPart);
        }

        private static bool DriverVersionMatches(string label, string packageValue,
            string valueName, out string reason)
        {
            reason = null;
            string installed = null;
            try
            {
                installed = InstalledRegistryReader(valueName);
            }
            catch (Exception)
            {
            }

            if (string.IsNullOrWhiteSpace(packageValue) ||
                string.IsNullOrWhiteSpace(installed))
            {
                reason = "The installed " + label + " version could not be compared.";
                return false;
            }

            if (!string.Equals(packageValue.Trim(), installed.Trim(),
                StringComparison.OrdinalIgnoreCase))
            {
                reason = "The " + label + " version differs from the installed one.";
                return false;
            }

            return true;
        }

        private static string ReadInstalledRegistryValue(string valueName)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,
                RegistryView.Registry64))
            using (var key = baseKey.OpenSubKey(RegistrySubKey))
            {
                object value = key == null ? null : key.GetValue(valueName);
                return value as string;
            }
        }

        private static bool SameManifest(UpdatePackageManifest left,
            UpdatePackageManifest right)
        {
            if (left.Schema != right.Schema || left.Version != right.Version ||
                left.DriversChanged != right.DriversChanged ||
                left.UsbipVersion != right.UsbipVersion ||
                left.HidHideVersion != right.HidHideVersion ||
                left.Files.Count != right.Files.Count)
            {
                return false;
            }

            for (int i = 0; i < left.Files.Count; i++)
            {
                var a = left.Files[i];
                var b = right.Files[i];
                if (a.Section != b.Section || a.Path != b.Path ||
                    a.Sha256 != b.Sha256 || a.Size != b.Size)
                {
                    return false;
                }
            }

            return true;
        }

        private static bool PathHasNoReparsePoint(string root, string relative,
            out string error)
        {
            error = null;
            string current = root;
            foreach (var segment in relative.Split('\\'))
            {
                current = System.IO.Path.Combine(current, segment);
                if ((Directory.Exists(current) || File.Exists(current)) &&
                    (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                {
                    error = "The destination contains a link: " + relative;
                    return false;
                }
            }

            return true;
        }

        private static void DeleteQuietly(string directory)
        {
            if (directory == null)
            {
                return;
            }

            try
            {
                if (Directory.Exists(directory))
                {
                    Directory.Delete(directory, true);
                }
            }
            catch
            {
            }
        }
    }
}
