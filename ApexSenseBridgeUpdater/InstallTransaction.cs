using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using ApexSenseBridge.Security;

namespace ApexSenseBridgeUpdater
{
    internal sealed class InstallTransaction
    {
        private const string BackupFolderName = ".update-backup";
        private const string SectionApp = "app";

        private sealed class Entry
        {
            public string Relative;
            public long Size;
            public string Target;
            public bool Existed;
            public bool IsOwnExecutable;
            public string Saved;
            public bool Moved;
        }

        private readonly string installDirectory;
        private readonly string ownExecutable;
        private readonly List<Entry> entries = new List<Entry>();
        private readonly List<string> missingDirectories = new List<string>();
        private string backupRoot;

        public InstallTransaction(string installDirectory, string ownExecutable)
        {
            this.installDirectory = installDirectory.TrimEnd('\\');
            this.ownExecutable = ownExecutable;
        }

        public int FileCount
        {
            get { return entries.Count; }
        }

        public static void CleanupLeftovers(string installDirectory, string ownExecutable)
        {
            try
            {
                var leftover = ownExecutable + ".old";
                if (File.Exists(leftover) && !SafePaths.IsReparsePoint(leftover)) File.Delete(leftover);
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("Leftover cleanup skipped: " + ex.Message);
            }

            try
            {
                var root = Path.Combine(installDirectory, BackupFolderName);
                if (!Directory.Exists(root) || SafePaths.IsReparsePoint(root)) return;
                foreach (var directory in Directory.GetDirectories(root))
                {
                    if (SafePaths.IsReparsePoint(directory)) continue;
                    if (DateTime.UtcNow - Directory.GetCreationTimeUtc(directory) < TimeSpan.FromDays(7)) continue;
                    Directory.Delete(directory, true);
                }
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("Old backup cleanup skipped: " + ex.Message);
            }
        }

        public bool Prepare(UpdatePackageManifest manifest, out string error)
        {
            error = null;
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var missing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in manifest.Files)
            {
                if (!string.Equals(file.Section, SectionApp, StringComparison.Ordinal)) continue;

                string target;
                if (!SafePaths.TryResolveRelative(installDirectory, file.Path, out target) || !seen.Add(target))
                {
                    error = "The package lists a path that cannot be installed safely.";
                    return false;
                }

                string problem;
                if (!SafePaths.ChainIsPlain(installDirectory, target, out problem))
                {
                    error = problem;
                    return false;
                }

                if (Directory.Exists(target))
                {
                    error = "A directory is in the way of " + file.Path + ".";
                    return false;
                }

                var parent = Path.GetDirectoryName(target);
                while (parent != null && SafePaths.IsInside(installDirectory, parent) && !Directory.Exists(parent))
                {
                    if (missing.Add(parent)) missingDirectories.Add(parent);
                    parent = Path.GetDirectoryName(parent);
                }

                entries.Add(new Entry
                {
                    Relative = file.Path.Replace('/', '\\'),
                    Size = file.Size,
                    Target = target,
                    Existed = File.Exists(target),
                    IsOwnExecutable = string.Equals(target, ownExecutable, StringComparison.OrdinalIgnoreCase)
                });
            }

            if (entries.Count == 0)
            {
                error = "The package has no application files.";
                return false;
            }
            return true;
        }

        public bool Apply(string packagePath, UpdatePackageManifest manifest, out string error)
        {
            error = null;
            try
            {
                var backupBase = Path.Combine(installDirectory, BackupFolderName);
                if (Directory.Exists(backupBase) && SafePaths.IsReparsePoint(backupBase))
                {
                    error = "The update backup folder is a link.";
                    return false;
                }
                backupRoot = Path.Combine(backupBase, Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(backupRoot);

                foreach (var entry in entries)
                {
                    if (!entry.Existed) continue;
                    if (entry.IsOwnExecutable)
                    {
                        entry.Saved = entry.Target + ".old";
                        if (File.Exists(entry.Saved))
                        {
                            if (SafePaths.IsReparsePoint(entry.Saved)) throw new IOException("A link is in the way of the updater backup name.");
                            File.Delete(entry.Saved);
                        }
                    }
                    else
                    {
                        entry.Saved = Path.Combine(backupRoot, entry.Relative);
                        Directory.CreateDirectory(Path.GetDirectoryName(entry.Saved));
                    }
                    File.Move(entry.Target, entry.Saved);
                    entry.Moved = true;
                }

                List<string> written;
                string extractError;
                if (!UpdatePackage.Extract(packagePath, manifest, SectionApp, installDirectory, out written, out extractError))
                {
                    error = "Extraction failed: " + extractError;
                    return false;
                }

                foreach (var entry in entries)
                {
                    if (!File.Exists(entry.Target) || SafePaths.IsReparsePoint(entry.Target) ||
                        new FileInfo(entry.Target).Length != entry.Size)
                    {
                        error = "The installed file " + entry.Relative + " is missing or does not match the package.";
                        return false;
                    }
                }
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        public bool Rollback()
        {
            var ok = true;
            for (var i = entries.Count - 1; i >= 0; i--)
            {
                var entry = entries[i];
                try
                {
                    if (entry.Existed && !entry.Moved) continue;
                    DeleteWithRetry(entry.Target);
                    if (entry.Moved)
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(entry.Target));
                        File.Move(entry.Saved, entry.Target);
                        entry.Moved = false;
                    }
                }
                catch (Exception ex)
                {
                    ok = false;
                    UpdaterLog.Info("Could not restore " + entry.Relative + ": " + ex.Message);
                }
            }

            foreach (var directory in missingDirectories)
            {
                try
                {
                    if (Directory.Exists(directory) && Directory.GetFileSystemEntries(directory).Length == 0)
                    {
                        Directory.Delete(directory);
                    }
                }
                catch (Exception ex)
                {
                    UpdaterLog.Info("Could not remove a new directory: " + ex.Message);
                }
            }

            if (ok) RemoveBackup();
            return ok;
        }

        public void Commit()
        {
            RemoveBackup();
            foreach (var entry in entries)
            {
                if (!entry.IsOwnExecutable || entry.Saved == null) continue;
                try
                {
                    File.Delete(entry.Saved);
                }
                catch (Exception)
                {
                    UpdaterLog.Info("The previous updater copy will be removed on the next run.");
                }
            }
        }

        private void RemoveBackup()
        {
            if (backupRoot == null) return;
            try
            {
                if (Directory.Exists(backupRoot)) Directory.Delete(backupRoot, true);
                var parent = Path.GetDirectoryName(backupRoot);
                if (Directory.Exists(parent) && Directory.GetFileSystemEntries(parent).Length == 0) Directory.Delete(parent);
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("The update backup could not be removed: " + ex.Message);
            }
        }

        private static void DeleteWithRetry(string path)
        {
            for (var attempt = 0; ; attempt++)
            {
                try
                {
                    if (File.Exists(path))
                    {
                        File.SetAttributes(path, FileAttributes.Normal);
                        File.Delete(path);
                    }
                    return;
                }
                catch (IOException)
                {
                    if (attempt >= 4) throw;
                    Thread.Sleep(250);
                }
                catch (UnauthorizedAccessException)
                {
                    if (attempt >= 4) throw;
                    Thread.Sleep(250);
                }
            }
        }
    }
}
