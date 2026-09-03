using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.IO;

namespace ApexSenseBridge
{
    internal static class InstallLocator
    {
        private const string RegistryPath = @"SOFTWARE\ApexSenseBridge";
        private const string EngineFileName = "ApexSenseBridge.exe";

        public static string ResolveEngine(string configuredPath)
        {
            // A user-selected executable is authoritative. This keeps portable
            // distributions usable when the machine-wide installer cannot run.
            if (IsEngine(configuredPath))
            {
                return Path.GetFullPath(configuredPath);
            }

            foreach (var view in RegistryViews())
            {
                try
                {
                    using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    using (var key = baseKey.OpenSubKey(RegistryPath, false))
                    {
                        var explicitPath = key?.GetValue("ExecutablePath") as string;
                        if (IsEngine(explicitPath))
                        {
                            return Path.GetFullPath(explicitPath);
                        }

                        var installPath = key?.GetValue("InstallPath") as string;
                        if (!string.IsNullOrWhiteSpace(installPath))
                        {
                            var installedEngine = Path.Combine(installPath, EngineFileName);
                            if (IsEngine(installedEngine))
                            {
                                return Path.GetFullPath(installedEngine);
                            }
                        }
                    }
                }
                catch (Exception)
                {
                    // A damaged or inaccessible registry view must not prevent
                    // checking the other supported discovery locations.
                }
            }

            var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            if (!string.IsNullOrWhiteSpace(programFiles))
            {
                var installedEngine = Path.Combine(programFiles, "ApexSenseBridge", EngineFileName);
                if (IsEngine(installedEngine))
                {
                    return Path.GetFullPath(installedEngine);
                }
            }

            return string.Empty;
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

        internal static bool IsEngine(string path)
        {
            return !string.IsNullOrWhiteSpace(path) &&
                   string.Equals(Path.GetFileName(path), EngineFileName,
                                 StringComparison.OrdinalIgnoreCase) &&
                   File.Exists(path);
        }
    }
}
