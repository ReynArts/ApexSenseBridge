using System;
using System.Diagnostics;
using System.IO;
using System.Security.Principal;
using System.Threading;
using System.Windows.Forms;
using ApexSenseBridge.Security;

namespace ApexSenseBridgeUpdater
{
    internal static class Program
    {
        private const int ExitOk = 0;
        private const int ExitBadArguments = 2;
        private const int ExitPackageRejected = 3;
        private const int ExitDriversChanged = 4;
        private const int ExitApplyFailed = 5;
        private const int ExitRollbackFailed = 6;
        private const string MutexName = "Global\\ApexSenseBridge.Updater.v1";

        [STAThread]
        private static int Main(string[] args)
        {
            var ownExecutable = Process.GetCurrentProcess().MainModule.FileName;
            var ownDirectory = Path.GetDirectoryName(ownExecutable);

            UpdaterOptions options;
            string error;
            var parsed = UpdaterOptions.TryParse(args, ownDirectory, out options, out error);

            string programDataDirectory;
            if (!TryPrepareProgramData(options, out programDataDirectory, out var programDataError))
            {
                return Finish(options, ExitApplyFailed, programDataError);
            }
            UpdaterLog.Initialize(programDataDirectory);
            UpdaterLog.Info("Updater " + typeof(Program).Assembly.GetName().Version + " started" +
                            (options.IsTestMode ? " (test mode)" : string.Empty) + ".");

            if (!parsed) return Finish(options, ExitBadArguments, error);
#if DEBUG
            if (options.IsTestMode && !options.TestRealTrust) TestHooks.Install(options);
#endif

#if !DEBUG
            if (!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
            {
                return Finish(options, ExitApplyFailed, "The updater must run as administrator.");
            }
#endif

            bool createdNew;
            using (var mutex = new Mutex(false, MutexName, out createdNew))
            {
                var held = false;
                try
                {
                    try
                    {
                        held = mutex.WaitOne(0);
                    }
                    catch (AbandonedMutexException)
                    {
                        held = true;
                    }

                    if (!held) return Finish(options, ExitApplyFailed, "Another update is already running.");
                    return Run(options, ownExecutable, programDataDirectory);
                }
                finally
                {
                    if (held) mutex.ReleaseMutex();
                }
            }
        }

        private static int Run(UpdaterOptions options, string ownExecutable, string programDataDirectory)
        {
            var installDirectory = options.InstallDirectory;
            var registry = new InstallRegistry(TestRegistryRoot(options));

            string error;
            if (!ValidateInstallation(options, registry, out error)) return Finish(options, ExitBadArguments, error);
            InstallTransaction.CleanupLeftovers(installDirectory, ownExecutable);

            var trayWasRunning = ProcessControl.IsImageRunning(installDirectory, "ApexSenseBridgeTray") || options.WaitProcessId != 0;
            UpdaterLog.Info("Tray " + (trayWasRunning ? "expected (running or the waited-for caller)" : "not running") + " at start.");

            var relaunch = false;
            var code = Apply(options, registry, ownExecutable, programDataDirectory, out error, ref relaunch);

            if (code == ExitOk && relaunch) UpdaterLog.Info("Update to " + options.ExpectedVersion + " applied.");
            if (relaunch && options.RelaunchTray && trayWasRunning && code != ExitRollbackFailed) RelaunchTray(installDirectory);
            return Finish(options, code, error);
        }

        private static int Apply(UpdaterOptions options, InstallRegistry registry, string ownExecutable,
            string programDataDirectory, out string error, ref bool relaunch)
        {
            error = null;
            var installDirectory = options.InstallDirectory;

            if (!File.Exists(options.PackagePath) || SafePaths.IsReparsePoint(options.PackagePath))
            {
                error = "The update package is missing or is a link.";
                return ExitPackageRejected;
            }

            relaunch = true;
            if (options.WaitProcessId != 0) ProcessControl.WaitForExit(options.WaitProcessId, TimeSpan.FromSeconds(60));

            using (var staging = StagingArea.Create(programDataDirectory, options.RelaxedStagingAcl, out error))
            {
                if (staging == null) return ExitApplyFailed;
                if (!staging.CopyPackage(options.PackagePath, out error))
                {
                    return staging.FailureIsPackageProblem ? ExitPackageRejected : ExitApplyFailed;
                }

                UpdatePackageManifest manifest;
                var engine = Path.Combine(installDirectory, "ApexSenseBridge.exe");
                if (!UpdatePackage.Verify(staging.PackageCopy, options.ExpectedVersion, engine, out manifest, out error))
                {
                    return ExitPackageRejected;
                }
                UpdaterLog.Info("Package verified: version " + manifest.Version + ", " + manifest.Files.Count + " files.");

                if (IsAlreadyApplied(manifest, engine))
                {
                    relaunch = false;
                    UpdaterLog.Info("Version " + manifest.Version + " is already installed; no file was changed.");
                    return ExitOk;
                }

                string reason;
                if (manifest.DriversChanged)
                {
                    error = "The package changes the drivers; the full setup is required.";
                    return ExitDriversChanged;
                }
                if (!UpdatePackage.DriversMatchInstalled(manifest, out reason))
                {
                    error = "The installed drivers differ from the package: " + reason;
                    return ExitDriversChanged;
                }

                string sessionText;
                if (SessionGuard.HasActiveSession(SessionName(options), out sessionText))
                {
                    if (!options.Force)
                    {
                        UpdaterLog.Info("active session: " + sessionText);
                        error = sessionText + " Close it first.";
                        return ExitApplyFailed;
                    }
                    UpdaterLog.Info(sessionText + " Continuing because --force was given.");
                }

                var transaction = new InstallTransaction(installDirectory, ownExecutable);
                if (!transaction.Prepare(manifest, out error)) return ExitApplyFailed;
                UpdaterLog.Info("Prepared " + transaction.FileCount + " files.");

                UpdaterLog.Info("Stopping the application.");
                ProcessControl.StopInstalledProcesses(installDirectory);
                if (options.Force) ProcessControl.RunEngineCommand(installDirectory, "restore-controller-visibility");

                var applied = transaction.Apply(staging.PackageCopy, manifest, out error);
                if (applied)
                {
                    try
                    {
                        registry.WriteVersion(options.ExpectedVersion);
                    }
                    catch (Exception ex)
                    {
                        error = "The registry could not be updated: " + ex.Message;
                        applied = false;
                    }
                }

                if (applied)
                {
                    transaction.Commit();
                    return ExitOk;
                }

                UpdaterLog.Info("Apply failed: " + error);
                var registryRestored = registry.Rollback();
                var filesRestored = transaction.Rollback();
                if (filesRestored && registryRestored)
                {
                    UpdaterLog.Info("Rollback completed.");
                    return ExitApplyFailed;
                }

                error = "Apply failed and the rollback was incomplete: " + error;
                return ExitRollbackFailed;
            }
        }

        private static bool IsAlreadyApplied(UpdatePackageManifest manifest, string enginePath)
        {
            try
            {
                var installed = UpdatePackage.InstalledVersionReader(enginePath);
                Version package;
                if (installed == null || !Version.TryParse(manifest.Version, out package)) return false;
                return package.Major == installed.Major && package.Minor == installed.Minor &&
                       package.Build == Math.Max(installed.Build, 0);
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool ValidateInstallation(UpdaterOptions options, InstallRegistry registry, out string error)
        {
            error = null;
            var installDirectory = options.InstallDirectory;
            if (!Directory.Exists(installDirectory) || SafePaths.IsReparsePoint(installDirectory))
            {
                error = "The install directory is missing or is a link.";
                return false;
            }

            var engine = Path.Combine(installDirectory, "ApexSenseBridge.exe");
            if (!File.Exists(engine) || SafePaths.IsReparsePoint(engine))
            {
                error = "ApexSenseBridge.exe was not found in the install directory.";
                return false;
            }

            string registered;
            try
            {
                registered = registry.ReadInstallPath();
            }
            catch (Exception ex)
            {
                error = "The installation could not be identified: " + ex.Message;
                return false;
            }

            string registeredFull;
            if (!SafePaths.TryNormalizeLocalPath(registered, out registeredFull) ||
                !string.Equals(registeredFull.TrimEnd('\\'), installDirectory, StringComparison.OrdinalIgnoreCase))
            {
                error = "The install directory is not the registered ApexSenseBridge installation.";
                return false;
            }

            var enforceAcl = true;
#if DEBUG
            enforceAcl = !options.IsTestMode || options.TestEnforceInstallAcl;
#endif
            if (enforceAcl)
            {
                string detail;
                if (SafePaths.IsWritableByNonAdministrators(installDirectory, out detail))
                {
                    error = "The install directory is writable by non-administrators (" + detail + ").";
                    return false;
                }
            }
            return true;
        }

        private static bool TryPrepareProgramData(UpdaterOptions options, out string directory, out string error)
        {
            error = null;
            var root = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
#if DEBUG
            if (options.TestProgramData != null) root = options.TestProgramData;
#endif
            directory = Path.Combine(root, "ApexSenseBridge");
            try
            {
                if (Directory.Exists(directory))
                {
                    if (SafePaths.IsReparsePoint(directory))
                    {
                        error = "The ProgramData directory is a link.";
                        return false;
                    }
                }
                else
                {
                    Directory.CreateDirectory(directory);
                }
                return true;
            }
            catch (Exception ex)
            {
                error = "The ProgramData directory could not be prepared: " + ex.Message;
                return false;
            }
        }

        private static string TestRegistryRoot(UpdaterOptions options)
        {
#if DEBUG
            return options.TestRegistryRoot;
#else
            return null;
#endif
        }

        private static string SessionName(UpdaterOptions options)
        {
#if DEBUG
            return options.TestSessionName;
#else
            return null;
#endif
        }

        private static void RelaunchTray(string installDirectory)
        {
            var tray = Path.Combine(installDirectory, "ApexSenseBridgeTray.exe");
            if (!File.Exists(tray)) return;
            try
            {
                var explorer = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
                Process.Start(new ProcessStartInfo(explorer, "\"" + tray + "\"") { UseShellExecute = false })?.Dispose();
                UpdaterLog.Info("Tray relaunch requested through the shell.");
            }
            catch (Exception ex)
            {
                UpdaterLog.Info("The Tray could not be relaunched: " + ex.Message);
            }
        }

        private static int Finish(UpdaterOptions options, int code, string message)
        {
            if (code != ExitOk) UpdaterLog.Info("Exit " + code + ": " + message);
            if (code != ExitOk && !options.Silent && !options.IsTestMode)
            {
                try
                {
                    MessageBox.Show("ApexSenseBridge could not be updated (code " + code + ").\n\n" + message +
                                    "\n\nLog file: %ProgramData%\\ApexSenseBridge\\update.log",
                        "ApexSenseBridge update", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                catch (Exception)
                {
                }
            }
            return code;
        }
    }
}
