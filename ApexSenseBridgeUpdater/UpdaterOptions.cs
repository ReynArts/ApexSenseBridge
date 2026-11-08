using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;

namespace ApexSenseBridgeUpdater
{
    internal sealed class UpdaterOptions
    {
        public string PackagePath;
        public string InstallDirectory;
        public string ExpectedVersion;
        public uint WaitProcessId;
        public bool RelaunchTray;
        public bool Silent;
        public bool Force;
#if DEBUG
        public string TestProgramData;
        public string TestRegistryRoot;
        public string TestSessionName;
        public bool TestEnforceInstallAcl;
        public bool TestRealTrust;
        public bool TestStrictStagingAcl;
#endif

        public bool RelaxedStagingAcl
        {
            get
            {
#if DEBUG
                return IsTestMode && !TestStrictStagingAcl;
#else
                return false;
#endif
            }
        }

        public bool IsTestMode
        {
            get
            {
#if DEBUG
                return TestProgramData != null || TestRegistryRoot != null || TestSessionName != null;
#else
                return false;
#endif
            }
        }

        private static readonly Regex VersionPattern =
            new Regex(@"^\d{1,5}\.\d{1,5}\.\d{1,5}$", RegexOptions.CultureInvariant);

        public static bool TryParse(string[] args, string ownDirectory, out UpdaterOptions options, out string error)
        {
            options = new UpdaterOptions();
            error = null;
            var seen = new HashSet<string>(StringComparer.Ordinal);
            for (var i = 0; i < args.Length; i++)
            {
                var name = args[i];
                if (!seen.Add(name))
                {
                    error = "Duplicate option " + name + ".";
                    return false;
                }

                switch (name)
                {
                    case "--relaunch-tray": options.RelaunchTray = true; continue;
                    case "--silent": options.Silent = true; continue;
                    case "--force": options.Force = true; continue;
#if DEBUG
                    case "--test-enforce-install-acl": options.TestEnforceInstallAcl = true; continue;
                    case "--test-real-trust": options.TestRealTrust = true; continue;
                    case "--test-strict-staging-acl": options.TestStrictStagingAcl = true; continue;
#endif
                }

                if (!IsValueOption(name))
                {
                    error = "Unknown option.";
                    return false;
                }

                if (i + 1 >= args.Length || args[i + 1].StartsWith("--", StringComparison.Ordinal))
                {
                    error = "Option " + name + " needs a value.";
                    return false;
                }

                var value = args[++i];
                switch (name)
                {
                    case "--package": options.PackagePath = value; break;
                    case "--install-dir": options.InstallDirectory = value; break;
                    case "--expected-version": options.ExpectedVersion = value; break;
                    case "--wait-pid":
                        uint pid;
                        if (!uint.TryParse(value, out pid) || pid <= 4)
                        {
                            error = "Invalid --wait-pid.";
                            return false;
                        }
                        options.WaitProcessId = pid;
                        break;
#if DEBUG
                    case "--test-programdata": options.TestProgramData = value; break;
                    case "--test-registry-root": options.TestRegistryRoot = value; break;
                    case "--test-session-name": options.TestSessionName = value; break;
#endif
                }
            }

            if (string.IsNullOrEmpty(options.PackagePath) || string.IsNullOrEmpty(options.InstallDirectory) ||
                string.IsNullOrEmpty(options.ExpectedVersion))
            {
                error = "--package, --install-dir and --expected-version are required.";
                return false;
            }

            if (!VersionPattern.IsMatch(options.ExpectedVersion))
            {
                error = "--expected-version must look like 1.2.3.";
                return false;
            }

            string normalizedPackage;
            if (!SafePaths.TryNormalizeLocalPath(options.PackagePath, out normalizedPackage) ||
                !string.Equals(Path.GetExtension(normalizedPackage), ".zip", StringComparison.OrdinalIgnoreCase))
            {
                error = "--package must be a local .zip path.";
                return false;
            }
            options.PackagePath = normalizedPackage;

            string normalizedInstall;
            if (!SafePaths.TryNormalizeLocalPath(options.InstallDirectory, out normalizedInstall))
            {
                error = "--install-dir must be a local absolute path.";
                return false;
            }
            options.InstallDirectory = normalizedInstall.TrimEnd('\\');
            if (!string.Equals(options.InstallDirectory, ownDirectory.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase))
            {
                error = "--install-dir is not the directory of the updater.";
                return false;
            }

            return true;
        }

        private static bool IsValueOption(string name)
        {
            switch (name)
            {
                case "--package":
                case "--install-dir":
                case "--expected-version":
                case "--wait-pid":
#if DEBUG
                case "--test-programdata":
                case "--test-registry-root":
                case "--test-session-name":
#endif
                    return true;
                default:
                    return false;
            }
        }
    }
}
