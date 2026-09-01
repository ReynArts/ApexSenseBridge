using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;

namespace ApexSenseBridgeTray.Common
{
    internal static class InstallLocator
    {
        private const string RegistryPath = @"SOFTWARE\ApexSenseBridge";
        private const string EngineFileName = "ApexSenseBridge.exe";
        private const string ControlFileName = "ApexSenseBridgeControl.exe";

        public static string ResolveEngine(string legacyPath = null)
        {
            return ResolveFile(EngineFileName, legacyPath);
        }

        public static string ResolveControlPanel()
        {
            return ResolveFile(ControlFileName, null);
        }

        private static string ResolveFile(string fileName, string legacyPath)
        {
            foreach (var view in RegistryViews())
            {
                try
                {
                    using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    {
                        if (baseKey != null)
                        {
                            using (var key = baseKey.OpenSubKey(RegistryPath, false))
                            {
                                if (key != null)
                                {
                                    var explicitPath = key.GetValue("ExecutablePath") as string;
                                    if (!string.IsNullOrWhiteSpace(explicitPath))
                                    {
                                        var dir = Path.GetDirectoryName(explicitPath);
                                        var target = Path.Combine(dir, fileName);
                                        if (File.Exists(target))
                                        {
                                            return Path.GetFullPath(target);
                                        }
                                    }

                                    var installPath = key.GetValue("InstallPath") as string;
                                    if (!string.IsNullOrWhiteSpace(installPath))
                                    {
                                        var target = Path.Combine(installPath, fileName);
                                        if (File.Exists(target))
                                        {
                                            return Path.GetFullPath(target);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch
                {
                }
            }

            var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            if (!string.IsNullOrWhiteSpace(programFiles))
            {
                var target = Path.Combine(programFiles, "ApexSenseBridge", fileName);
                if (File.Exists(target))
                {
                    return Path.GetFullPath(target);
                }
            }

            var appDir = AppDomain.CurrentDomain.BaseDirectory;
            if (!string.IsNullOrWhiteSpace(appDir))
            {
                var target = Path.Combine(appDir, fileName);
                if (File.Exists(target))
                {
                    return Path.GetFullPath(target);
                }
            }

            var searchRoots = new List<string>();
            if (!string.IsNullOrWhiteSpace(appDir))
            {
                searchRoots.Add(appDir);
                var parent = Path.GetDirectoryName(appDir.TrimEnd('\\', '/'));
                if (!string.IsNullOrWhiteSpace(parent))
                    searchRoots.Add(parent);
                var grandparent = Path.GetDirectoryName(parent);
                if (!string.IsNullOrWhiteSpace(grandparent))
                    searchRoots.Add(grandparent);
                var greatgrandparent = Path.GetDirectoryName(grandparent);
                if (!string.IsNullOrWhiteSpace(greatgrandparent))
                    searchRoots.Add(greatgrandparent);
            }

            var relativePaths = new[]
            {
                "build-win\\Release",
                "build-verify\\Release",
                "dist",
            };

            foreach (var root in searchRoots)
            {
                foreach (var rel in relativePaths)
                {
                    try
                    {
                        var candidate = Path.Combine(root, rel, fileName);
                        if (File.Exists(candidate))
                        {
                            return Path.GetFullPath(candidate);
                        }
                    }
                    catch
                    {
                    }
                }
            }

            return (!string.IsNullOrWhiteSpace(legacyPath) && File.Exists(legacyPath))
                ? Path.GetFullPath(legacyPath)
                : string.Empty;
        }

        private static IEnumerable<RegistryView> RegistryViews()
        {
            if (Environment.Is64BitOperatingSystem)
            {
                yield return RegistryView.Registry64;
                yield return RegistryView.Registry32;
            }
            else
            {
                yield return RegistryView.Default;
            }
        }
    }
}
