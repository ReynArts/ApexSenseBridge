using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using ApexSenseBridge.Common;
using ApexSenseBridge.Security;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;

internal static class TrayLearningTests
{
    private static int assertions;

    public static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--gyro-hardware") return TestGyroHardwareLifecycle();
        if (args.Length > 0 && (args[0] == "--fake-session" || args.Contains("--session-token"))) return RunFakeSession(args);
        if (args.Length == 1 && args[0] == "--show")
        {
            using (var shown = EventWaitHandle.OpenExisting(Environment.GetEnvironmentVariable("ASB_TEST_SHOW_EVENT"))) shown.Set();
            return 0;
        }
        var testRoot = Path.Combine(
            Path.GetTempPath(),
            "ApexSenseBridge-LearningTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(testRoot);

        try
        {
            TestReleaseAssetPolicy();
            TestUpdateLaunchPolicy();
            TestUpdateAttemptMarker();
            TestUpdateFlowContractAndLocalization();
            TestLatencyStatisticsAndTelemetry();
            TestGyroStreamParsing();
            TestControllerTestRefusedDuringBridgeSession();
            TestColocatedEngineTakesPriority();
            TestStableLearningResolutionExportAndDeletion(testRoot);
            TestCancelledAndUnstableSessionsAreNotLearned(testRoot);
            TestConcurrentObservationsAreIndependent(testRoot);
            TestRepeatedObservationDoesNotRestartStabilityWindow(testRoot);
            TestImmediateShutdownFlushesValidatedBinding(testRoot);
            TestLimitedInformationProcessPathLookup();
            TestCorruptAndOversizedCaches(testRoot);
            TestAmbiguousSteamAppIdFallsBackToNormalizedIdentity(testRoot);
            TestDatabaseExecutableResolutionAndCollisions();
            TestShortGameNamesCannotFuzzyMatchUnrelatedProcesses();
            TestDatabaseExecutableMissPerformance();
            TestGeneratedDatabaseExecutableCoverage();
            TestCatalogAddedDates();
            TestActivationPolicyStillAppliesAfterLearning();
            TestManualFixGamesPreservePhysicalInput();
            TestManualFixGuideEntries();
            TestPerGameApexProfileSettings();
            TestOnboardProfileSelectorLock();
            TestManualBridgeModeIsNotPersisted();
            TestTrayStartupOptions();
            TestSharedBridgeArguments();
            TestControllerCalibrations();
            TestReportRelayClient();
            TestControllerCalibrationDetection();
            TestMultipleControllerDetection();
            TestControllerSelection();
            TestGameExecutableSettings();
            TestApex6Rate2kHzSetting();
            TestApex6Rate2kHzLocalization();
            TestSessionStatusDiscovery();
            TestRecoveryPolicy();
            TestHidHideBusyDetection();
            TestInterruptedSessionRecovery();
            TestExternalRecoveryOwnership();
            TestControllerLostStatusMapping();
            TestControllerLostLocalization();
            TestControllerLostIsNotAFailure();
            TestControllerLostOnExternalSession();
            TestRecoveryCancelledDuringStartup();
            TestRecoveryGameLifetimeTracking();
            TestStartRetryPolicy();
            TestAutomaticStartPause();
            TestIdentityFailureQuickRetry();
            TestPidChurningFailureLoopPauses();
            TestUnexpectedEngineExitBacksOff();
            TestLauncherHandoffGraceAndInstallFolder();
            TestForegroundSwitchesNeverRestartSession();
            TestPreparedLaunchLifecycle();
            TestProcessLivenessClassification();
            TestProtectedGameProcessStaysAttached(testRoot);
            TestEngineMainEntry(testRoot);
            TestGameProcessSessionPidHandoff();
            TestPlatformClientsNeverCountAsGameProcesses();
            TestPidTrackingFastPathPerformance();
            TestMissPerformance(testRoot);
            TestSteamAlreadyRunningDetection();
            TestInstallerIdentityIgnoresVersionResourcePadding(testRoot);
            TestStartupDiagnosticsReport();
            assertions += UpdatePackageTests.Run(testRoot);
            Console.WriteLine("Tray executable learning tests passed ({0} assertions).", assertions);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.ToString());
            return 1;
        }
        finally
        {
            try { Directory.Delete(testRoot, true); } catch { }
        }
    }

    private static void TestReleaseAssetPolicy()
    {
        const string officialUrl =
            "https://github.com/ReynArts/ApexSenseBridge/releases/download/v0.6.3/ApexSenseBridge-Setup.exe";
        Assert(ReleaseSecurity.IsExpectedSetupAsset(
            "ApexSenseBridge-Setup.exe", officialUrl),
            "the exact official setup asset should be accepted");
        Assert(!ReleaseSecurity.IsExpectedSetupAsset(
            "ApexSenseBridgeTray.exe", officialUrl),
            "a different release executable must be rejected");
        Assert(!ReleaseSecurity.IsExpectedSetupAsset(
            "ApexSenseBridge-Setup.exe",
            "https://github.com/SomeoneElse/ApexSenseBridge/releases/download/v0.6.3/ApexSenseBridge-Setup.exe"),
            "a setup asset from another repository must be rejected");
        Assert(!ReleaseSecurity.IsExpectedSetupAsset(
            "ApexSenseBridge-Setup.exe",
            "http://github.com/ReynArts/ApexSenseBridge/releases/download/v0.6.3/ApexSenseBridge-Setup.exe"),
            "an insecure download URL must be rejected");
        Assert(!ReleaseSecurity.IsExpectedSetupAsset(
            "ApexSenseBridge-Setup.exe",
            "https://github.com.evil.example/ReynArts/ApexSenseBridge/releases/download/v0.6.3/ApexSenseBridge-Setup.exe"),
            "a lookalike GitHub host must be rejected");
    }

    private static void TestUpdateLaunchPolicy()
    {
        Assert(UpdateLaunchPolicy.NormalizeVersion("v1.2.3") == "1.2.3" &&
               UpdateLaunchPolicy.NormalizeVersion("1.2.3.4") == "1.2.3" &&
               UpdateLaunchPolicy.NormalizeVersion("1.2") == "1.2.0" &&
               UpdateLaunchPolicy.NormalizeVersion(" V2.0.1 ") == "2.0.1",
            "release versions must normalize to Major.Minor.Build");
        Assert(UpdateLaunchPolicy.NormalizeVersion(null) == null &&
               UpdateLaunchPolicy.NormalizeVersion("") == null &&
               UpdateLaunchPolicy.NormalizeVersion("latest") == null &&
               UpdateLaunchPolicy.NormalizeVersion("1.2.3-beta") == null,
            "unparsable versions must not normalize");
        Assert(UpdateLaunchPolicy.IsSameOrNewer("1.2.0.0", "1.2.0") &&
               UpdateLaunchPolicy.IsSameOrNewer("1.3.0", "1.2.9") &&
               !UpdateLaunchPolicy.IsSameOrNewer("1.1.9", "1.2.0") &&
               !UpdateLaunchPolicy.IsSameOrNewer("garbage", "1.2.0"),
            "version ordering for the update marker is wrong");

        const string url = "https://github.com/ReynArts/ApexSenseBridge/releases/download/v1.2.0/ApexSenseBridge-Update.zip";
        Assert(UpdateLaunchPolicy.ShouldTryPackage(url, "1.2.0", null, true, true),
            "a usable package with the updater and engine present must be tried");
        Assert(!UpdateLaunchPolicy.ShouldTryPackage(null, "1.2.0", null, true, true) &&
               !UpdateLaunchPolicy.ShouldTryPackage("", "1.2.0", null, true, true),
            "a release without a package asset must use the full setup");
        Assert(!UpdateLaunchPolicy.ShouldTryPackage(url, "1.2.0", null, false, true),
            "a portable copy without the updater must use the full setup");
        Assert(!UpdateLaunchPolicy.ShouldTryPackage(url, "1.2.0", null, true, false),
            "an install without a co-located engine must use the full setup");
        Assert(!UpdateLaunchPolicy.ShouldTryPackage(url, "not-a-version", null, true, true),
            "an unparsable release version must use the full setup");
        Assert(!UpdateLaunchPolicy.ShouldTryPackage(url, "v1.2.0", "1.2.0", true, true),
            "a version whose package updater failed must use the full setup");
        Assert(UpdateLaunchPolicy.ShouldTryPackage(url, "1.3.0", "1.2.0", true, true),
            "a failure on one version must not block the next release");

        Assert(UpdateLaunchPolicy.BuildUpdaterArguments(
                   @"C:\Temp\ApexSenseBridge-Update-abc.zip", @"C:\Program Files\ApexSenseBridge", "v1.2.0", 4321) ==
               "--package \"C:\\Temp\\ApexSenseBridge-Update-abc.zip\" --install-dir \"C:\\Program Files\\ApexSenseBridge\" " +
               "--expected-version 1.2.0 --wait-pid 4321 --relaunch-tray",
            "the updater command line does not match the shared contract");
        var trailing = UpdateLaunchPolicy.BuildUpdaterArguments(
            @"C:\Temp\u.zip", @"C:\Program Files\ApexSenseBridge\", "1.2.0", 7);
        Assert(trailing != null && trailing.Contains("--install-dir \"C:\\Program Files\\ApexSenseBridge\" "),
            "a trailing backslash would escape the closing quote of the install directory");
        Assert(!UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\Program Files\ApexSenseBridge", "1.2.0", 7)
                   .Contains("--force") &&
               !UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\Program Files\ApexSenseBridge", "1.2.0", 7)
                   .Contains("--silent"),
            "the Tray must never force the updater past an active session");
        Assert(UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u"".zip", @"C:\App", "1.2.0", 7) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\App""x", "1.2.0", 7) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments(@"u.zip", @"C:\App", "1.2.0", 7) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\", "1.2.0", 7) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\App", "bogus", 7) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments(@"C:\Temp\u.zip", @"C:\App", "1.2.0", 0) == null &&
               UpdateLaunchPolicy.BuildUpdaterArguments("C:\\Temp\\u\r\n.zip", @"C:\App", "1.2.0", 7) == null,
            "unsafe or incomplete updater arguments must be refused");
    }

    private static void TestUpdateAttemptMarker()
    {
        var settings = new TraySettings();
        Assert(string.IsNullOrEmpty(settings.UpdateAttemptVersion) &&
               string.IsNullOrEmpty(settings.UpdatePackageFailedVersion) &&
               !settings.IsUpdatePackageBlocked("1.2.0") &&
               settings.ResolveUpdateAttempt("1.1.0") == null,
            "a fresh profile must have no update marker");

        settings.BeginUpdateAttempt("v1.2.0");
        Assert(settings.UpdateAttemptVersion == "1.2.0", "the attempted version was not recorded normalized");
        Assert(settings.ResolveUpdateAttempt("1.2.0.0") == null &&
               string.IsNullOrEmpty(settings.UpdateAttemptVersion) &&
               !settings.IsUpdatePackageBlocked("1.2.0"),
            "a start on the attempted version is a success and must clear the marker");

        settings.BeginUpdateAttempt("1.2.0");
        Assert(settings.ResolveUpdateAttempt("1.1.0") == "1.2.0",
            "a start on the old version after an attempt is a failed update");
        Assert(string.IsNullOrEmpty(settings.UpdateAttemptVersion) &&
               settings.IsUpdatePackageBlocked("1.2.0") && settings.IsUpdatePackageBlocked("v1.2.0.0") &&
               !settings.IsUpdatePackageBlocked("1.3.0"),
            "a failed attempt must block the package for that version only");
        Assert(settings.ResolveUpdateAttempt("1.1.0") == null && settings.IsUpdatePackageBlocked("1.2.0"),
            "the failure marker must survive later starts without a new attempt");

        settings.BeginUpdateAttempt("1.2.0");
        settings.AbandonUpdateAttempt();
        Assert(string.IsNullOrEmpty(settings.UpdateAttemptVersion) && settings.ResolveUpdateAttempt("1.1.0") == null,
            "an abandoned attempt (UAC cancelled, launch failure) must not count as a failure");

        settings.BeginUpdateAttempt("1.2.0");
        Assert(settings.ResolveUpdateAttempt("1.2.0") == null && !settings.IsUpdatePackageBlocked("1.2.0"),
            "a later successful update to the blocked version must lift the block");

        settings.BeginUpdateAttempt("1.4.0");
        Assert(settings.ResolveUpdateAttempt("1.3.0") == "1.4.0" && settings.IsUpdatePackageBlocked("1.4.0"),
            "a failed attempt on a newer version must be recorded");

        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.UpdatePackageFailedVersion == "1.4.0" && restored.IsUpdatePackageBlocked("1.4.0"),
            "the failure marker must survive a settings round trip");
    }

    private static void TestUpdateFlowContractAndLocalization()
    {
        var root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", ".."));
        var service = File.ReadAllText(Path.Combine(root, "ApexSenseBridgeTray", "Services", "UpdateCheckerService.cs"), Encoding.UTF8);
        Assert(service.Contains("UpdatePackage.IsExpectedUpdateAsset(") &&
               service.Contains("psi.Verb = \"runas\";") &&
               service.Contains("UacCancelledErrorCode = 1223") &&
               service.Contains("UpdatePackage.ReadManifest(") &&
               service.Contains("UpdatePackage.DriversMatchInstalled(") &&
               service.Contains("ReleaseSecurity.VerifyInstaller("),
            "the Tray update flow lost one of its required steps");
        Assert(!service.Contains("--force") && !service.Contains("Process.GetProcessesByName"),
            "the Tray must not force the updater or kill processes itself");

        var directory = Path.Combine(root, "ApexSenseBridgeTray", "Resources", "Languages");
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer { MaxJsonLength = int.MaxValue };
        var english = serializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(directory, "en.json"), Encoding.UTF8));
        var keys = new[]
        {
            "Loc_UpdateDownloadingPackage", "Loc_UpdateDownloadingSetup", "Loc_UpdateRestarting",
            "Loc_UpdateCancelled", "Loc_UpdateSessionActive", "Loc_UpdateAttemptFailedTitle", "Loc_UpdateAttemptFailedBody"
        };
        foreach (var language in new[] { "en", "es", "fr", "ja", "ko", "pt", "ru", "vi", "zh" })
        {
            var entries = serializer.Deserialize<Dictionary<string, object>>(
                File.ReadAllText(Path.Combine(directory, language + ".json"), Encoding.UTF8));
            foreach (var key in keys)
            {
                object value;
                Assert(entries.TryGetValue(key, out value) && !string.IsNullOrWhiteSpace(value as string),
                    language + " has no " + key + ".");
                Assert(language == "en" || (string)value != (string)english[key],
                    language + " reuses the English " + key + ".");
            }
            foreach (var key in new[] { "Loc_UpdateDownloadingPackage", "Loc_UpdateDownloadingSetup", "Loc_UpdateRestarting", "Loc_UpdateAttemptFailedBody" })
                Assert(((string)entries[key]).Contains("{0}"), language + " dropped the version placeholder of " + key + ".");
            Assert(((string)entries["Loc_UpdateRestarting"]).Contains("ApexSenseBridge"),
                language + " translated the product name in the restart text.");
        }
    }

    private static int TestGyroHardwareLifecycle()
    {
        try
        {
            using (var sensors = new ManualResetEventSlim())
            using (var service = new ControllerTestService())
            {
                Action<GyroMotionState> sample = state => {
                    if (!string.IsNullOrWhiteSpace(state.Error)) Console.Error.WriteLine(state.Error);
                    if (state.Connected &&
                        Math.Abs(state.AccelX) + Math.Abs(state.AccelY) + Math.Abs(state.AccelZ) > 5000)
                        sensors.Set();
                };
                service.StartGyroStream(sample);
                Assert(sensors.Wait(8000), "No physical IMU sample reached the Tray service.");
                var result = service.TestGyroAsync(1).GetAwaiter().GetResult();
                Assert(result.Connected && string.IsNullOrWhiteSpace(result.Error) && result.SamplesCount > 0,
                    "One-shot test overlapped the stream or failed: " + result.Error);
                sensors.Reset();
                service.StartGyroStream(sample);
                service.StopGyroStream();
                service.StartGyroStream(sample);
                Assert(sensors.Wait(8000), "The stream failed after rapid tab changes.");
                service.StopGyroStream();
                // Wait for the asynchronous worker's cooperative cleanup, then
                // ensure the next diagnostic can acquire the engine mutex.
                Thread.Sleep(1000);
                result = service.TestGyroAsync(1).GetAwaiter().GetResult();
                Assert(result.Connected && string.IsNullOrWhiteSpace(result.Error),
                    "The cooperative stop left an orphan engine: " + result.Error);
            }
            Console.WriteLine("APEX 5 hardware stream, one-shot transition, rapid restart and cooperative stop passed.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }

    private static void TestControllerTestRefusedDuringBridgeSession()
    {
        Assert(ControllerTestService.UnavailableInputMessageKey(true) == "Loc_ControllerTestInputSessionActive" &&
               ControllerTestService.UnavailableInputMessageKey(false) == "Loc_ControllerTestInputXInputOnly",
            "the controller test must explain why an XInput-only preview shows no controller");
        bool createdNew;
        using (var owner = new Mutex(false, @"Local\ApexSenseBridge.ActiveSession.Owner.v1", out createdNew))
        {
            if (!createdNew)
            {
                Console.WriteLine("Controller test session guard skipped: a bridge session owner already exists.");
                return;
            }
            Assert(!ControllerTestService.IsBridgeSessionActive(),
                "an unowned session mutex must not block the controller test");
            using (var held = new ManualResetEventSlim())
            using (var release = new ManualResetEventSlim())
            {
                var holder = new Thread(() =>
                {
                    owner.WaitOne();
                    held.Set();
                    release.Wait();
                    owner.ReleaseMutex();
                });
                holder.Start();
                Assert(held.Wait(5000), "the simulated bridge session did not start");
                try
                {
                    Assert(ControllerTestService.IsBridgeSessionActive(),
                        "an owned session mutex must be reported as an active bridge session");
                    using (var service = new ControllerTestService())
                    {
                        bool ok = service.TestRumbleAsync(10, 10, 1).GetAwaiter().GetResult();
                        Assert(!ok && service.LastFailureWasActiveSession,
                            "a vibration test must not open the controller during a bridge session");
                        ok = service.ResetAllEffectsAsync().GetAwaiter().GetResult();
                        Assert(!ok && service.LastFailureWasActiveSession,
                            "clearing effects must not reset the controller during a bridge session");
                    }
                }
                finally
                {
                    release.Set();
                    holder.Join();
                }
            }
            Assert(!ControllerTestService.IsBridgeSessionActive(),
                "the controller test must be available again after the bridge session ends");
        }
    }

    private static void TestGyroStreamParsing()
    {
        var sample = ControllerTestService.ParseGyroStreamLine(
            "GYRO:-120,45,0 ACCEL:500,-2500,9600 PITCH:14 ROLL:-3");
        Assert(sample.Connected && sample.MotionDetected && sample.GyroX == -120 &&
            sample.GyroY == 45 && sample.AccelY == -2500 && sample.Pitch == 14 && sample.Roll == -3,
            "Gyro stream lost sensor values or signed orientation.");
        Assert(!ControllerTestService.ParseGyroStreamLine("Motion diagnostic ready").Connected,
            "Diagnostic status text was treated as a sensor sample.");
    }

    private static void TestLatencyStatisticsAndTelemetry()
    {
        var samples = new List<double> { 0.1, 0.2, 0.3, 0.4, 2.0 };
        Assert(Math.Abs(ControllerTestService.Percentile(samples, 50) - 0.3) < 0.0001,
            "latency P50 should use the nearest-rank percentile");
        Assert(Math.Abs(ControllerTestService.Percentile(samples, 95) - 2.0) < 0.0001,
            "latency P95 should include the tail sample");

        const string telemetry = "{" +
            "\"forward_latency_us_p50\":671," +
            "\"forward_latency_us_p95\":1210," +
            "\"forward_latency_us_p99\":1545," +
            "\"forward_latency_samples\":4321," +
            "\"virtual_report_rate_hz\":812.5}";
        var result = ControllerTestService.ParseBridgeTelemetry(telemetry);
        Assert(result.Success && result.IsBridge && result.Samples == 4321,
            "valid bridge latency telemetry should produce a successful result");
        Assert(Math.Abs(result.P50Milliseconds - 0.671) < 0.0001 &&
               Math.Abs(result.P99Milliseconds - 1.545) < 0.0001,
            "bridge telemetry should convert microseconds to milliseconds");
        Assert(Math.Abs(result.UpdateRateHz - 812.5) < 0.0001,
            "bridge telemetry should preserve the virtual report rate");

        var empty = ControllerTestService.ParseBridgeTelemetry(
            "{\"forward_latency_samples\":0}");
        Assert(!empty.Success && !string.IsNullOrWhiteSpace(empty.Error),
            "zero-sample bridge telemetry should be rejected");
    }

    private static void TestColocatedEngineTakesPriority()
    {
        var colocatedEngine = Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "ApexSenseBridge.exe");
        var existed = File.Exists(colocatedEngine);
        byte[] original = null;
        if (existed) original = File.ReadAllBytes(colocatedEngine);

        try
        {
            File.WriteAllBytes(colocatedEngine, new byte[] { 0x41, 0x53, 0x42 });
            Assert(string.Equals(
                       InstallLocator.ResolveEngine(),
                       Path.GetFullPath(colocatedEngine),
                       StringComparison.OrdinalIgnoreCase),
                "A portable Tray must prefer its colocated engine over an installed or registered copy.");
        }
        finally
        {
            if (existed)
                File.WriteAllBytes(colocatedEngine, original);
            else
                File.Delete(colocatedEngine);
        }
    }

    private static void TestStableLearningResolutionExportAndDeletion(string root)
    {
        var cachePath = Path.Combine(root, "stable.json");
        var exportOne = Path.Combine(root, "export-one.json");
        var exportTwo = Path.Combine(root, "export-two.json");
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var executablePath = @"C:\Users\PrivateName\Games\Alpha\AlphaGame.exe";

        using (var service = new ExecutableLearningService(cachePath, TimeSpan.FromMilliseconds(35)))
        {
            SupportedGame resolved;
            Assert(!service.TryResolve(executablePath, gameList, out resolved),
                "An empty cache must miss and leave the existing resolver available.");

            service.BeginObservation(
                42, executablePath, game, "fuzzy title 'Alpha Game'",
                (pid, path) => pid == 42 && path == executablePath);

            WaitUntil(() => service.Count == 1, TimeSpan.FromSeconds(2),
                "A stable observation was not validated.");
            WaitUntil(() => File.Exists(cachePath), TimeSpan.FromSeconds(2),
                "The validated cache was not persisted.");

            Assert(service.TryResolve(executablePath, gameList, out resolved),
                "The learned exact path did not resolve.");
            Assert(service.TryResolve(
                    @"C:\Users\PrivateName\Games\Alpha\..\Alpha\AlphaGame.exe",
                    gameList, out resolved),
                "A semantically identical path was not normalized for learned resolution.");
            Assert(resolved != null && resolved.SteamAppId == 123456,
                "Steam AppID resolution did not select the expected game.");

            service.BeginObservation(
                42, executablePath, game, "learned exact path",
                (pid, path) => true);
            WaitUntil(
                () => service.GetBindings().Single().SuccessfulSessions == 2,
                TimeSpan.FromSeconds(2),
                "A second stable session did not increment the counter.");
            Assert(service.GetBindings().Single().DetectionMethod == "fuzzy title 'Alpha Game'",
                "A learned lookup replaced the original discovery method.");

            string error;
            var selected = service.GetBindings();
            Assert(service.ExportBindings(selected, exportOne, out error),
                "The first sanitized export failed: " + error);
            Assert(service.ExportBindings(selected, exportTwo, out error),
                "The second sanitized export failed: " + error);

            var firstExport = File.ReadAllText(exportOne);
            var secondExport = File.ReadAllText(exportTwo);
            Assert(firstExport == secondExport, "Exports must be deterministic.");
            Assert(!firstExport.Contains("PrivateName") && !firstExport.Contains(@"C:\Users"),
                "The export leaked an absolute local path.");
            Assert(firstExport.Contains("AlphaGame.exe") && firstExport.Contains("123456"),
                "The export omitted the executable or Steam AppID.");

            Assert(service.DeleteBindings(new[] { executablePath }) == 1,
                "Deleting one learned association failed.");
            Assert(service.Count == 0, "The deleted association remained in memory.");
            WaitUntil(
                () => FileExistsWithoutText(cachePath, "AlphaGame.exe"),
                TimeSpan.FromSeconds(2),
                "The deletion was not persisted.");
            Assert(!File.Exists(cachePath + ".tmp"), "Atomic persistence left a temporary file behind.");
        }
    }

    private static void TestCancelledAndUnstableSessionsAreNotLearned(string root)
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var cachePath = Path.Combine(root, "cancelled.json");

        using (var service = new ExecutableLearningService(cachePath, TimeSpan.FromMilliseconds(40)))
        {
            service.BeginObservation(
                50, @"D:\Games\Alpha\Cancelled.exe", game, "exact title",
                (pid, path) => true);
            service.CancelObservation(50);
            Thread.Sleep(100);
            Assert(service.Count == 0, "A cancelled session was learned.");

            service.BeginObservation(
                51, @"D:\Games\Alpha\ChangedPid.exe", game, "exact title",
                (pid, path) => pid == 999);
            Thread.Sleep(100);
            Assert(service.Count == 0, "A session with a changed PID was learned.");

            service.BeginObservation(
                52, @"D:\Games\Alpha\CancelledOne.exe", game, "exact title",
                (pid, path) => true);
            service.BeginObservation(
                53, @"D:\Games\Alpha\CancelledTwo.exe", game, "exact title",
                (pid, path) => true);
            Assert(service.PendingCount == 2,
                "The cancel-all setup did not retain both pending observations.");
            service.CancelObservation(0);
            Assert(service.PendingCount == 0,
                "Cancelling all observations left a pending executable.");
            Thread.Sleep(100);
            Assert(service.Count == 0, "A cancel-all observation was learned.");
            Assert(!File.Exists(cachePath), "An unstable session wrote a cache file.");
        }
    }

    private static void TestConcurrentObservationsAreIndependent(string root)
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var cachePath = Path.Combine(root, "concurrent.json");

        using (var service = new ExecutableLearningService(
            cachePath, TimeSpan.FromMilliseconds(60)))
        {
            int stateChanges = 0;
            int bindingChanges = 0;
            service.StateChanged += () => Interlocked.Increment(ref stateChanges);
            service.BindingsChanged += () => Interlocked.Increment(ref bindingChanges);

            service.BeginObservation(
                60, @"D:\Games\Alpha\AlphaMain.exe", game, "database executable",
                (pid, path) => true);
            service.BeginObservation(
                61, @"D:\Games\Alpha\AlphaWorker.exe", game, "product name",
                (pid, path) => true);
            service.BeginObservation(
                62, @"D:\Games\Alpha\AlphaRender.exe", game, "file description",
                (pid, path) => true);

            Assert(service.PendingCount == 3,
                "Concurrent game processes did not retain independent learning observations.");

            service.CancelObservation(61);
            Assert(service.PendingCount == 2,
                "Cancelling one process also cancelled another process observation.");

            WaitUntil(() => service.Count == 2, TimeSpan.FromSeconds(2),
                "Independent stable observations were not both learned.");
            WaitUntil(() => Volatile.Read(ref stateChanges) >= 6,
                TimeSpan.FromSeconds(2),
                "Pending/validated state changes were not published to the UI.");
            Assert(Volatile.Read(ref bindingChanges) == 2,
                "Validated bindings did not each publish a binding change.");
            var learned = service.GetBindings();
            Assert(learned.Any(x => x.Executable == "AlphaMain.exe") &&
                   learned.Any(x => x.Executable == "AlphaRender.exe"),
                "The wrong concurrent executable observations were retained.");
            Assert(!learned.Any(x => x.Executable == "AlphaWorker.exe"),
                "A specifically cancelled executable observation was learned.");
        }
    }

    private static void TestRepeatedObservationDoesNotRestartStabilityWindow(string root)
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var cachePath = Path.Combine(root, "repeated.json");
        var path = @"D:\Games\Alpha\AlphaRepeated.exe";

        using (var service = new ExecutableLearningService(
            cachePath, TimeSpan.FromMilliseconds(300)))
        {
            var started = Stopwatch.StartNew();
            service.BeginObservation(70, path, game, "database executable", (pid, exe) => true);
            Thread.Sleep(220);
            service.BeginObservation(70, path, game, "database executable", (pid, exe) => true);

            WaitUntil(() => service.Count == 1, TimeSpan.FromSeconds(2),
                "A repeated sighting never reached the stable state.");
            Assert(started.Elapsed < TimeSpan.FromMilliseconds(470),
                "A repeated sighting restarted the executable stability window.");
            Assert(service.PendingCount == 0,
                "A validated executable remained in the pending observation set.");
        }
    }

    private static void TestImmediateShutdownFlushesValidatedBinding(string root)
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var cachePath = Path.Combine(root, "shutdown-flush.json");
        var service = new ExecutableLearningService(cachePath, TimeSpan.Zero);

        service.BeginObservation(
            71, @"D:\Games\Alpha\ShutdownFlush.exe", game, "database executable",
            (pid, path) => true);
        WaitUntil(() => service.Count == 1, TimeSpan.FromSeconds(2),
            "The shutdown-flush observation was not validated.");
        service.Dispose();

        Assert(File.Exists(cachePath),
            "Disposing immediately after validation lost the learned cache.");
        using (var reloaded = new ExecutableLearningService(cachePath, TimeSpan.Zero))
        {
            reloaded.InitializeAsync();
            WaitUntil(() => reloaded.Count == 1, TimeSpan.FromSeconds(2),
                "The synchronously flushed learned binding could not be reloaded.");
        }
    }

    private static void TestLimitedInformationProcessPathLookup()
    {
        using (var process = Process.GetCurrentProcess())
        {
            var path = NativeMethods.GetProcessPath((uint)process.Id);
            Assert(!string.IsNullOrWhiteSpace(path) && Path.IsPathRooted(path),
                "PROCESS_QUERY_LIMITED_INFORMATION did not return an absolute executable path.");
            Assert(string.Equals(
                    Path.GetFileName(path),
                    "ApexSenseBridgeTray.LearningTests.exe",
                    StringComparison.OrdinalIgnoreCase),
                "The limited-information process lookup returned the wrong executable.");
        }
    }

    private static void TestCorruptAndOversizedCaches(string root)
    {
        var corruptPath = Path.Combine(root, "corrupt.json");
        File.WriteAllText(corruptPath, "{ definitely not json");
        using (var corrupt = new ExecutableLearningService(corruptPath, TimeSpan.Zero))
        {
            corrupt.InitializeAsync();
            Thread.Sleep(100);
            Assert(corrupt.Count == 0, "A corrupt cache must be ignored.");
        }

        var oversizedPath = Path.Combine(root, "oversized.json");
        var now = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
        var sb = new StringBuilder();
        sb.Append("{\"version\":1,\"bindings\":[");
        for (int i = 0; i < 2100; i++)
        {
            if (i > 0) sb.Append(',');
            sb.Append("{\"path\":\"C:\\\\Games\\\\Game")
                .Append(i.ToString(CultureInfo.InvariantCulture))
                .Append(".exe\",\"gameTitle\":\"Alpha Game\",\"gameNormalized\":\"alphagame\",")
                .Append("\"steamAppId\":123456,\"detectionMethod\":\"test\",")
                .Append("\"firstSeenUtc\":\"").Append(now).Append("\",")
                .Append("\"lastSeenUtc\":\"").Append(now).Append("\",")
                .Append("\"successfulSessions\":1}");
        }
        sb.Append("]}");
        File.WriteAllText(oversizedPath, sb.ToString());

        using (var oversized = new ExecutableLearningService(oversizedPath, TimeSpan.Zero))
        {
            oversized.InitializeAsync();
            WaitUntil(() => oversized.Count > 0, TimeSpan.FromSeconds(2),
                "The oversized cache did not load.");
            Assert(oversized.Count == 2048, "The learned cache capacity was not enforced.");
        }
    }

    private static void TestMissPerformance(string root)
    {
        var service = new ExecutableLearningService(
            Path.Combine(root, "performance.json"), TimeSpan.Zero);
        var gameList = CreateGameList();
        SupportedGame ignored;
        const string missingPath = @"E:\Games\Missing\Missing.exe";

        for (int i = 0; i < 5000; i++) service.TryResolve(missingPath, gameList, out ignored);

        var samples = new long[20000];
        for (int i = 0; i < samples.Length; i++)
        {
            long started = Stopwatch.GetTimestamp();
            service.TryResolve(missingPath, gameList, out ignored);
            samples[i] = Stopwatch.GetTimestamp() - started;
        }
        Array.Sort(samples);
        double p99Milliseconds = samples[(samples.Length * 99) / 100] * 1000.0 / Stopwatch.Frequency;
        Console.WriteLine("Learned-cache miss p99: {0:F6} ms", p99Milliseconds);
        Assert(p99Milliseconds <= 0.05,
            "The learned-cache miss exceeded the 0.05 ms p99 budget.");
        service.Dispose();
    }

    private static void TestAmbiguousSteamAppIdFallsBackToNormalizedIdentity(string root)
    {
        const string json = "{\"games\":[" +
            "{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true,\"profile\":\"standard\",\"steamAppId\":777,\"steamAppIdVerified\":true}," +
            "{\"title\":\"Beta Game\",\"normalized\":\"betagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true,\"profile\":\"standard\",\"steamAppId\":777,\"steamAppIdVerified\":true}]}";
        var gameList = CreateGameList(json);
        var alpha = FindGame(gameList, "Alpha Game");
        var path = @"F:\Games\Alpha\Alpha.exe";

        using (var service = new ExecutableLearningService(
            Path.Combine(root, "ambiguous.json"), TimeSpan.FromMilliseconds(25)))
        {
            service.BeginObservation(88, path, alpha, "exact title", (pid, executable) => true);
            WaitUntil(() => service.Count == 1, TimeSpan.FromSeconds(2),
                "The ambiguous-AppID test binding was not learned.");

            SupportedGame resolved;
            Assert(service.TryResolve(path, gameList, out resolved),
                "An ambiguous Steam AppID did not fall back to the normalized identity.");
            Assert(resolved != null && resolved.Title == "Alpha Game",
                "An ambiguous Steam AppID resolved to the wrong database entry.");
        }
    }

    private static void TestActivationPolicyStillAppliesAfterLearning()
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var settings = new TraySettings();

        Assert(GameActivationPolicy.ShouldActivate(
                game, settings, "AlphaGame", "Alpha", "AlphaGame.exe"),
            "An eligible learned game was rejected by the shared activation policy.");

        settings.SetGameExcludedAliases(game.Normalized, game.Title, true);
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "AlphaGame", "Alpha", "AlphaGame.exe"),
            "A learned association bypassed the game exclusion policy.");
        Assert(settings.IsGameExcluded(game.Title),
            "The display-title exclusion alias was not recorded.");

        settings.SetGameExcludedAliases(game.Normalized, game.Title, false);
        Assert(!settings.IsGameExcluded(game.Normalized) &&
               !settings.IsGameExcluded(game.Title),
            "Re-adding a game left one of its exclusion aliases behind.");
        settings.TriggerOnAdaptiveTriggers = false;
        settings.TriggerOnHapticFeedback = false;
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "AlphaGame", "Alpha", "AlphaGame.exe"),
            "A learned association bypassed the disabled feature criteria.");

        settings.TriggerOnHapticFeedback = true;
        Assert(GameActivationPolicy.ShouldActivate(
                game, settings, "AlphaGame", "Alpha", "AlphaGame.exe"),
            "The haptic feature criterion did not activate the learned game.");
    }

    private static void TestManualFixGamesPreservePhysicalInput()
    {
        const string json = "{\"games\":[{" +
            "\"title\":\"Dying Light\",\"normalized\":\"dyinglight\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true," +
            "\"hapticFeedback\":true,\"hapticFeedbackManualFix\":true," +
            "\"requiresManualFix\":true," +
            "\"manualFixUrl\":\"https://www.pcgamingwiki.com/wiki/Dying_Light\"," +
            "\"profile\":\"standard\",\"steamAppId\":239140," +
            "\"steamAppIdVerified\":true}]}";
        var game = FindGame(CreateGameList(json), "Dying Light");
        var settings = new TraySettings();

        Assert(game.RequiresManualFix &&
               game.AdaptiveTriggersManualFix &&
               game.HapticFeedbackManualFix,
            "The PCGamingWiki manual-fix flags were not loaded.");
        Assert(game.ManualFixUrl.EndsWith("/Dying_Light", StringComparison.Ordinal),
            "The PCGamingWiki help URL was not loaded.");
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A manual-fix-only game would still hide the working physical controller.");
        Assert(GameActivationPolicy.IsBlockedByManualFix(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "The manual-fix block reason was not exposed to the notification path.");

        Assert(GameActivationPolicy.CanToggleManualFix(game, false) &&
               !GameActivationPolicy.CanToggleManualFix(game, true) &&
               !GameActivationPolicy.CanToggleManualFix(null, false),
            "The manual-fix allowance could be toggled on an excluded or missing game.");
        settings.SetManualFixAllowed(game.Normalized, true);
        Assert(GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe") &&
               !GameActivationPolicy.IsBlockedByManualFix(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A manual-fix game the user allowed did not activate automatically.");
        settings.SetGameExcludedAliases(game.Normalized, game.Title, true);
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "Allowing a manual-fix game bypassed its exclusion.");
        settings.SetGameExcludedAliases(game.Normalized, game.Title, false);
        settings.SetManualFixAllowed(game.Normalized, false);
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "Revoking the manual-fix allowance left the game activating.");

        game.AdaptiveTriggersManualFix = false;
        Assert(GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A native feature was incorrectly blocked by another feature's manual-fix state.");
        Assert(!GameActivationPolicy.IsBlockedByManualFix(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A game with a ready selected feature was incorrectly reported as fully blocked.");
    }

    private static void TestManualFixGuideEntries()
    {
        const string json = "{\"games\":[" +
            "{\"title\":\"Forza Horizon 5\",\"normalized\":\"forzahorizon5\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixUrl\":\"https://www.pcgamingwiki.com/wiki/Forza_Horizon_5\"," +
            "\"manualFixDetails\":{\"adaptiveTriggers\":{\"note\":\"Requires DSX and RacingDSX.\"," +
            "\"links\":[{\"label\":\"RacingDSX\",\"url\":\"https://github.com/cosmii02/RacingDSX\"}," +
            "{\"label\":\"Local\",\"url\":\"file:///C:/Windows/System32/calc.exe\"}," +
            "{\"label\":\"Plain\",\"url\":\"http://example.com/fix\"}," +
            "{\"label\":\"Broken\"},\"text\"]," +
            "\"tools\":[\"DSX\",\"RacingDSX\"]}}}," +
            "{\"title\":\"Onimusha\",\"normalized\":\"onimusha\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true," +
            "\"hapticFeedback\":true,\"hapticFeedbackManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixUrl\":\"https://www.pcgamingwiki.com/wiki/Onimusha\"," +
            "\"manualFixDetails\":{" +
            "\"adaptiveTriggers\":{\"note\":\"Use the mod.\",\"links\":[],\"tools\":[]}," +
            "\"hapticFeedback\":{\"note\":\"Use the mod.\",\"links\":[],\"tools\":[]}}}," +
            "{\"title\":\"Dying Light\",\"normalized\":\"dyinglight\"," +
            "\"hapticFeedback\":true,\"hapticFeedbackManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixUrl\":\"javascript:alert(1)\"," +
            "\"manualFixDetails\":{\"adaptiveTriggers\":{\"note\":\"Stale entry.\",\"links\":[],\"tools\":[]}}}," +
            "{\"title\":\"Native\",\"normalized\":\"native\",\"adaptiveTriggers\":true,\"manualFixDetails\":\"oops\"}" +
            "]}";
        var list = CreateGameList(json);

        var forza = FindGame(list, "Forza Horizon 5");
        Assert(forza.AdaptiveTriggersFix != null && forza.HapticFeedbackFix == null &&
               forza.AdaptiveTriggersFix.Links.Length == 3 &&
               forza.AdaptiveTriggersFix.Tools.SequenceEqual(new[] { "DSX", "RacingDSX" }),
            "The manual-fix notes of the catalogue were not loaded.");
        var forzaEntries = ManualFixGuide.BuildEntries(forza);
        Assert(forzaEntries.Count == 1 &&
               forzaEntries[0].Note == "Requires DSX and RacingDSX." &&
               forzaEntries[0].Links.Length == 1 &&
               forzaEntries[0].Links[0].Url == "https://github.com/cosmii02/RacingDSX" &&
               forzaEntries[0].FeatureKeys.SequenceEqual(new[] { "Loc_PillTriggers" }),
            "The guide kept a link that is not a plain HTTPS address.");
        Assert(ManualFixGuide.PageUrl(forza) == "https://www.pcgamingwiki.com/wiki/Forza_Horizon_5",
            "The PCGamingWiki page address was not exposed to the guide.");

        var onimusha = ManualFixGuide.BuildEntries(FindGame(list, "Onimusha"));
        Assert(onimusha.Count == 1 &&
               onimusha[0].FeatureKeys.SequenceEqual(new[] { "Loc_PillTriggers", "Loc_PillHaptics" }),
            "Identical notes for both features were not merged into one guide entry.");

        var dyingLight = FindGame(list, "Dying Light");
        Assert(ManualFixGuide.BuildEntries(dyingLight).All(entry =>
                   entry.Note != "Stale entry." && !entry.FeatureKeys.Contains("Loc_PillTriggers")),
            "A note of a feature that needs no manual fix reached the guide.");
        Assert(ManualFixGuide.PageUrl(dyingLight).Length == 0,
            "A non-HTTPS page address was exposed to the guide.");

        var staleCloud = CreateGameList("{\"games\":[" +
            "{\"title\":\"Forza Horizon 5\",\"normalized\":\"forzahorizon5\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixUrl\":\"https://www.pcgamingwiki.com/wiki/Forza_Horizon_5\"}," +
            "{\"title\":\"Forza Motorsport 7\",\"normalized\":\"forzamotorsport7\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixDetails\":{\"adaptiveTriggers\":{\"note\":\"Cloud note.\",\"links\":[],\"tools\":[]}}}," +
            "{\"title\":\"Forza Horizon 4\",\"normalized\":\"forzahorizon4\",\"adaptiveTriggers\":true}" +
            "]}");
        var filledFromEmbedded = ManualFixGuide.BuildEntries(FindGame(staleCloud, "Forza Horizon 5"));
        Assert(filledFromEmbedded.Count == 1 && filledFromEmbedded[0].Note.Length > 0,
            "A catalogue without manual-fix notes did not fall back to the notes embedded in the Tray.");
        var cloudWins = ManualFixGuide.BuildEntries(FindGame(staleCloud, "Forza Motorsport 7"));
        Assert(cloudWins.Count == 1 && cloudWins[0].Note == "Cloud note.",
            "The embedded notes replaced the notes of the up-to-date catalogue.");
        Assert(ManualFixGuide.BuildEntries(FindGame(staleCloud, "Forza Horizon 4")).Count == 0,
            "A game that needs no manual fix received the embedded notes.");

        var translated = CreateGameList("{\"games\":[" +
            "{\"title\":\"Forza Horizon 5\",\"normalized\":\"forzahorizon5\"," +
            "\"adaptiveTriggers\":true,\"adaptiveTriggersManualFix\":true,\"requiresManualFix\":true," +
            "\"manualFixDetails\":{\"adaptiveTriggers\":{\"note\":\"Requires DSX.\"," +
            "\"links\":[{\"label\":\"Custom config\",\"url\":\"https://example.com/a\"," +
            "\"translations\":{\"fr\":\"Configuration personnalisée\"}}]," +
            "\"tools\":[\"DSX\"],\"translations\":{\"fr\":\"Nécessite DSX.\",\"ja\":\"\",\"es\":5}}}}" +
            "]}");
        var translatedGame = FindGame(translated, "Forza Horizon 5");
        var french = ManualFixGuide.BuildEntries(translatedGame, "fr")[0];
        Assert(french.Note == "Nécessite DSX." && french.Links[0].Label == "Configuration personnalisée",
            "The note and link label were not shown in the selected language.");
        foreach (var language in new[] { "en", "ja", "es", "xx", "", null })
        {
            var fallback = ManualFixGuide.BuildEntries(translatedGame, language)[0];
            Assert(fallback.Note == "Requires DSX." && fallback.Links[0].Label == "Custom config",
                "A missing or empty translation did not fall back to the English text: " + language);
        }
        Assert(translatedGame.AdaptiveTriggersFix.Links[0].Translations["FR"] == "Configuration personnalisée",
            "Translation languages are not matched case-insensitively.");
        Assert(translatedGame.AdaptiveTriggersFix.Translations.Count == 1,
            "Empty or non-text translations were kept.");

        Assert(ManualFixGuide.BuildEntries(FindGame(staleCloud, "Forza Horizon 5"), "fr")[0].Note
                   .StartsWith("Nécessite DSX et RacingDSX", StringComparison.Ordinal),
            "The embedded fallback lost the translations of the notes.");

        var native = FindGame(list, "Native");
        Assert(native.AdaptiveTriggersFix == null && ManualFixGuide.BuildEntries(native).Count == 0,
            "A malformed manualFixDetails value produced guide entries.");
        Assert(ManualFixGuide.BuildEntries(null).Count == 0, "A missing game produced guide entries.");

        foreach (var url in new[] { "", "   ", "ftp://example.com/a", "http://example.com/a", "file:///C:/a.exe",
                     "javascript:alert(1)", "https://user:secret@example.com/a", "https://", "not a url" })
            Assert(!ManualFixGuide.IsOpenableUrl(url), "An unsafe address was accepted: " + url);
        Assert(ManualFixGuide.IsOpenableUrl("https://github.com/cosmii02/RacingDSX"),
            "A plain HTTPS address was rejected.");
    }

    private static void TestPerGameApexProfileSettings()
    {
        var settings = new TraySettings();
        Assert(settings.GetApexProfileSlot("spiderman2") == 0,
            "A game without an override did not keep the current Apex profile.");

        settings.SetApexProfileSlot("SpiderMan2", 3);
        Assert(settings.GetApexProfileSlot("spiderman2") == 3,
            "The per-game Apex profile was not resolved case-insensitively.");

        settings.SetApexProfileSlot("SPIDERMAN2", 4);
        Assert(settings.GetApexProfileSlot("spiderman2") == 4 &&
               settings.ApexProfileSlots.Count == 1,
            "Updating a per-game Apex profile created a duplicate key.");

        settings.SetApexProfileSlot("spiderman2", 0);
        Assert(settings.GetApexProfileSlot("spiderman2") == 0 &&
               settings.ApexProfileSlots.Count == 0,
            "Keeping the current profile did not remove the per-game override.");

        bool rejected = false;
        try { settings.SetApexProfileSlot("spiderman2", 5); }
        catch (ArgumentOutOfRangeException) { rejected = true; }
        Assert(rejected, "An invalid Apex profile slot was accepted.");
    }

    private static void TestOnboardProfileSelectorLock()
    {
        foreach (var model in new[] { "apex4", "unsupported" })
            Assert(ControllerDetectionService.LocksOnboardProfile(model),
                "The onboard profile selector stayed editable for " + model + ".");
        foreach (var status in new[] { "apex5", "Apex5", "apex6", "APEX6", "disconnected", "unavailable", ControllerDetectionService.Ambiguous, "", null })
            Assert(!ControllerDetectionService.LocksOnboardProfile(status),
                "The onboard profile selector was locked for " + (status ?? "null") + ".");
        Assert(ControllerDetectionService.LocksOnboardProfile(
                ControllerDetectionService.ParseVerifiedModel("Verified: Apex 4 (k2)")) &&
            !ControllerDetectionService.LocksOnboardProfile(
                ControllerDetectionService.ParseVerifiedModel("Verified: Apex 5 (k5)")),
            "The identified model did not drive the onboard profile lock.");
    }

    private static void TestManualBridgeModeIsNotPersisted()
    {
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var settings = new TraySettings { ForcedProfile = "standard" };
        var json = serializer.Serialize(settings);

        Assert(!json.Contains("ForcedProfile"),
            "The transient manual bridge mode was persisted as a durable setting.");

        var migrated = serializer.Deserialize<TraySettings>(
            "{\"ForcedProfile\":\"standard\",\"AutoDetectGames\":true}");
        migrated.ResetTransientState();
        Assert(migrated != null && migrated.ForcedProfile == "none",
            "A legacy persisted manual bridge mode still disables detection after restart.");
    }

    private static void TestTrayStartupOptions()
    {
        var empty = TrayStartupOptions.Parse(new string[0]);
        Assert(!empty.ShowDashboard && !empty.ForceAlwaysOn && !TrayStartupOptions.Parse(null).ForceAlwaysOn,
            "Launching the Tray without arguments requested a startup option.");

        var forced = TrayStartupOptions.Parse(new[] { "--FORCE-ALWAYS-ON", "--unknown" });
        Assert(forced.ForceAlwaysOn && !forced.ShowDashboard,
            "--force-always-on was not recognized, or it also requested the dashboard.");
        var both = TrayStartupOptions.Parse(new[] { "--show", "--force-always-on" });
        Assert(both.ForceAlwaysOn && both.ShowDashboard,
            "--show and --force-always-on did not combine.");
        Assert(TrayStartupOptions.Parse(new[] { "--show" }).ShowDashboard &&
               !TrayStartupOptions.Parse(new[] { "--show" }).ForceAlwaysOn,
            "--show alone requested continuous activation.");
        foreach (var near in new[] { "force-always-on", "--force-always-on=1", "--force-always-on-now", "-force-always-on", "" })
            Assert(!TrayStartupOptions.Parse(new[] { near }).ForceAlwaysOn,
                "A near-miss argument enabled continuous activation: '" + near + "'.");

        var settings = new TraySettings();
        Assert(settings.ForcedProfile == "none", "A new settings object started in manual bridge mode.");
        empty.ApplyTo(settings);
        Assert(settings.ForcedProfile == "none", "Starting without the option changed the manual bridge mode.");
        forced.ApplyTo(null);
        forced.ApplyTo(settings);
        Assert(settings.ForcedProfile == "standard", "--force-always-on did not enable the manual bridge mode.");

        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var json = serializer.Serialize(settings);
        Assert(!json.Contains("ForcedProfile") && !json.Contains("standard"),
            "--force-always-on was written to the settings file.");
        var restarted = serializer.Deserialize<TraySettings>(json);
        restarted.ResetTransientState();
        Assert(restarted.ForcedProfile == "none",
            "--force-always-on survived a restart without the argument.");
    }

    private static void TestSharedBridgeArguments()
    {
        var cachedGames = CreateGameList("{\"games\":[{\"title\":\"Death Stranding 2: On the Beach\",\"profile\":\"standard\"}]}");
        Assert(FindGame(cachedGames, "Death Stranding 2: On the Beach").Profile == "death-stranding-2",
            "An older cached catalogue lost the embedded Death Stranding 2 remapping.");
        foreach (var explicitProfile in new[] { "none", "warframe" })
        {
            var explicitGames = CreateGameList("{\"games\":[{\"title\":\"Death Stranding 2: On the Beach\",\"profile\":\"" + explicitProfile + "\"}]}");
            Assert(FindGame(explicitGames, "Death Stranding 2: On the Beach").Profile == explicitProfile,
                "An explicit cloud profile was overwritten by the embedded fallback.");
        }
        var cachedAlanWake = CreateGameList("{\"games\":[{\"title\":\"Alan Wake II\",\"profile\":\"standard\"}]}");
        Assert(FindGame(cachedAlanWake, "Alan Wake II").Profile == "alan-wake-2",
            "An older cached catalogue lost the embedded Alan Wake 2 remapping.");
        Assert(BridgeArguments.Build("alan-wake-2", false, 12, false, 0).Contains(
            "--touchpad-profile alan-wake-2"),
            "The shared launcher dropped the Alan Wake 2 touchpad profile.");
        foreach (var profile in new[] { "spider-man-2", "miles-morales", "ghost-of-tsushima", "warframe", "death-stranding-2", "alan-wake-2" })
        {
            var entries = TouchpadRemapGuide.BuildEntries(profile);
            Assert(entries.Count > 0 && entries.All(entry =>
                    !string.IsNullOrEmpty(entry.ApexKey) && entry.ApexArguments.Length > 0 &&
                    !string.IsNullOrEmpty(entry.DualSenseKey)),
                "The touchpad remapping guide is incomplete for " + profile + ".");
        }
        Assert(TouchpadRemapGuide.BuildEntries("standard").Count == 0 && TouchpadRemapGuide.BuildEntries(null).Count == 0,
            "The touchpad remapping guide invented entries for a standard game.");
        var originalDeathStranding = CreateGameList("{\"games\":[{\"title\":\"Death Stranding Director's Cut\",\"profile\":\"standard\"}]}");
        Assert(FindGame(originalDeathStranding, "Death Stranding Director's Cut").Profile == "standard",
            "The Death Stranding 2 profile leaked into the original game.");
        Assert(BridgeArguments.Build("death-stranding-2", false, 12, false, 0).Contains(
            "--touchpad-profile death-stranding-2"),
            "The shared launcher dropped the Death Stranding 2 touchpad profile.");
        Assert(BridgeArguments.Build("Spider-Man-2", true, 12, true, 3) ==
               "bridge-triggers --touchpad-profile spider-man-2 --trigger-strength 100 --vibration-strength 100 " +
               "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100 --rumble " +
               "--haptic-threshold 12 --sync-lightbar --apex-profile 3",
            "The shared bridge argument contract changed for a complete profile.");
        Assert(BridgeArguments.Build("unknown", true, 999, false, 9) ==
               "bridge-triggers --touchpad-profile none --trigger-strength 100 --vibration-strength 100 " +
               "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100 --rumble --haptic-threshold 95",
            "The shared bridge argument contract did not normalize invalid settings.");
        Assert(BridgeArguments.Build(null, false, -1, false, 0) ==
               "bridge-triggers --touchpad-profile none --trigger-strength 100 --vibration-strength 100 " +
               "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100",
            "The shared bridge argument contract did not preserve minimal defaults.");
    }

    private static void TestGameExecutableSettings()
    {
        var settings = new TraySettings();
        string game;
        Assert(settings.TriggerStrengthPercent == 100 && settings.VibrationStrengthPercent == 100 &&
               settings.Apex4GyroStrengthPercent == 100 && settings.Apex4GyroYawStrengthPercent == 100,
            "Default strengths changed.");
        settings.SetGameExecutable("alpha", @"D:\Games\Alpha\game.EXE");
        Assert(settings.TryGetGameForExecutable(@"d:\games\alpha\GAME.exe", out game) && game == "alpha", "Configured absolute path did not resolve.");
        Assert(!settings.TryGetGameForExecutable(@"D:\Other\game.exe", out game), "Configured path matched by basename.");
        settings.SetGameExecutable("beta", @"D:\Games\Alpha\game.EXE");
        Assert(!settings.TryGetGameForExecutable(@"D:\Games\Alpha\game.exe", out game), "Ambiguous configured path resolved.");
        settings.SetGameExecutable("beta", null);
        Assert(settings.TryGetGameForExecutable(@"D:\Games\Alpha\game.exe", out game) && game == "alpha", "Clearing optional association failed.");
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.GetGameExecutable("ALPHA") == @"D:\Games\Alpha\game.EXE", "Executable did not survive serialization.");
        var legacy = serializer.Deserialize<TraySettings>("{\"AutoDetectGames\":true}");
        Assert(legacy.TriggerStrengthPercent == 100 && legacy.VibrationStrengthPercent == 100 &&
               legacy.Apex4GyroStrengthPercent == 100 && legacy.Apex4GyroYawStrengthPercent == 100,
            "Legacy settings lost default strengths.");
        Assert(BridgeArguments.Build("none", true, 20, false, 0, 35, 60, 175, 225).Contains(
            "--trigger-strength 35 --vibration-strength 60 --apex4-gyro-strength 175 --apex4-gyro-yaw-strength 225"),
            "Strength values did not reach engine arguments.");
        Assert(BridgeArguments.Build("none", true, 12, false, 0, 100, 150).Contains("--vibration-strength 150"),
            "Vibration amplification was clamped before reaching the engine.");
        Assert(BridgeArguments.Build("none", true, 12, false, 0, 100, 999).Contains("--vibration-strength 200"),
            "Vibration amplification was not bounded.");
        Assert(settings.ZoneEmulation && restored.ZoneEmulation && legacy.ZoneEmulation,
            "Trigger emulation is not enabled by default or in legacy settings.");
        Assert(!BridgeArguments.Build("none", true, 12, false, 0).Contains("--no-zone-emulation"),
            "Default arguments disabled the trigger emulation.");
        Assert(BridgeArguments.Build("none", true, 12, false, 0, zoneEmulation: false).Contains("--no-zone-emulation"),
            "Disabling the trigger emulation did not reach the engine.");
        Assert(BridgeArguments.Build("none", false, 12, true, 0, lightbarBrightnessPercent: 40).Contains("--sync-lightbar --lightbar-brightness 40"),
            "The lightbar brightness limit did not reach the engine.");
        Assert(!BridgeArguments.Build("none", false, 12, true, 0, lightbarBrightnessPercent: 100).Contains("--lightbar-brightness"),
            "The default lightbar brightness limit changed the launch arguments.");
        Assert(!BridgeArguments.Build("none", false, 12, false, 0, lightbarBrightnessPercent: 40).Contains("--lightbar-brightness"),
            "The lightbar brightness limit was passed without the lightbar sync.");
        Assert(BridgeArguments.Build("none", false, 12, true, 0, lightbarBrightnessPercent: -5).Contains("--lightbar-brightness 0") &&
            !BridgeArguments.Build("none", false, 12, true, 0, lightbarBrightnessPercent: 999).Contains("--lightbar-brightness"),
            "The lightbar brightness limit was not bounded.");
        settings.ZoneEmulation = false;
        var disabledEmulation = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(!disabledEmulation.ZoneEmulation, "Disabled trigger emulation did not survive serialization.");
    }

    private static void TestApex6Rate2kHzSetting()
    {
        const string prefix = "bridge-triggers --touchpad-profile none --trigger-strength 100 --vibration-strength 100 " +
                              "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100";
        Assert(BridgeArguments.Build("none", true, 12, false, 0) == prefix + " --rumble --haptic-threshold 12",
            "The existing rumble arguments changed.");
        Assert(BridgeArguments.Build("none", true, 12, false, 0, apex6Rate2k: false) ==
               BridgeArguments.Build("none", true, 12, false, 0),
            "A disabled 2 kHz rate changed the launch arguments.");
        Assert(BridgeArguments.Build("none", true, 12, false, 0, apex6Rate2k: true) ==
               prefix + " --rumble --haptic-threshold 12 --apex6-rate 2000",
            "The 2 kHz rate did not follow the rumble arguments.");
        Assert(BridgeArguments.Build("none", false, 12, false, 0, apex6Rate2k: true) ==
               prefix + " --apex6-rate 2000",
            "The 2 kHz rate depended on the rumble switch.");
        Assert(BridgeArguments.Build("Spider-Man-2", true, 12, true, 3, 100, 100, 100, 100, false, 40, true) ==
               "bridge-triggers --touchpad-profile spider-man-2 --trigger-strength 100 --vibration-strength 100 " +
               "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100 --rumble --haptic-threshold 12 " +
               "--apex6-rate 2000 --sync-lightbar --lightbar-brightness 40 --no-zone-emulation --apex-profile 3",
            "The 2 kHz rate moved relative to the other optional arguments.");
        Assert(BridgeArguments.Build("Spider-Man-2", true, 12, true, 3, 100, 100, 100, 100, false, 40, false) ==
               "bridge-triggers --touchpad-profile spider-man-2 --trigger-strength 100 --vibration-strength 100 " +
               "--apex4-gyro-strength 100 --apex4-gyro-yaw-strength 100 --rumble --haptic-threshold 12 " +
               "--sync-lightbar --lightbar-brightness 40 --no-zone-emulation --apex-profile 3",
            "Existing optional arguments changed when the 2 kHz rate is off.");

        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var settings = new TraySettings();
        Assert(!settings.Apex6Rate2kHz, "The 2 kHz rate is not disabled by default.");
        Assert(!serializer.Deserialize<TraySettings>("{\"AutoDetectGames\":true}").Apex6Rate2kHz &&
               !serializer.Deserialize<TraySettings>("{\"ZoneEmulation\":false}").Apex6Rate2kHz &&
               !serializer.Deserialize<TraySettings>("{}").Apex6Rate2kHz,
            "A legacy settings file enabled the 2 kHz rate.");
        Assert(serializer.Serialize(settings).Contains("\"Apex6Rate2kHz\":false"),
            "The 2 kHz rate is not part of the persisted settings.");
        settings.Apex6Rate2kHz = true;
        var restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.Apex6Rate2kHz, "The enabled 2 kHz rate did not survive serialization.");
        Assert(restored.ZoneEmulation && restored.EnableRumble && restored.AutoDetectGames,
            "Enabling the 2 kHz rate disturbed other settings.");
        Assert(!new TraySettings().Apex6Rate2kHz, "Default settings kept the 2 kHz rate enabled.");
        Assert(serializer.Deserialize<TraySettings>("{\"Apex6Rate2kHz\":true}").Apex6Rate2kHz,
            "A persisted 2 kHz rate was ignored.");

        var builder = typeof(EngineSessionManager).GetMethod("BuildArguments", BindingFlags.Static | BindingFlags.NonPublic);
        var enabled = (string)builder.Invoke(null, new object[] { "standard", settings, 2, "Alpha" });
        Assert(enabled.Contains(" --rumble") == false && enabled.Contains(" --apex6-rate 2000 --apex-profile 2") &&
               enabled.IndexOf("--apex6-rate 2000", StringComparison.Ordinal) <
               enabled.IndexOf("--controller-calibration", StringComparison.Ordinal),
            "The session launch did not pass the enabled 2 kHz rate.");
        settings.Apex6Rate2kHz = false;
        var disabled = (string)builder.Invoke(null, new object[] { "standard", settings, 2, "Alpha" });
        Assert(!disabled.Contains("--apex6-rate"), "The session launch passed a disabled 2 kHz rate.");
        var withoutSettings = (string)builder.Invoke(null, new object[] { "standard", null, 2, "Alpha" });
        Assert(!withoutSettings.Contains("--apex6-rate"), "A session without settings passed the 2 kHz rate.");
        Assert(enabled.Replace(" --apex6-rate 2000", string.Empty) == disabled,
            "Only the 2 kHz rate may differ between enabled and disabled session launches.");
    }

    private static void TestApex6Rate2kHzLocalization()
    {
        var root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", ".."));
        var directory = Path.Combine(root, "ApexSenseBridgeTray", "Resources", "Languages");
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer { MaxJsonLength = int.MaxValue };
        var english = serializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(directory, "en.json"), Encoding.UTF8));
        Assert((string)english["Loc_Apex6Rate2kHz"] == "2 kHz grip haptics (experimental)",
            "The English 2 kHz title changed.");
        Assert(((string)english["Loc_Apex6Rate2kHzHint"]).StartsWith("Sends APEX 6 Pro grip haptics at 2000 samples per second instead of 1000.", StringComparison.Ordinal),
            "The English 2 kHz description changed.");
        foreach (var language in new[] { "en", "es", "fr", "ja", "ko", "pt", "ru", "vi", "zh" })
        {
            var entries = serializer.Deserialize<Dictionary<string, object>>(
                File.ReadAllText(Path.Combine(directory, language + ".json"), Encoding.UTF8));
            object titleValue, hintValue;
            Assert(entries.TryGetValue("Loc_Apex6Rate2kHz", out titleValue) && !string.IsNullOrWhiteSpace(titleValue as string),
                language + " has no 2 kHz title.");
            Assert(entries.TryGetValue("Loc_Apex6Rate2kHzHint", out hintValue) && !string.IsNullOrWhiteSpace(hintValue as string),
                language + " has no 2 kHz description.");
            var title = (string)titleValue;
            var hint = (string)hintValue;
            Assert(title.Contains("2 kHz"), language + " translated the 2 kHz unit in the title.");
            Assert(hint.Contains("APEX 6 Pro") && hint.Contains("libVIIPER"),
                language + " translated a product name in the 2 kHz description.");
            Assert(language == "en" || (title != (string)english["Loc_Apex6Rate2kHz"] && hint != (string)english["Loc_Apex6Rate2kHzHint"]),
                language + " reuses the English 2 kHz text.");
            Assert(!hint.Contains("\n") && !hint.Contains("\r"), language + " has a multi-line 2 kHz description.");
        }
        var window = File.ReadAllText(Path.Combine(root, "ApexSenseBridgeTray", "GameListWindow.xaml"), Encoding.UTF8);
        Assert(window.Contains("{DynamicResource Loc_Apex6Rate2kHz}") && window.Contains("{DynamicResource Loc_Apex6Rate2kHzHint}"),
            "The settings window does not use the 2 kHz texts.");
    }

    private static void TestReportRelayClient()
    {
        var probe = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
        probe.Start();
        int port = ((System.Net.IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop();
        string endpoint = "http://localhost:" + port + "/v1/report";

        string bigImage = Path.Combine(Path.GetTempPath(), "asb-relay-big-" + Guid.NewGuid().ToString("N") + ".png");
        string smallImage = Path.Combine(Path.GetTempPath(), "asb-relay-shot-" + Guid.NewGuid().ToString("N") + ".png");
        File.WriteAllBytes(bigImage, new byte[9 * 1024 * 1024]);
        File.WriteAllBytes(smallImage, new byte[] { 0x89, 0x50, 0x4e, 0x47, 1, 2, 3, 4 });

        var listener = new System.Net.HttpListener();
        listener.Prefixes.Add("http://localhost:" + port + "/");
        listener.Start();
        string clientHeader = null;
        string firstBody = null;
        var server = System.Threading.Tasks.Task.Run(() =>
        {
            for (int i = 0; i < 2; i++)
            {
                var context = listener.GetContext();
                string body;
                using (var reader = new StreamReader(context.Request.InputStream, Encoding.UTF8))
                    body = reader.ReadToEnd();
                if (i == 0) { clientHeader = context.Request.Headers["X-ASB-Client"]; firstBody = body; }
                byte[] payload = Encoding.UTF8.GetBytes(i == 0
                    ? "{\"ok\":true,\"number\":7,\"url\":\"https://github.com/ReynArts/ApexSenseBridge/issues/7\"}"
                    : "{\"ok\":false,\"error\":\"rate_limited\"}");
                context.Response.StatusCode = i == 0 ? 200 : 429;
                context.Response.ContentType = "application/json";
                context.Response.OutputStream.Write(payload, 0, payload.Length);
                context.Response.Close();
            }
        });

        try
        {
            var sent = ReportRelayClient.Send(endpoint, "feature", "Per-game profiles", "A profile per game.",
                null, null, "ASB-TEST", null, new[] { smallImage, bigImage });
            Assert(sent.Success && sent.Url == "https://github.com/ReynArts/ApexSenseBridge/issues/7",
                "The relay answer was not turned into an issue address: " + sent.Error);
            Assert(sent.SkippedAttachments, "An image over the relay limit was not reported as skipped.");
            Assert(clientHeader != null && clientHeader.StartsWith("tray/", StringComparison.Ordinal),
                "The relay request did not identify the Tray.");
            Assert(firstBody.Contains("name=category") && firstBody.Contains("feature") &&
                   firstBody.Contains("Per-game profiles") && firstBody.Contains("name=screenshots"),
                "The relay request lost its report fields.");
            Assert(!firstBody.Contains(Path.GetFileName(bigImage)), "An image over the relay limit was uploaded.");

            var refused = ReportRelayClient.Send(endpoint, "bug", "Title", "Text", null, null, "ASB-TEST", null, null);
            Assert(!refused.Success && refused.Error == "rate_limited", "A refused report was reported as sent.");
            server.Wait(5000);
        }
        finally
        {
            listener.Close();
            File.Delete(bigImage);
            File.Delete(smallImage);
        }

        var unreachable = ReportRelayClient.Send(endpoint, "bug", "Title", "Text", null, null, "ASB-TEST", null, null);
        Assert(!unreachable.Success && !string.IsNullOrEmpty(unreachable.Error),
            "An unreachable relay was reported as sent.");
    }

    private static void TestControllerCalibrations()
    {
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var settings = serializer.Deserialize<TraySettings>(
            "{\"TriggerStrengthPercent\":35,\"VibrationStrengthPercent\":60,\"HapticThresholdPercent\":24," +
            "\"EnableRumble\":false,\"SyncLightbar\":true,\"Apex4GyroStrengthPercent\":175,\"Apex4GyroYawStrengthPercent\":225}");
        foreach (var status in new[] { null, "", "disconnected", "unavailable", "unsupported", "APEX5" })
            Assert(settings.GetControllerCalibration(status) == null, "An unverified model unlocked calibration.");
        var a4 = settings.GetControllerCalibration("apex4");
        var a5 = settings.GetControllerCalibration("apex5");
        var a6 = settings.GetControllerCalibration("apex6");
        Assert(a4.TriggerStrengthPercent == 35 && a5.VibrationStrengthPercent == 60 && !a6.EnableRumble,
            "Migration lost existing common preferences.");
        Assert(a4.HapticThresholdPercent == 24 && a5.HapticThresholdPercent == 24 && a6.HapticThresholdPercent == 0,
            "Threshold migration crossed the APEX 6 hardware boundary.");
        Assert(!a4.SyncLightbar && a5.SyncLightbar && !a6.SyncLightbar,
            "RGB leaked to an unsupported controller.");
        Assert(a4.GyroStrengthPercent == 175 && a4.GyroYawStrengthPercent == 225 &&
            a5.GyroStrengthPercent == 100 && a6.GyroYawStrengthPercent == 100, "Gyro tuning crossed model boundary.");
        a6.TriggerStrengthPercent = 80;
        a6.VibrationStrengthPercent = 90;
        a6.EnableRumble = true;
        Assert(a4.TriggerStrengthPercent == 35 && a5.VibrationStrengthPercent == 60 &&
            settings.TriggerStrengthPercent == 35, "Editing APEX 6 changed another profile or migration source.");
        var restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.GetControllerCalibration("apex6").TriggerStrengthPercent == 80 &&
            restored.GetControllerCalibration("apex5").TriggerStrengthPercent == 35, "Per-model tuning did not survive serialization.");
        a4.TriggerStrengthPercent = -10;
        a4.GyroStrengthPercent = 900;
        a5.VibrationStrengthPercent = 999;
        a6.HapticThresholdPercent = 95;
        Assert(a4.LightbarBrightnessPercent == 100 && a5.LightbarBrightnessPercent == 100 && a6.LightbarBrightnessPercent == 100,
            "The lightbar brightness limit did not default to 100.");
        a4.LightbarBrightnessPercent = 40;
        a5.LightbarBrightnessPercent = 40;
        a6.LightbarBrightnessPercent = 999;
        var arguments = settings.BuildControllerCalibrationArguments();
        Assert(arguments.Contains("--controller-calibration apex4:0:60:24:0:0:400:225:100") &&
            arguments.Contains("--controller-calibration apex5:35:200:24:0:1:100:100:40") &&
            arguments.Contains("--controller-calibration apex6:80:90:0:1:0:100:100:100"),
            "Controller profiles were not bounded/serialized to the native protocol.");
        a6.SyncLightbar = true;
        a6.LightbarBrightnessPercent = 40;
        a4.SyncLightbar = true;
        var rgbArguments = settings.BuildControllerCalibrationArguments();
        Assert(rgbArguments.Contains("--controller-calibration apex6:80:90:0:1:1:100:100:40") &&
            rgbArguments.Contains("--controller-calibration apex4:0:60:24:0:0:400:225:100"),
            "RGB sync was not enabled for the APEX 6 Pro only, or it leaked to the APEX 4.");
        a6.SyncLightbar = false;
        a6.LightbarBrightnessPercent = 100;
        a4.SyncLightbar = false;
        Assert(a4.LightbarBrightnessPercent == 100 && a5.LightbarBrightnessPercent == 40 && a6.LightbarBrightnessPercent == 100,
            "The lightbar brightness limit crossed the APEX 5 boundary.");
        a5.LightbarBrightnessPercent = -5;
        settings.BuildControllerCalibrationArguments();
        Assert(a5.LightbarBrightnessPercent == 0, "The lightbar brightness limit was not bounded.");
        a5.LightbarBrightnessPercent = 40;
        Assert(serializer.Deserialize<TraySettings>(serializer.Serialize(settings))
                .GetControllerCalibration("apex5").LightbarBrightnessPercent == 40,
            "The lightbar brightness limit did not survive serialization.");
        Assert(serializer.Deserialize<TraySettings>(
                "{\"ControllerCalibrations\":{\"apex5\":{\"SyncLightbar\":true}}}")
                .GetControllerCalibration("apex5").LightbarBrightnessPercent == 100,
            "A settings file without the brightness limit did not keep the full brightness.");
        a4.VibrationStrengthPercent = 150;
        a5.VibrationStrengthPercent = 175;
        a6.VibrationStrengthPercent = 200;
        settings.BuildControllerCalibrationArguments();
        Assert(a4.VibrationStrengthPercent == 150 && a5.VibrationStrengthPercent == 175 &&
            a6.VibrationStrengthPercent == 100, "Conventional-motor amplification crossed the APEX 6 boundary.");
        restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.GetControllerCalibration("apex4").VibrationStrengthPercent == 150 &&
            restored.GetControllerCalibration("apex5").VibrationStrengthPercent == 175,
            "Model-specific amplification did not survive persistence.");
        arguments = settings.BuildControllerCalibrationArguments();
        var builder = typeof(EngineSessionManager).GetMethod("BuildArguments", BindingFlags.Static | BindingFlags.NonPublic);
        var sessionArguments = (string)builder.Invoke(null, new object[] { "standard", settings, 3, "Alpha" });
        Assert(sessionArguments.EndsWith(arguments, StringComparison.Ordinal) &&
            !sessionArguments.Contains("--sync-lightbar") && !sessionArguments.Contains("--haptic-threshold"),
            "Session launch used legacy global tuning instead of verified-model selection.");
        Assert(settings.GetGameTuning("alpha") == null, "A game had tuning it was never given.");
        settings.SetGameTuning("alpha", "Alpha", new GameTuning
        {
            TriggerStrengthPercent = 55, VibrationStrengthPercent = 160, HapticThresholdPercent = 40
        });
        var stored = settings.GetGameTuning("ALPHA");
        Assert(stored != null && stored.TriggerStrengthPercent == 55 && stored.VibrationStrengthPercent == 160 &&
            stored.HapticThresholdPercent == 40 && settings.GetGameTuning("Alpha") != null,
            "The per-game tuning was not stored under both the normalized name and the title.");
        sessionArguments = (string)builder.Invoke(null, new object[] { "standard", settings, 3, "Alpha" });
        Assert(sessionArguments.Contains("--controller-calibration apex4:55:160:40:0:0:400:225") &&
            sessionArguments.Contains("--controller-calibration apex5:55:160:40:0:1:100:100") &&
            sessionArguments.Contains("--controller-calibration apex6:55:100:0:1:0:100:100"),
            "The per-game tuning did not replace the three settings within each model's limits.");
        sessionArguments = (string)builder.Invoke(null, new object[] { "standard", settings, 3, "Beta" });
        Assert(sessionArguments.EndsWith(arguments, StringComparison.Ordinal),
            "A per-game tuning leaked to another game.");
        restored = serializer.Deserialize<TraySettings>(serializer.Serialize(settings));
        Assert(restored.GetGameTuning("alpha") != null && restored.GetGameTuning("alpha").HapticThresholdPercent == 40,
            "The per-game tuning did not survive serialization.");
        settings.SetGameTuning("alpha", "Alpha", new GameTuning
        {
            TriggerStrengthPercent = 500, VibrationStrengthPercent = 500, HapticThresholdPercent = 500
        });
        stored = settings.GetGameTuning("alpha");
        Assert(stored.TriggerStrengthPercent == 100 && stored.VibrationStrengthPercent == 200 && stored.HapticThresholdPercent == 95,
            "The per-game tuning was not bounded.");
        settings.SetGameTuning("alpha", "Alpha", null);
        Assert(settings.GetGameTuning("alpha") == null && settings.GetGameTuning("Alpha") == null &&
            settings.GameTunings.Count == 0, "Clearing the per-game tuning left an override behind.");
        Assert(ControllerDetectionService.ParseVerifiedModel("Verified: Apex 4 (k4)") == "apex4" &&
            ControllerDetectionService.ParseVerifiedModel("Verified: Apex 5 (k5)") == "apex5" &&
            ControllerDetectionService.ParseVerifiedModel("Verified: Apex 6 Pro (k6)") == "apex6",
            "Verified controller identities did not map to their profiles.");
        Assert(ControllerDetectionService.ParseVerifiedModel("Product: Apex 6 Pro") == "unsupported",
            "An unverified marketing name unlocked calibration.");
        string candidate;
        Assert(ControllerDetectionService.ParseCandidate("Found 2 candidate(s):\n[0] Apex 5\n[1] Apex 6", out candidate) == "unavailable" && candidate == "",
            "Multiple controller interfaces were accepted for calibration.");
        Assert(ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\n[0] Apex 5\nPath A", out candidate) == null,
            "A unique candidate was rejected.");
        var first = candidate;
        ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\n[0] Apex 5\nPath B", out candidate);
        Assert(candidate != first, "Hot swap of the vendor path left a stale identity fingerprint.");
        const string apex4Block = "    VID:PID      04B4:2412\r\n    Usage page   FFA0  usage 0001\r\n" +
            "    Reports      input=33 output=33 bytes\r\n    Path         \\\\?\\hid#vid_04b4&pid_2412&mi_02#7&1a2b&0&0000\r\n";
        Assert(ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\r\n\r\n[0] Flydigi APEX 4\r\n" + apex4Block, out first) == null &&
            ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\r\n\r\n[0] Flydigi controller\r\n" + apex4Block.Replace("      ", " "), out candidate) == null &&
            candidate == first && first.Contains("pid_2412&mi_02"),
            "A transient HID product string or spacing change altered the controller fingerprint.");
        ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\r\n\r\n[0] Flydigi APEX 4\r\n" + apex4Block.Replace("output=33", "output=64"), out candidate);
        Assert(candidate != first, "A changed vendor report layout kept the old fingerprint.");
        Assert(ControllerDetectionService.ParseCandidate("Found 2 candidate(s):\r\n\r\n[0] Flydigi APEX 4\r\n" + apex4Block +
            "[1] Flydigi APEX 4\r\n" + apex4Block.Replace("&0&0000", "&0&0001"), out candidate) == ControllerDetectionService.Ambiguous &&
            candidate.StartsWith(first + "\n\n", StringComparison.Ordinal),
            "Two vendor interfaces were not reported as ambiguous with their fingerprints.");
        Assert(ControllerDetectionService.ParseCandidate("Found 2 candidate(s):\r\n\r\n[0] Flydigi APEX 4\r\n" + apex4Block, out candidate) == "unavailable" &&
            candidate == "", "A truncated candidate list was accepted.");
        Assert(ControllerDetectionService.ParseActiveModel(new BridgeSession.SessionInfo
            { Phase = SessionPhase.Ready, Controller = "Apex 6 Pro (k6)" }) == "apex6",
            "The active engine's verified model was not reused without a competing HID reader.");
        foreach (var phase in new[] { SessionPhase.Starting, SessionPhase.Failed, SessionPhase.Stopped })
            Assert(ControllerDetectionService.ParseActiveModel(new BridgeSession.SessionInfo
                { Phase = phase, Controller = "Apex 5 (k5)" }) == "unavailable",
                "A stale controller identity survived an inactive session phase.");
    }

    private static string FabricatedCandidate(int index, string product, string vidPid, int output, string path)
    {
        return "[" + index + "] " + product + "\r\n" +
               "    VID:PID      " + vidPid + "\r\n" +
               "    Usage page   0xFFA0  usage 0x0001\r\n" +
               "    Reports      input=33 output=" + output + " bytes\r\n" +
               "    Path         " + path + "\r\n";
    }

    private static void TestMultipleControllerDetection()
    {
        string dongleA = FabricatedCandidate(0, "Flydigi APEX 5", "0x37D7:0x2401", 33, @"\\?\hid#vid_37d7&pid_2401&mi_02&col03#8&1111&0&0002#{4d1e55b2}");
        string dongleB = FabricatedCandidate(1, "Flydigi APEX 5", "0x37D7:0x2401", 33, @"\\?\hid#vid_37d7&pid_2401&mi_02&col03#8&2222&0&0002#{4d1e55b2}");
        string wired = FabricatedCandidate(1, "Flydigi controller", "0x37D7:0x2402", 33, @"\\?\hid#vid_37d7&pid_2402&mi_02&col03#8&0aaa&0&0002#{4d1e55b2}");
        string apex4 = FabricatedCandidate(0, "Flydigi APEX 4", "0x04B4:0x2412", 64, @"\\?\hid#vid_04b4&pid_2412&mi_02#7&1a2b&0&0000#{4d1e55b2}");
        string third = FabricatedCandidate(2, "Flydigi APEX 5", "0x37D7:0x2401", 33, @"\\?\hid#vid_37d7&pid_2401&mi_02&col03#8&3333&0&0002#{4d1e55b2}");
        string candidate;
        Assert(ControllerDetectionService.ParseCandidate("No APEX 4/5/6 vendor HID interface found.\r\n", out candidate) == "unavailable" && candidate == "",
            "An empty controller list was not rejected.");
        Assert(ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\r\n\r\n" + dongleA, out candidate) == null && candidate.Contains("&1111&"),
            "A single APEX 5 was not accepted.");
        var fabricated = new[]
        {
            new { Output = "Found 2 candidate(s):\r\n\r\n" + dongleA + dongleB, Count = 2, Name = "two APEX 5 on two dongles", Models = new[] { "apex5", "apex5" } },
            new { Output = "Found 2 candidate(s):\r\n\r\n" + dongleA + wired, Count = 2, Name = "APEX 5 on dongle plus APEX 5 wired", Models = new[] { "apex5", "apex5" } },
            new { Output = "Found 2 candidate(s):\r\n\r\n" + apex4 + dongleA.Replace("[0]", "[1]"), Count = 2, Name = "APEX 4 plus APEX 5", Models = new[] { "apex4", "apex5" } },
            new { Output = "Found 3 candidate(s):\r\n\r\n" + dongleA + dongleB + third, Count = 3, Name = "three APEX 5", Models = new[] { "apex5", "apex5", "apex5" } },
            new { Output = "Found 2 candidate(s):\r\n\r\n" + dongleA + dongleA.Replace("[0]", "[1]"), Count = 2, Name = "duplicate device path", Models = new[] { "apex5", "apex5" } }
        };
        foreach (var sample in fabricated)
        {
            Assert(ControllerDetectionService.ParseCandidate(sample.Output, out candidate) == ControllerDetectionService.Ambiguous &&
                candidate.Split(new[] { "\n\n" }, StringSplitOptions.None).Length == sample.Count,
                "Multiple controllers were not reported as ambiguous: " + sample.Name + ".");

            string fingerprints = candidate, published = null;
            int identityReads = 0, changes = 0;
            BridgeSession.SessionInfo active = null;
            var detector = new ControllerDetectionService(
                delegate(out string fingerprint) { fingerprint = fingerprints; return ControllerDetectionService.Ambiguous; },
                (int? index) => { identityReads++; return sample.Models[index.Value]; },
                () => active, () => false, () => 0, false);
            detector.StatusChanged += status => published = status;
            detector.ControllersChanged += () => changes++;
            try
            {
                var keys = ControllerDetectionService.SplitCandidates(candidate).Select(ControllerDetectionService.KeyOf).ToArray();
                detector.Scan(null);
                Assert(published == sample.Models[0] && detector.CandidateCount == sample.Count && identityReads == keys.Distinct().Count(),
                    "Several controllers were not identified one by one: " + sample.Name + ".");
                Assert(detector.Controllers.Count == sample.Count && detector.SelectedController.Key == keys[0] && changes > 0,
                    "The detected controllers were not exposed with the first one selected: " + sample.Name + ".");
                int reads = identityReads;
                detector.Scan(null);
                Assert(identityReads == reads, "A verified set of controllers was identified again on every scan: " + sample.Name + ".");

                int pick = Array.IndexOf(keys, keys[sample.Count - 1]);
                detector.Select(keys[sample.Count - 1]);
                Assert(published == sample.Models[pick] && detector.SelectedController.Key == keys[pick] && identityReads == reads,
                    "Choosing a controller did not publish its model without new identity reads: " + sample.Name + ".");

                active = new BridgeSession.SessionInfo { Phase = SessionPhase.Ready, Controller = "Apex 5 (k5, DeviceType 5)" };
                detector.Scan(null);
                Assert(published == "apex5" && identityReads == reads,
                    "An active bridge with several controllers did not report the controller the engine verified: " + sample.Name + ".");
                active = null;
                detector.Scan(null);
                Assert(published == sample.Models[pick] && identityReads == reads,
                    "Stopping the bridge did not restore the chosen controller: " + sample.Name + ".");
            }
            finally { detector.Dispose(); }
        }

        int count;
        Assert(ControllerDetectionService.TryParseSeveralControllers(
            "APEX identity check failed: Several APEX controllers are connected (2 vendor interfaces). ApexSenseBridge drives one controller at a time.", out count) && count == 2,
            "The engine's several-controllers failure was not recognised.");
        Assert(ControllerDetectionService.TryParseSeveralControllers(
            "Identity check failed: Several APEX controllers are connected (3 vendor interfaces).", out count) && count == 3,
            "The diagnostic several-controllers failure did not expose its count.");
        Assert(!ControllerDetectionService.TryParseSeveralControllers("No APEX 4/5/6 vendor HID interface found.", out count) &&
            !ControllerDetectionService.TryParseSeveralControllers(null, out count),
            "An unrelated engine failure was reported as several controllers.");
    }

    private static void TestControllerSelection()
    {
        string pathA = @"\\?\hid#vid_37d7&pid_2403&mi_02&col03#8&aaaa&0&0002#{4d1e55b2}";
        string pathB = @"\\?\hid#vid_37d7&pid_2401&mi_02&col03#8&bbbb&0&0002#{4d1e55b2}";
        string a0 = FabricatedCandidate(0, "Flydigi APEX 6 Pro", "0x37D7:0x2403", 64, pathA);
        string b1 = FabricatedCandidate(1, "Flydigi APEX 5", "0x37D7:0x2401", 33, pathB);
        string b0 = FabricatedCandidate(0, "Flydigi APEX 5", "0x37D7:0x2401", 33, pathB);
        string a1 = FabricatedCandidate(1, "Flydigi APEX 6 Pro", "0x37D7:0x2403", 64, pathA);
        string single, both, swapped;
        string singleState = ControllerDetectionService.ParseCandidate("Found 1 candidate(s):\r\n\r\n" + a0, out single);
        string bothState = ControllerDetectionService.ParseCandidate("Found 2 candidate(s):\r\n\r\n" + a0 + b1, out both);
        string swappedState = ControllerDetectionService.ParseCandidate("Found 2 candidate(s):\r\n\r\n" + b0 + a1, out swapped);
        Assert(singleState == null && bothState == ControllerDetectionService.Ambiguous && swappedState == ControllerDetectionService.Ambiguous,
            "The fabricated controller lists were not parsed.");
        var keys = ControllerDetectionService.SplitCandidates(both).Select(ControllerDetectionService.KeyOf).ToArray();
        Assert(keys.Length == 2 && keys[0] == pathA && keys[1] == pathB,
            "The controller identity was not the HID path.");

        Func<string, string, ControllerDetectionService.CandidateReader> reader = (listing, status) =>
            delegate(out string fingerprint) { fingerprint = listing; return status; };
        var ambiguous = ControllerDetectionService.Ambiguous;
        Assert(ControllerSelection.DeviceIndexArgument(keys[0], reader(both, ambiguous)) == " 0" &&
               ControllerSelection.DeviceIndexArgument(keys[1], reader(both, ambiguous)) == " 1" &&
               ControllerSelection.DeviceIndexArgument(keys[1], reader(swapped, ambiguous)) == " 0",
            "The chosen controller did not map to its current list position.");
        Assert(ControllerSelection.DeviceIndexArgument(null, reader(both, ambiguous)) == "" &&
               ControllerSelection.DeviceIndexArgument("", reader(both, ambiguous)) == "" &&
               ControllerSelection.DeviceIndexArgument("missing", reader(both, ambiguous)) == "" &&
               ControllerSelection.DeviceIndexArgument(keys[1], reader(single, null)) == "" &&
               ControllerSelection.DeviceIndexArgument(keys[1], reader("", "disconnected")) == "" &&
               ControllerSelection.DeviceIndexArgument(keys[1], reader("", "unavailable")) == "",
            "A missing, empty or single-controller selection produced a device index.");

        string listingNow = single, state = null, published = null;
        int reads = 0, changes = 0;
        long now = 0;
        bool swapOnce = false;
        var answers = new Dictionary<string, string> { { pathA, "apex6" }, { pathB, "apex5" } };
        var detector = new ControllerDetectionService(
            delegate(out string fingerprint) { fingerprint = listingNow; return state; },
            (int? index) =>
            {
                reads++;
                string key = ControllerDetectionService.KeyOf(
                    ControllerDetectionService.SplitCandidates(listingNow)[index.HasValue ? index.Value : 0]);
                string model = answers[key];
                if (swapOnce) { swapOnce = false; listingNow = swapped; }
                return model;
            },
            () => null, () => false, () => now, false);
        detector.StatusChanged += status => published = status;
        detector.ControllersChanged += () => changes++;
        Action<int> scan = n => { for (int i = 0; i < n; i++) detector.Scan(null); };
        try
        {
            scan(1);
            Assert(published == "apex6" && reads == 1 && detector.Controllers.Count == 1 && detector.Controllers[0].Model == "apex6",
                "A single controller was not identified and listed.");

            listingNow = both; state = ambiguous;
            changes = 0;
            scan(1);
            Assert(published == "apex6" && reads == 2 && detector.CandidateCount == 2 && changes > 0 &&
                   detector.Controllers.Count == 2 && detector.Controllers[0].Model == "apex6" && detector.Controllers[1].Model == "apex5" &&
                   detector.SelectedController.Key == pathA,
                "A second controller re-identified the first or hid the already selected one.");

            changes = 0;
            detector.Select(pathB);
            Assert(published == "apex5" && reads == 2 && changes > 0 && detector.SelectedController.Key == pathB,
                "Choosing the second controller did not publish its model.");
            scan(2);
            Assert(published == "apex5" && reads == 2 && detector.SelectedController.Key == pathB,
                "The chosen controller was not kept across scans.");

            now += ControllerDetectionService.ReverifyMilliseconds;
            scan(1);
            Assert(published == "apex5" && reads == 4, "Both controllers were not verified again after the cache expired.");

            now += ControllerDetectionService.ReverifyMilliseconds;
            answers[pathB] = "unavailable";
            scan(ControllerDetectionService.FailureScans - 1);
            Assert(published == "apex5" && detector.Controllers[1].Model == "apex5",
                "A chosen controller was downgraded before the failure threshold.");
            scan(1);
            Assert(published == "unavailable" && detector.Controllers[1].Model == "unavailable" &&
                   detector.Controllers[0].Model == "apex6" && detector.SelectedController.Key == pathB,
                "An unresponsive chosen controller did not lock its settings while the other stayed verified.");
            answers[pathB] = "apex5";
            scan(1);
            Assert(published == "apex5" && detector.Controllers[1].Model == "apex5", "A woken controller was not verified again.");

            detector.Select("missing");
            Assert(published == "apex6" && detector.SelectedController.Key == pathA,
                "A missing choice did not fall back to the first verified controller.");
            detector.Select(pathB);
            Assert(published == "apex5", "The choice could not be restored.");

            now += ControllerDetectionService.ReverifyMilliseconds;
            swapOnce = true;
            int before = reads;
            scan(1);
            Assert(published == "apex5" && reads == before + 2 && detector.SelectedController.Key == pathB,
                "A list that changed during identification altered the published state.");
            listingNow = swapped;
            scan(1);
            Assert(reads == before + 4 && published == "apex5" && detector.SelectedController.Key == pathB &&
                   detector.Controllers[0].Key == pathB && detector.Controllers[1].Key == pathA,
                "Results gathered against a changed list were cached.");

            listingNow = single; state = null;
            scan(1);
            Assert(published == "apex6" && detector.Controllers.Count == 1,
                "A single remaining controller did not become the active one.");
            Assert(detector.CandidateCount == 1, "The candidate count did not follow the list.");

            listingNow = both; state = ambiguous;
            detector.Select(pathB);
            scan(1);
            Assert(published == "apex5", "The remembered choice was lost after a controller left and returned.");
            state = "disconnected"; listingNow = string.Empty;
            scan(ControllerDetectionService.DisconnectScans - 1);
            Assert(published == "apex5", "A transient list failure flapped the chosen controller.");
            scan(1);
            Assert(published == "disconnected" && detector.Controllers.Count == 0, "Unplugging every controller was not reported.");
        }
        finally { detector.Dispose(); }
    }

    private static void TestControllerCalibrationDetection()
    {
        string candidate = "path-a", candidateState = null, model = "apex5", lastPublished = null, publishedAtIdentify = null;
        int identityReads = 0;
        long now = 0;
        bool owner = false, changeDuringIdentification = false;
        BridgeSession.SessionInfo active = null;
        var published = new List<string>();
        var detector = new ControllerDetectionService(
            delegate(out string fingerprint) { fingerprint = candidate; return candidateState; },
            () =>
            {
                identityReads++;
                publishedAtIdentify = lastPublished;
                if (changeDuringIdentification) candidate = "path-c";
                return model;
            },
            () => active, () => owner, () => now, false);
        detector.StatusChanged += status => { lastPublished = status; published.Add(status); };
        Action<int> scan = count => { for (int i = 0; i < count; i++) detector.Scan(null); };
        try
        {
            detector.Scan(null);
            Assert(lastPublished == "apex5" && identityReads == 1, "First connection was not verified.");
            now += 2000; scan(10);
            Assert(identityReads == 1 && lastPublished == "apex5", "An unchanged verified controller was re-identified every scan.");
            published.Clear();
            now += ControllerDetectionService.ReverifyMilliseconds; model = "unavailable";
            detector.Scan(null);
            model = "apex5";
            detector.Scan(null);
            Assert(identityReads == 3 && published.Count == 0, "One missed identity reply made the controller flap.");
            scan(3);
            Assert(identityReads == 3, "A recovered controller was not cached again.");
            now += ControllerDetectionService.ReverifyMilliseconds;
            model = "unavailable"; // Sleeping controller; receiver stays enumerated.
            scan(ControllerDetectionService.FailureScans - 1);
            Assert(lastPublished == "apex5", "A verified controller was downgraded before the failure threshold.");
            detector.Scan(null);
            Assert(lastPublished == "unavailable" && identityReads == 3 + ControllerDetectionService.FailureScans,
                "Unchanged dongle kept a stale connected controller.");
            model = "apex5";
            detector.Scan(null);
            Assert(lastPublished == "apex5", "A woken controller was not verified again.");
            published.Clear();
            var reads = identityReads;
            foreach (var glitch in new[] { "unavailable", "disconnected" })
            {
                candidateState = glitch;
                detector.Scan(null);
                candidateState = null;
                detector.Scan(null);
            }
            Assert(published.Count == 0 && identityReads == reads + 2, "A transient enumeration glitch flapped the controller.");
            candidateState = "disconnected";
            scan(ControllerDetectionService.DisconnectScans);
            Assert(lastPublished == "disconnected" && identityReads == reads + 2, "A real unplug was not reported.");
            candidateState = null;
            detector.Scan(null);
            Assert(lastPublished == "apex5" && identityReads == reads + 3, "Reconnecting did not verify the controller.");
            candidate = "path-b"; model = "apex6";
            detector.Scan(null);
            Assert(publishedAtIdentify == "unavailable" && lastPublished == "apex6",
                "A changed HID interface was not locked before identification.");
            published.Clear();
            candidateState = ControllerDetectionService.Ambiguous; candidate = "path-x\n\npath-b";
            scan(ControllerDetectionService.FailureScans - 1);
            Assert(published.Count == 0, "A transient second interface locked the verified controller.");
            detector.Scan(null);
            Assert(lastPublished == "apex6" && detector.CandidateCount == 2, "A second listed controller changed the verified controller's model.");
            candidateState = null; candidate = "path-b";
            detector.Scan(null);
            Assert(lastPublished == "apex6", "The remaining controller was not verified again.");
            candidateState = ControllerDetectionService.Ambiguous; candidate = "path-x\n\npath-y";
            detector.Scan(null);
            Assert(lastPublished == "apex6" && detector.Controllers.Count == 2, "Several controllers without the verified one were not identified.");
            candidateState = null; candidate = "path-b";
            detector.Scan(null);
            active = new BridgeSession.SessionInfo { Phase = SessionPhase.Ready, Controller = "Apex 6 Pro (k6)" };
            reads = identityReads;
            detector.Scan(null);
            Assert(identityReads == reads && lastPublished == "apex6", "An active session gained a competing HID identity reader.");
            active.Phase = SessionPhase.Failed;
            detector.Scan(null);
            Assert(lastPublished == "unavailable" && identityReads == reads, "A failed session unlocked a stale model.");
            active = null; owner = true;
            detector.Scan(null);
            Assert(lastPublished == "unavailable" && identityReads == reads, "An uninspectable owned engine triggered hardware identification.");
            owner = false;
            detector.Scan(null);
            Assert(lastPublished == "apex6" && identityReads == reads + 1, "Stopping a session did not reverify the hardware.");
            candidateState = null; changeDuringIdentification = true;
            now += ControllerDetectionService.ReverifyMilliseconds;
            detector.Scan(null);
            Assert(lastPublished == "unavailable", "A mid-identification hot swap published the wrong model.");
            candidateState = "disconnected";
            detector.Scan(null);
            Assert(lastPublished == "disconnected", "Disconnect failed to lock calibration.");
            detector.Dispose();
            candidateState = null;
            reads = identityReads;
            detector.Scan(null);
            Assert(identityReads == reads && lastPublished == "disconnected", "A disposed detector published stale identity.");
        }
        finally { detector.Dispose(); }
    }

    private static int RunFakeSession(string[] args)
    {
        var tokenIndex = Array.IndexOf(args, "--session-token");
        var prefix = "Local\\ApexSenseBridge.Session." + args[tokenIndex + 1];
        using (var mapping = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(prefix + ".Status"))
        using (var view = mapping.CreateViewAccessor())
        using (var progressMap = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(prefix + ".Progress"))
        using (var progress = progressMap.CreateViewAccessor())
        using (var ready = EventWaitHandle.OpenExisting(prefix + ".Ready"))
        using (var stop = EventWaitHandle.OpenExisting(prefix + ".Stop"))
        {
            if (Array.IndexOf(args, "--ignore-stop") >= 0)
            {
                Thread.Sleep(30000);
                return 0;
            }
            var injectedFailure = Environment.GetEnvironmentVariable("ASB_TEST_FAKE_FAILURE");
            bool fail = Array.IndexOf(args, "--fail") >= 0 || !string.IsNullOrEmpty(injectedFailure);
            int failureCode = 11;
            string failureMessage = "Isolation impossible (test).";
            if (!string.IsNullOrEmpty(injectedFailure))
            {
                var parts = injectedFailure.Split(new[] { '|' }, 2);
                failureCode = int.Parse(parts[0], CultureInfo.InvariantCulture);
                failureMessage = parts[1];
            }
            progress.Write(0, 0x50534241u);
            progress.Write(4, 1u);
            foreach (uint stages in new uint[] { 1, 3, 7 })
            {
                progress.Write(8, stages);
                Thread.Sleep(150);
            }
            progress.Write(8, fail ? 7u : 15u);
            var bytes = Encoding.UTF8.GetBytes(fail ? failureMessage : "ASB_READY|APEX test USB");
            view.Write(0, 0x53425341u);
            view.Write(4, (ushort)1);
            view.Write(6, (ushort)(fail ? 5 : 2));
            view.Write(8, fail ? failureCode : 0);
            view.Write(12, (uint)bytes.Length);
            view.WriteArray(16, bytes, 0, bytes.Length);
            ready.Set();
            if (fail) return failureCode;
            var runtimeFailure = Environment.GetEnvironmentVariable("ASB_TEST_FAKE_RUNTIME_FAILURE");
            if (!string.IsNullOrEmpty(runtimeFailure) && !stop.WaitOne(300))
            {
                var parts = runtimeFailure.Split(new[] { '|' }, 2);
                int runtimeCode = int.Parse(parts[0], CultureInfo.InvariantCulture);
                var runtimeMessage = Encoding.UTF8.GetBytes(parts[1]);
                view.Write(6, (ushort)5);
                view.Write(8, runtimeCode);
                view.Write(12, (uint)runtimeMessage.Length);
                view.WriteArray(16, runtimeMessage, 0, runtimeMessage.Length);
                return runtimeCode;
            }
            if (Array.IndexOf(args, "--disconnect") >= 0 && !stop.WaitOne(900))
            {
                progress.Write(12, 1u);
                var failure = Encoding.UTF8.GetBytes("Controller disconnected; cleanup requires reconnect (test).");
                view.Write(6, (ushort)5);
                view.Write(8, 11); // Cleanup can fail after a real disconnect.
                view.Write(12, (uint)failure.Length);
                view.WriteArray(16, failure, 0, failure.Length);
                return 11;
            }
            var lossEvent = Environment.GetEnvironmentVariable("ASB_TEST_LOSS_EVENT");
            if (!string.IsNullOrEmpty(lossEvent))
            {
                using (var loss = EventWaitHandle.OpenExisting(lossEvent))
                {
                    var guard = Stopwatch.StartNew();
                    while (guard.Elapsed < TimeSpan.FromSeconds(30) && !stop.WaitOne(25))
                        progress.Write(8, loss.WaitOne(0) ? 31u : 15u);
                }
                view.Write(6, (ushort)4);
                return 0;
            }
            if (Array.IndexOf(args, "--disconnect") < 0) stop.WaitOne(10000);
            view.Write(6, (ushort)4);
            return 0;
        }
    }

    private static void TestEngineMainEntry(string testRoot)
    {
        var root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", ".."));
        var engine = Path.Combine(root, "build-win", "Release", "ApexSenseBridge.exe");
        if (!File.Exists(engine)) return; // Standalone C# test runs may precede the native build.
        var fixture = Path.Combine(testRoot, "main-entry");
        Directory.CreateDirectory(fixture);
        File.Copy(engine, Path.Combine(fixture, "ApexSenseBridge.exe"));
        File.Copy(Assembly.GetExecutingAssembly().Location, Path.Combine(fixture, "ApexSenseBridgeTray.exe"));
        var eventName = "Local\\ASB.EntryTest." + Guid.NewGuid().ToString("N");
        using (var shown = new EventWaitHandle(false, EventResetMode.ManualReset, eventName))
        {
            var start = new ProcessStartInfo { FileName = Path.Combine(fixture, "ApexSenseBridge.exe"), UseShellExecute = false, CreateNoWindow = true };
            Environment.SetEnvironmentVariable("ASB_TEST_SHOW_EVENT", eventName);
            using (var process = Process.Start(start))
            {
                Assert(shown.WaitOne(3000), "Opening the engine without arguments did not forward to the main Tray.");
                Assert(process.WaitForExit(3000) && process.ExitCode == 0, "Main-entry forwarding did not exit successfully.");
            }
            Environment.SetEnvironmentVariable("ASB_TEST_SHOW_EVENT", null);
        }
    }

    private static void TestSessionStatusDiscovery()
    {
        // Real IPC with a child test process, never a controller/driver session.
        string error;
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.SessionTest." + Guid.NewGuid().ToString("N");
        var stages = new HashSet<uint>();
        using (var session = BridgeSession.TryStart(executable, "--fake-session", TimeSpan.FromSeconds(3), null, null, out error, "Alpha", "standard / APEX 2", discovery, status => stages.Add(status.Stages)))
        {
            Assert(session != null, "Fake IPC session failed: " + error);
            var info = BridgeSession.ReadActiveSession(discovery);
            Assert(info != null && info.Game == "Alpha" && info.Controller == "APEX test USB", "Active session discovery lost game/controller.");
            Assert(info.Phase == SessionPhase.Ready && info.Profile == "standard / APEX 2", "Active session phase/profile was wrong.");
            Assert(info.Stages == 15 && info.Interruption == 0, "External progress was not read from the token-scoped mapping.");
            Assert(stages.Contains(1) && stages.Contains(3) && stages.Contains(7) && stages.Contains(15), "Startup verification stages were lost.");
            Assert(session.StopAndWait(TimeSpan.FromSeconds(3)), "Graceful fake session shutdown failed.");
            Assert(session.ReadStatus().Phase == SessionPhase.Stopped, "Final session phase was lost.");
        }
        Assert(BridgeSession.ReadActiveSession(discovery) == null, "Disposed IPC session remained visible.");
        using (var failedSession = BridgeSession.TryStart(executable, "--fake-session --fail", TimeSpan.FromSeconds(3), null, null, out error))
            Assert(failedSession == null && error == "Isolation impossible (test).", "Initialization failure reason was lost.");
        var forcedCleanupLogged = false;
        using (var hungSession = BridgeSession.TryStart(
            executable, "--fake-session --ignore-stop", TimeSpan.FromMilliseconds(200),
            null, message => forcedCleanupLogged |= message.Contains("stale session"), out error))
        {
            Assert(hungSession == null, "A hung startup unexpectedly returned a session.");
        }
        Assert(forcedCleanupLogged,
            "A hung startup was not forcibly reaped after its cooperative stop timeout.");
    }

    private static void TestHidHideBusyDetection()
    {
        string[] executables;
        Assert(EngineSessionManager.TryParseHidHideBusy(
            "Temporary APEX isolation failed: HidHide is busy: DSX (DSX.exe), HidHide Configuration Client (HidHideClient.exe) are holding the HidHide control device. Close them or disable their HidHide integration, then retry.",
            out executables) && executables.Length == 2 && executables[0] == "DSX.exe" && executables[1] == "HidHideClient.exe",
            "The engine's HidHide busy error was not recognized.");
        Assert(EngineSessionManager.TryParseHidHideBusy("Temporary APEX isolation failed: HidHide is busy: another application is holding the HidHide control device (DSX, DS4Windows...).", out executables) && executables.Length == 0,
            "An unnamed HidHide busy error reported executables.");
        Assert(!EngineSessionManager.TryParseHidHideBusy("Temporary APEX isolation failed: HidHide is not installed.", out executables) && !EngineSessionManager.TryParseHidHideBusy(null, out executables),
            "An unrelated isolation failure was classified as HidHide busy.");
    }

    private static void TestRecoveryPolicy()
    {
        var recovery = new SessionRecoveryState();
        Assert(!recovery.Offer("Alpha", "standard", 2, 0, 15), "A deliberate stop/generic failure offered recovery.");
        Assert(!recovery.Offer("Alpha", "standard", 2, 1, 7), "A startup failure offered runtime recovery.");
        Assert(!recovery.Offer("Alpha", "standard", 2, 99, 15), "An unknown interruption offered recovery.");
        Assert(recovery.Offer("Alpha", "standard", 2, 1, 15), "A confirmed physical disconnect did not offer recovery.");
        Assert(recovery.Pending && recovery.BlocksAutomaticActivation && !recovery.Resuming, "Recovery restarted without consent.");
        Assert(recovery.Game == "Alpha" && recovery.Profile == "standard" && recovery.ApexSlot == 2, "Recovery lost its original session context.");
        recovery.ControllerDetected(true);
        Assert(recovery.ControllerAvailable && recovery.Stages == 0, "Presence was incorrectly presented as verified isolation/readiness.");
        Assert(recovery.Begin() && !recovery.Begin(), "Duplicate recovery was accepted.");
        recovery.Observe(1); recovery.Observe(2);
        Assert(recovery.Stages == 3, "Observed recovery stages are not cumulative.");
        recovery.Dismiss();
        Assert(recovery.Pending && recovery.Resuming, "A running recovery was dismissed.");
        recovery.Complete(false);
        Assert(recovery.Pending && recovery.BlocksAutomaticActivation && !recovery.Recovered, "A failed recovery resumed automation or claimed success.");
        Assert(recovery.Begin() && recovery.Stages == 0, "A retry reused verification results from the failed attempt.");
        recovery.Observe(15); recovery.Complete(true);
        Assert(recovery.Recovered && !recovery.Pending && !recovery.BlocksAutomaticActivation, "Successful recovery stayed blocked.");
        var snapshot = recovery.Snapshot(); recovery.Clear();
        Assert(snapshot.Recovered && !recovery.Recovered, "Recovery snapshots mutate with the service state.");
        Assert(recovery.Offer("Alpha", "standard", 0, 2, 15, "Playnite"), "Virtual disconnection was not recognized.");
        Assert(!recovery.Begin() && recovery.Pending, "Tray took ownership of a Playnite recovery.");
        recovery.Dismiss();
        Assert(!recovery.Pending && recovery.BlocksAutomaticActivation, "Dismissing the notice silently restarted the bridge.");
        recovery.Clear();
        Assert(!recovery.BlocksAutomaticActivation && recovery.Game == null, "Closing the game left a pending recovery.");
        Assert(recovery.Offer("Alpha", "standard", 0, 3, 15), "Loss of input while the dongle remains attached did not offer recovery.");
        recovery.Clear();
        Assert(recovery.Offer("Alpha", "standard", 0, 4, 15) && recovery.BlocksAutomaticActivation,
            "A runtime APEX feedback-writer loss restarted the bridge automatically instead of offering recovery.");
        recovery.Clear();
        Assert(!recovery.Offer("Alpha", "standard", 0, 5, 15), "An unknown interruption offered recovery.");
        Assert(!recovery.Offer("Alpha", "standard", 0, 4, 7), "A startup failure offered runtime recovery.");
    }

    private static void TestInterruptedSessionRecovery()
    {
        string error;
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.RecoveryTest." + Guid.NewGuid().ToString("N");
        using (var session = BridgeSession.TryStart(executable, "--fake-session --disconnect", TimeSpan.FromSeconds(3), null, null, out error, "Alpha", "standard", discovery))
        {
            Assert(session != null, "Fake interrupted session did not start: " + error);
            var manager = new EngineSessionManager(() => executable, discovery, () => false);
            try
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                typeof(EngineSessionManager).GetField("activeSession", flags).SetValue(manager, session);
                typeof(EngineSessionManager).GetField("activeGameTitle", flags).SetValue(manager, "Alpha");
                typeof(EngineSessionManager).GetField("activeProfileName", flags).SetValue(manager, "standard");
                typeof(EngineSessionManager).GetField("activeApexSlot", flags).SetValue(manager, 3);
                WaitUntil(() => manager.Recovery.Pending, TimeSpan.FromSeconds(3), "Interrupted engine did not offer recovery.");
                Assert(!manager.IsSessionActive && manager.AutomaticActivationBlocked, "A dead session was retained or automatically retried.");
                Assert(manager.Recovery.ApexSlot == 3 && manager.LastReason.Contains("cleanup"), "Recovery lost the slot or masked a cleanup failure.");
                Assert(!manager.StartSession("Alpha", "standard", new TraySettings(), out error), "Automatic startup bypassed recovery consent.");
                Assert(manager.ResumeSession(new TraySettings(), out error), "Controlled recovery failed: " + error);
                Assert(manager.IsSessionActive && manager.Recovery.Recovered && !manager.AutomaticActivationBlocked, "Recovered session was not ready or remained blocked.");
                Assert(manager.Recovery.Stages == 15 && manager.ActiveProfile == "standard / APEX 3", "Resume lost verified stages or the original APEX slot.");
                manager.StopSession("Recovery test stopped");
                var state = (SessionRecoveryState)typeof(EngineSessionManager).GetField("recovery", flags).GetValue(manager);
                state.Offer("Alpha", "standard", 3, 1, 15);
                manager.DismissRecovery();
                Assert(manager.AutomaticActivationBlocked, "Dismissing the recovery restarted automation.");
                manager.StopSession("Game closed");
                Assert(!manager.AutomaticActivationBlocked, "Game shutdown did not clear the recovery gate.");
            }
            finally { manager.Dispose(); }
        }
        Assert(BridgeSession.ReadSessionStatus("invalid") == null, "Invalid status token was accepted.");
    }

    private static void TestExternalRecoveryOwnership()
    {
        string error;
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.ExternalRecoveryTest." + Guid.NewGuid().ToString("N");
        using (var session = BridgeSession.TryStart(executable, "--fake-session --disconnect", TimeSpan.FromSeconds(3), null, null, out error, "Alpha", "standard", discovery))
        {
            Assert(session != null, "Fake external session did not start: " + error);
            var info = BridgeSession.ReadActiveSession(discovery);
            info.Owner = "Playnite";
            using (var mapping = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(discovery))
            using (var view = mapping.CreateViewAccessor())
            {
                var bytes = Encoding.UTF8.GetBytes(new System.Web.Script.Serialization.JavaScriptSerializer().Serialize(info));
                view.Write(0, 0); view.WriteArray(4, bytes, 0, bytes.Length); view.Write(0, bytes.Length);
            }
            var manager = new EngineSessionManager(() => executable, discovery, () => false);
            try
            {
                WaitUntil(() => manager.Recovery.Pending, TimeSpan.FromSeconds(3), "External interruption was not observed.");
                Assert(manager.Recovery.Owner == "Playnite" && manager.Recovery.Game == "Alpha", "External recovery lost its owner/game.");
                Assert(!manager.ResumeSession(new TraySettings(), out error) && !manager.IsSessionActive, "Tray took over a Playnite session.");
                Assert(manager.AutomaticActivationBlocked, "Automatic Tray activation took over an interrupted external session.");
            }
            finally { manager.Dispose(); }
        }
    }

    private static void TestControllerLostStatusMapping()
    {
        var lost = new BridgeSession.SessionInfo { Phase = SessionPhase.Ready, Stages = 31 };
        var steady = new BridgeSession.SessionInfo { Phase = SessionPhase.Ready, Stages = 15 };
        Assert(lost.ControllerLost && !steady.ControllerLost && !new BridgeSession.SessionInfo().ControllerLost,
            "The physical-lost progress bit was not exposed as ControllerLost.");
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer();
        var json = serializer.Serialize(lost);
        Assert(!json.Contains("ControllerLost") && serializer.Deserialize<BridgeSession.SessionInfo>(json).ControllerLost,
            "ControllerLost leaked into the discovery JSON or the stages no longer round-trip.");
        Assert(EngineSessionManager.SessionStatusKey("Ready", true, false) == "Loc_SessionControllerLost",
            "A ready session with a lost controller did not map to the waiting status.");
        Assert(EngineSessionManager.SessionStatusKey("Ready", false, false) == "Loc_SessionReady",
            "A ready session with its controller did not return to the normal status.");
        Assert(EngineSessionManager.SessionStatusKey("Stopping", true, false) == "Loc_SessionStopping" &&
               EngineSessionManager.SessionStatusKey("Failed", true, false) == "Loc_SessionFailed",
            "A stale lost bit overrode a session that is no longer ready.");
        Assert(EngineSessionManager.SessionStatusKey("Ready", true, true) == "Loc_SessionInterrupted",
            "A pending recovery was hidden by the waiting status.");
        Assert(EngineSessionManager.IsWaitingForController("Ready", true) && !EngineSessionManager.IsWaitingForController("Starting", true) &&
               !EngineSessionManager.IsWaitingForController("Ready", false),
            "The waiting-for-controller predicate is wrong.");

        var recovery = new SessionRecoveryState();
        Assert(recovery.Offer("Alpha", "standard", 2, 1, 31), "The lost bit changed which interruptions offer recovery.");
        Assert(recovery.Begin(), "Recovery could not begin.");
        recovery.Observe(16);
        Assert(recovery.Stages == 0, "Recovery observed the physical-lost bit as a verification stage.");
        recovery.Observe(31);
        Assert(recovery.Stages == 15, "Recovery stages no longer mask the physical-lost bit.");
        recovery.Clear();
        Assert(!recovery.Offer("Alpha", "standard", 2, 1, 23), "The lost bit stood in for the runtime-ready stage.");
    }

    private static void TestControllerLostLocalization()
    {
        var root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", ".."));
        var directory = Path.Combine(root, "ApexSenseBridgeTray", "Resources", "Languages");
        Assert(Directory.Exists(directory), "The language resources were not found.");
        var serializer = new System.Web.Script.Serialization.JavaScriptSerializer { MaxJsonLength = int.MaxValue };
        var english = serializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(directory, "en.json"), Encoding.UTF8));
        var languages = new[] { "en", "es", "fr", "ja", "ko", "pt", "ru", "vi", "zh" };
        foreach (var language in languages)
        {
            var path = Path.Combine(directory, language + ".json");
            Assert(File.Exists(path), language + ".json is missing.");
            var entries = serializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(path, Encoding.UTF8));
            Assert(entries.Count == english.Count && english.Keys.All(entries.ContainsKey),
                language + ".json does not define exactly the English key set.");
            foreach (var key in new[] { "Loc_SessionControllerLost", "Loc_SessionControllerLostHint" })
            {
                var text = entries[key] as string;
                Assert(!string.IsNullOrWhiteSpace(text), language + " has an empty " + key + ".");
                Assert(language == "en" || text != (string)english[key], language + " reuses the English text for " + key + ".");
            }
        }
    }

    private static void TestControllerLostIsNotAFailure()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.ControllerLostTest." + Guid.NewGuid().ToString("N");
        var signalName = "Local\\ASB.ControllerLostSignal." + Guid.NewGuid().ToString("N");
        using (var signal = new EventWaitHandle(false, EventResetMode.ManualReset, signalName))
        {
            Environment.SetEnvironmentVariable("ASB_TEST_LOSS_EVENT", signalName);
            var manager = new EngineSessionManager(() => executable, discovery, () => false);
            try
            {
                int changes = 0, stops = 0, errors = 0;
                manager.StateChanged += () => Interlocked.Increment(ref changes);
                manager.SessionStopped += reason => Interlocked.Increment(ref stops);
                manager.SessionError += error => Interlocked.Increment(ref errors);
                string startError;
                Assert(manager.StartSession("Alpha", "standard", new TraySettings(), out startError), "Fake bridge session did not start: " + startError);
                Thread.Sleep(1200);
                Assert(manager.IsSessionActive && !manager.ControllerLost &&
                       EngineSessionManager.SessionStatusKey(manager.StateName, manager.ControllerLost, false) == "Loc_SessionReady",
                    "A healthy Tray-owned session was reported as waiting for its controller.");
                int baseline = Volatile.Read(ref changes);

                signal.Set();
                WaitUntil(() => manager.ControllerLost, TimeSpan.FromSeconds(3), "The Tray-owned session never reported the lost controller.");
                Thread.Sleep(1200);
                Assert(manager.IsSessionActive && manager.IsSessionHealthy && manager.StateName == "Ready",
                    "Losing the controller ended or degraded the still-running session.");
                Assert(EngineSessionManager.SessionStatusKey(manager.StateName, manager.ControllerLost, manager.Recovery.Pending && !manager.Recovery.Resuming) == "Loc_SessionControllerLost",
                    "The dashboard status for a lost controller is not the waiting status.");
                int unexpectedCode;
                string unexpectedReason;
                Assert(!manager.Recovery.Pending && !manager.AutomaticActivationBlocked && !manager.TryTakeUnexpectedExit(out unexpectedCode, out unexpectedReason),
                    "A lost controller was handled as a failure or offered recovery.");
                Assert(stops == 0 && errors == 0, "A lost controller raised a stop or error notification.");
                Assert(Volatile.Read(ref changes) == baseline + 1, "StateChanged did not fire exactly once when the flag was set.");

                signal.Reset();
                WaitUntil(() => !manager.ControllerLost, TimeSpan.FromSeconds(3), "The Tray-owned session never recovered from the lost controller.");
                Thread.Sleep(1200);
                Assert(EngineSessionManager.SessionStatusKey(manager.StateName, manager.ControllerLost, false) == "Loc_SessionReady" &&
                       manager.IsSessionActive && !manager.Recovery.Pending,
                    "The dashboard status did not return to normal when the controller came back.");
                Assert(Volatile.Read(ref changes) == baseline + 2, "StateChanged did not fire exactly once when the flag cleared.");
                Assert(stops == 0 && errors == 0, "Recovering the controller raised a stop or error notification.");

                signal.Set();
                WaitUntil(() => manager.ControllerLost, TimeSpan.FromSeconds(3), "A second loss was not reported.");
                manager.StopSession("Controller loss test stopped");
                Assert(!manager.ControllerLost && !manager.IsSessionActive, "Stopping a waiting session left the flag set.");
            }
            finally
            {
                manager.Dispose();
                Environment.SetEnvironmentVariable("ASB_TEST_LOSS_EVENT", null);
            }
        }
    }

    private static void TestControllerLostOnExternalSession()
    {
        string error;
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.ExternalControllerLostTest." + Guid.NewGuid().ToString("N");
        var signalName = "Local\\ASB.ExternalControllerLostSignal." + Guid.NewGuid().ToString("N");
        using (var signal = new EventWaitHandle(false, EventResetMode.ManualReset, signalName))
        {
            Environment.SetEnvironmentVariable("ASB_TEST_LOSS_EVENT", signalName);
            try
            {
                using (var session = BridgeSession.TryStart(executable, "--fake-session", TimeSpan.FromSeconds(3), null, null, out error, "Alpha", "standard", discovery))
                {
                    Assert(session != null, "Fake external session did not start: " + error);
                    var info = BridgeSession.ReadActiveSession(discovery);
                    info.Owner = "Playnite";
                    using (var mapping = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(discovery))
                    using (var view = mapping.CreateViewAccessor())
                    {
                        var bytes = Encoding.UTF8.GetBytes(new System.Web.Script.Serialization.JavaScriptSerializer().Serialize(info));
                        view.Write(0, 0); view.WriteArray(4, bytes, 0, bytes.Length); view.Write(0, bytes.Length);
                    }
                    var manager = new EngineSessionManager(() => executable, discovery, () => false);
                    try
                    {
                        int changes = 0, stops = 0, errors = 0;
                        manager.StateChanged += () => Interlocked.Increment(ref changes);
                        manager.SessionStopped += reason => Interlocked.Increment(ref stops);
                        manager.SessionError += err => Interlocked.Increment(ref errors);
                        Thread.Sleep(1200);
                        Assert(!manager.ControllerLost && BridgeSession.ReadActiveSession(discovery).ControllerLost == false,
                            "A healthy external session was reported as waiting for its controller.");
                        int baseline = Volatile.Read(ref changes);

                        signal.Set();
                        WaitUntil(() => manager.ControllerLost, TimeSpan.FromSeconds(3), "The external session never reported the lost controller.");
                        var read = BridgeSession.ReadActiveSession(discovery);
                        Assert(read != null && read.Phase == SessionPhase.Ready && read.ControllerLost && read.Interruption == 0,
                            "The external lost-controller state was not read from the shared progress block.");
                        Thread.Sleep(1200);
                        Assert(!manager.Recovery.Pending && !manager.AutomaticActivationBlocked && !manager.IsSessionActive && stops == 0 && errors == 0,
                            "A lost controller on an external session was handled as a failure or offered recovery.");
                        Assert(Volatile.Read(ref changes) == baseline + 1, "StateChanged did not fire exactly once for the external flag.");

                        signal.Reset();
                        WaitUntil(() => !manager.ControllerLost, TimeSpan.FromSeconds(3), "The external session never recovered from the lost controller.");
                        Thread.Sleep(1200);
                        Assert(Volatile.Read(ref changes) == baseline + 2 && !manager.Recovery.Pending && stops == 0 && errors == 0,
                            "The external flag clearing was not reported exactly once or raised a failure.");

                        signal.Set();
                        WaitUntil(() => manager.ControllerLost, TimeSpan.FromSeconds(3), "A second external loss was not reported.");
                        Assert(session.StopAndWait(TimeSpan.FromSeconds(3)), "The fake external session did not stop.");
                        WaitUntil(() => !manager.ControllerLost, TimeSpan.FromSeconds(3), "A finished external session kept the waiting flag.");
                        Assert(!manager.Recovery.Pending && stops == 0, "A cleanly stopped external session offered recovery.");
                    }
                    finally { manager.Dispose(); }
                }
            }
            finally { Environment.SetEnvironmentVariable("ASB_TEST_LOSS_EVENT", null); }
        }
    }

    private static void TestRecoveryCancelledDuringStartup()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var discovery = "Local\\ASB.CancelRecoveryTest." + Guid.NewGuid().ToString("N");
        var manager = new EngineSessionManager(() => executable, discovery, () => false);
        try
        {
            var state = (SessionRecoveryState)typeof(EngineSessionManager).GetField("recovery", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(manager);
            state.Offer("Alpha", "standard", 2, 1, 15);
            string error = null;
            var resume = System.Threading.Tasks.Task.Run(() => manager.ResumeSession(new TraySettings(), out error));
            WaitUntil(() => manager.StateName == "Starting", TimeSpan.FromSeconds(2), "Recovery never entered startup.");
            manager.StopSession("Game exited during recovery");
            Assert(resume.Wait(TimeSpan.FromSeconds(4)) && !resume.Result, "Recovery ignored cancellation during startup.");
            Assert(!manager.IsSessionActive && !manager.Recovery.Pending && !manager.AutomaticActivationBlocked, "Cancelled recovery left an active engine or a stale gate.");
        }
        finally { manager.Dispose(); }
    }

    private static void TestRecoveryGameLifetimeTracking()
    {
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("alphagame", @"C:\Fake\Alpha.exe");
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return null; }, "Local\\ASB.LifetimeTest." + Guid.NewGuid().ToString("N"), () => false);
        using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
        {
            try
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var recovery = (SessionRecoveryState)typeof(EngineSessionManager).GetField("recovery", flags).GetValue(manager);
                recovery.Offer("Alpha Game", "standard", 0, 1, 15, "Playnite");
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                check.Invoke(monitor, new object[] { 1001u, "Alpha.exe", @"C:\Fake\Alpha.exe", 0L, null, "test", false });
                var tracker = (GameProcessSessionTracker)typeof(ProcessMonitorService).GetField("processSession", flags).GetValue(monitor);
                Assert(tracker.Contains(1001) && launchAttempts == 0, "Interrupted external game was not tracked without taking ownership.");
                tracker.Remove(1001, DateTime.UtcNow, TimeSpan.FromSeconds(2));
                check.Invoke(monitor, new object[] { 1002u, "Alpha.exe", @"C:\Fake\Alpha.exe", 0L, null, "test", false });
                Assert(tracker.Contains(1002) && !tracker.IsAwaitingReplacement && launchAttempts == 0, "PID handoff during recovery restarted or lost the session.");
                manager.DismissRecovery();
                tracker.Remove(1002, DateTime.UtcNow.AddSeconds(-3), TimeSpan.FromSeconds(2));
                bool stopped = (bool)typeof(ProcessMonitorService).GetMethod("TryStopExpiredSession", flags).Invoke(monitor, null);
                Assert(stopped && !manager.AutomaticActivationBlocked && !manager.Recovery.Pending && launchAttempts == 0, "Closing the interrupted game failed to clear recovery without launching an engine.");
            }
            finally { manager.Dispose(); }
        }
    }

    private static void TestStartRetryPolicy()
    {
        var t0 = new DateTime(2026, 10, 19, 12, 0, 0, DateTimeKind.Utc);
        var policy = new StartRetryPolicy();
        Assert(policy.RecordFailure("alpha", 11, "Isolation failed", t0, 100) == StartRetryPolicy.RetryDelay && !policy.IsPaused("alpha"),
            "A first failure did not keep the normal retry delay.");
        Assert(policy.RecordFailure("alpha", 11, "Isolation failed", t0.AddSeconds(12), 101) == null &&
               policy.IsPaused("alpha") && policy.HasPausedGame,
            "Two identical failures of one game in two different processes did not stop automatic retries.");
        Assert(!policy.IsPaused("beta") && policy.RecordFailure("beta", 11, "Isolation failed", t0, 200) == StartRetryPolicy.RetryDelay,
            "A pause leaked to another game.");

        Assert(policy.RecordFailure("gamma", 11, "Isolation failed", t0, 300) == StartRetryPolicy.RetryDelay &&
               policy.RecordFailure("gamma", 11, "Isolation failed", t0.Add(StartRetryPolicy.IdenticalFailureWindow).AddSeconds(1), 301) ==
                   StartRetryPolicy.RetryDelay && !policy.IsPaused("gamma"),
            "Identical failures far apart counted as a restart loop.");

        Assert(policy.RecordFailure("delta", 11, "A", t0, 400) == StartRetryPolicy.RetryDelay &&
               policy.RecordFailure("delta", 11, "B", t0, 400) == StartRetryPolicy.RetryDelay &&
               policy.RecordFailure("delta", 12, "B", t0, 400) == StartRetryPolicy.RetryDelay && !policy.IsPaused("delta"),
            "Failures with a different message or exit code counted as identical.");
        Assert(policy.RecordFailure("delta", 12, "B", t0, 400) == null, "The latest identical failure pair did not pause.");

        Assert(policy.RecordFailure("eps", 3, "APEX identity check failed: asleep", t0, 500) == StartRetryPolicy.IdentityRetryDelay,
            "An identity check failure was not retried quickly.");
        Assert(policy.RecordFailure("eps", 3, "APEX identity check failed: busy", t0, 501) == StartRetryPolicy.RetryDelay,
            "An identity check failure got more than one quick retry.");
        Assert(policy.RecordFailure("eps", 3, "APEX identity check failed: busy", t0, 502) == StartRetryPolicy.RetryDelay && !policy.IsPaused("eps"),
            "Repeated identity check failures paused automatic starts.");
        Assert(policy.RecordFailure("zeta", 3, "APEX identity check failed: asleep", t0, 600) == StartRetryPolicy.IdentityRetryDelay,
            "An identity check failure was not retried quickly.");
        for (int i = 0; i < 10; i++)
            Assert(policy.RecordFailure("zeta", 3, "APEX identity check failed: asleep", t0, (uint)(601 + i)) == StartRetryPolicy.RetryDelay &&
                   !policy.IsPaused("zeta"),
                "Identical identity check failures paused automatic starts or repeated the quick retry.");
        Assert(policy.RecordFailure("zeta", 11, "Isolation failed", t0, 620) == StartRetryPolicy.RetryDelay &&
               policy.RecordFailure("zeta", 11, "Isolation failed", t0, 621) == null && policy.IsPaused("zeta"),
            "Other failures after an identity check failure did not pause.");
        Assert(policy.RecordFailure("zeta", 3, "APEX identity check failed: asleep", t0, 622) == StartRetryPolicy.RetryDelay && !policy.IsPaused("zeta"),
            "An identity check failure kept a previous pause.");
        Assert(StartRetryPolicy.IdentityRetryDelay == TimeSpan.FromSeconds(2), "The quick identity retry is not about 2 s.");

        DateTime retryAt;
        policy.RecordFailure("eta", 11, "A", t0, 700);
        Assert(policy.IsDeferred("eta", t0.AddSeconds(5), out retryAt) && retryAt == t0.Add(StartRetryPolicy.RetryDelay) &&
               !policy.IsDeferred("eta", t0.Add(StartRetryPolicy.RetryDelay), out retryAt),
            "Another process of a failed game was not held until the retry time.");
        Assert(policy.IsDeferred("alpha", t0.AddHours(1), out retryAt) && retryAt == DateTime.MaxValue,
            "A paused game was not held until Retry.");

        policy.Reset();
        Assert(!policy.HasPausedGame && !policy.HasEntries &&
               policy.RecordFailure("eps", 3, "APEX identity check failed: busy", t0, 500) == StartRetryPolicy.IdentityRetryDelay,
            "A manual retry did not clear the pause and quick-retry state.");

        Assert(!policy.ObserveController("unavailable", false) && !policy.ObserveController("apex5", false),
            "The first controller report counted as a change.");
        Assert(!policy.ObserveController("unavailable", true) && !policy.ObserveController("ambiguous", false) &&
               !policy.ObserveController("apex5", false),
            "A bridge start or an ambiguous scan counted as a controller change.");
        Assert(!policy.ObserveController("unavailable", false) && policy.ObserveController("apex5", false),
            "A controller that went unavailable and came back was not treated as a change.");
        Assert(policy.ObserveController("apex6", false) && policy.ObserveController("disconnected", false) &&
               !policy.ObserveController("disconnected", false),
            "A controller swap or disconnection was not detected.");

        var retained = new StartRetryPolicy();
        retained.RecordFailure("alpha", 11, "Isolation failed", t0, 800);
        retained.RecordFailure("alpha", 11, "Isolation failed", t0, 801);
        retained.Forget(800, t0);
        retained.Forget(801, t0.AddSeconds(1));
        retained.Retain(new HashSet<uint> { 4 }, t0.AddSeconds(29));
        Assert(retained.IsPaused("alpha"), "A launcher-to-game PID handoff released the pause of the game.");
        retained.Observe("alpha", 802);
        retained.Retain(new HashSet<uint> { 4, 802 }, t0.AddMinutes(2));
        Assert(retained.IsPaused("alpha"), "A paused game that is still running was forgotten.");
        retained.Retain(new HashSet<uint> { 4 }, t0.AddMinutes(3));
        retained.Retain(new HashSet<uint> { 4 }, t0.AddMinutes(3).Add(StartRetryPolicy.GameClosedDelay));
        Assert(!retained.IsPaused("alpha") && !retained.HasEntries &&
               retained.RecordFailure("alpha", 11, "Isolation failed", t0.AddMinutes(4), 803) == StartRetryPolicy.RetryDelay,
            "A game closed for longer than the closed delay kept its failure history.");
    }

    private static void TestAutomaticStartPause()
    {
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("alphagame", @"C:\Fake\Alpha.exe");
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return null; }, "Local\\ASB.RetryPauseTest." + Guid.NewGuid().ToString("N"), () => false);
        using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
        {
            try
            {
                var now = DateTime.UtcNow;
                monitor.retryClock = () => now;
                Action advance = () => now = now.Add(StartRetryPolicy.RetryDelay).AddSeconds(1);
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var cooldowns = (Dictionary<uint, DateTime>)typeof(ProcessMonitorService).GetField("retryCooldowns", flags).GetValue(monitor);
                var paused = new List<string>();
                var reasons = new List<string>();
                monitor.AutomaticStartPaused += (title, reason) => { paused.Add(title); reasons.Add(reason); };
                Action<uint> attempt = pid => check.Invoke(monitor, new object[] { pid, "Alpha.exe", @"C:\Fake\Alpha.exe", 0L, null, "test", false });

                attempt(3001);
                Assert(launchAttempts == 1 && paused.Count == 0 && !monitor.HasPausedAutomaticStart, "A first start failure paused automatic starts.");
                Assert(cooldowns[3001] > now.AddSeconds(8), "A generic start failure lost the normal retry delay.");
                attempt(3001);
                Assert(launchAttempts == 1, "A failed start was retried during its cooldown.");
                attempt(3002);
                Assert(launchAttempts == 1 && cooldowns[3002] <= now.Add(StartRetryPolicy.RetryDelay),
                    "Another process of the same game bypassed the retry delay.");
                advance();
                attempt(3001);
                Assert(launchAttempts == 2 && paused.Count == 1 && paused[0] == "Alpha Game" && monitor.HasPausedAutomaticStart,
                    "Two identical start failures did not pause automatic starts with a notification.");
                Assert(!string.IsNullOrWhiteSpace(reasons[0]) && reasons[0] == manager.LastReason,
                    "The pause notification did not carry the engine error text.");
                attempt(3001);
                Assert(launchAttempts == 2 && paused.Count == 1 && cooldowns[3001] == DateTime.MaxValue,
                    "A paused game process was retried automatically.");
                attempt(3002);
                Assert(launchAttempts == 2 && paused.Count == 1 && cooldowns[3002] == DateTime.MaxValue,
                    "A new process of a paused game restarted the bridge.");

                monitor.RetryPausedStarts();
                Assert(!monitor.HasPausedAutomaticStart && !cooldowns.ContainsKey(3001), "A manual retry did not resume automatic starts.");
                attempt(3001);
                Assert(launchAttempts == 3 && !monitor.HasPausedAutomaticStart, "A manual retry kept the previous failure count.");
                advance();
                attempt(3001);
                Assert(launchAttempts == 4 && paused.Count == 2 && monitor.HasPausedAutomaticStart, "Automatic starts did not pause again.");

                monitor.ReportControllerStatus("apex5");
                Assert(monitor.HasPausedAutomaticStart, "The first controller report resumed automatic starts.");
                monitor.ReportControllerStatus("apex6");
                Assert(!monitor.HasPausedAutomaticStart && !cooldowns.ContainsKey(3001), "A controller change did not resume automatic starts.");

                attempt(3001);
                advance();
                attempt(3001);
                Assert(launchAttempts == 6 && monitor.HasPausedAutomaticStart, "The pause was not re-armed after a controller change.");
                monitor.ReportControllerStatus("unavailable");
                Assert(monitor.HasPausedAutomaticStart, "An unavailable controller alone resumed automatic starts.");
                monitor.ReportControllerStatus("apex6");
                Assert(!monitor.HasPausedAutomaticStart, "A controller that came back after being unavailable did not resume automatic starts.");

                attempt(3001);
                advance();
                attempt(3001);
                Assert(launchAttempts == 8 && monitor.HasPausedAutomaticStart, "The pause was not re-armed after a reconnection.");
                var isStarting = typeof(EngineSessionManager).GetField("isStarting", flags);
                isStarting.SetValue(manager, true);
                try
                {
                    monitor.ReportControllerStatus("unavailable");
                    monitor.ReportControllerStatus("apex6");
                    Assert(monitor.HasPausedAutomaticStart, "A bridge start counted as a controller change.");
                    monitor.RetryPausedStarts();
                    for (int i = 0; i < 3; i++)
                    {
                        advance();
                        attempt(3009);
                    }
                    Assert(launchAttempts == 8 && !monitor.HasPausedAutomaticStart && cooldowns[3009] < DateTime.MaxValue,
                        "Refusals while the Tray session was starting paused automatic starts.");
                }
                finally { isStarting.SetValue(manager, false); }

                lock (cooldowns) cooldowns.Clear();
                attempt(3001);
                advance();
                attempt(3001);
                Assert(launchAttempts == 10 && monitor.HasPausedAutomaticStart, "The pause was not re-armed after a busy session.");
                typeof(ProcessMonitorService).GetMethod("ForgetStartFailures", flags).Invoke(monitor, new object[] { 3001u });
                Assert(monitor.HasPausedAutomaticStart && !cooldowns.ContainsKey(3001),
                    "The exit of one game process released the pause of the whole game.");

                var prune = typeof(ProcessMonitorService).GetMethod("PruneStartRetries", flags, null, new[] { typeof(ICollection<uint>) }, null);
                prune.Invoke(monitor, new object[] { new HashSet<uint> { 4 } });
                now = now.Add(StartRetryPolicy.GameClosedDelay);
                prune.Invoke(monitor, new object[] { new HashSet<uint> { 4 } });
                Assert(!monitor.HasPausedAutomaticStart && !cooldowns.ContainsKey(3002) && !cooldowns.ContainsKey(3009),
                    "A closed game kept its pause or the cooldown of exited processes.");
                attempt(3005);
                Assert(launchAttempts == 11 && !monitor.HasPausedAutomaticStart, "A restarted game stayed paused.");
            }
            finally { manager.Dispose(); }
        }

        var external = new EngineSessionManager(() => null, "Local\\ASB.RetryExternalTest." + Guid.NewGuid().ToString("N"), () => true);
        using (var monitor = new ProcessMonitorService(catalog, external, null, settings, false))
        {
            try
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var cooldowns = (Dictionary<uint, DateTime>)typeof(ProcessMonitorService).GetField("retryCooldowns", flags).GetValue(monitor);
                for (int i = 0; i < 3; i++)
                {
                    lock (cooldowns) cooldowns.Remove(5001);
                    check.Invoke(monitor, new object[] { 5001u, "Alpha.exe", @"C:\Fake\Alpha.exe", 0L, null, "test", false });
                }
                Assert(!monitor.HasPausedAutomaticStart && cooldowns[5001] < DateTime.MaxValue,
                    "A session owned by Playnite or another app paused automatic starts.");
            }
            finally { external.Dispose(); }
        }
    }

    private static void TestPidChurningFailureLoopPauses()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Silent Game\",\"normalized\":\"silentgame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("silentgame", @"C:\Fake\Silent\Silent.exe");
        int launchAttempts = 0;
        const string readiness = "Virtual DualSense HID readiness verification failed after 2 attempts: The newly-created virtual DualSense firmware interface was not found.";
        var manager = new EngineSessionManager(() => { launchAttempts++; return executable; }, "Local\\ASB.ChurnTest." + Guid.NewGuid().ToString("N"), () => false);
        Environment.SetEnvironmentVariable("ASB_TEST_FAKE_FAILURE", "9|" + readiness);
        try
        {
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                var now = DateTime.UtcNow;
                monitor.retryClock = () => now;
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var forget = typeof(ProcessMonitorService).GetMethod("ForgetStartFailures", flags);
                var paused = new List<string>();
                monitor.AutomaticStartPaused += (title, reason) => paused.Add(title + "|" + reason);
                uint pid = 8100;
                for (int cycle = 0; cycle < 6; cycle++)
                {
                    check.Invoke(monitor, new object[] { pid, "Silent.exe", @"C:\Fake\Silent\Silent.exe", 0L, null, "test", false });
                    forget.Invoke(monitor, new object[] { pid });
                    pid++;
                    now = now.Add(StartRetryPolicy.RetryDelay).AddSeconds(1);
                }
                Assert(launchAttempts == 2, "A failure loop with a new game PID each time kept restarting the engine (" + launchAttempts + " starts).");
                Assert(paused.Count == 1 && paused[0] == "Silent Game|" + readiness && monitor.HasPausedAutomaticStart,
                    "The PID-churning failure loop did not pause once with the readiness error text.");
                Assert(manager.StateName == "Failed" && manager.LastReason == readiness && !manager.IsSessionActive && !manager.IsBusy,
                    "The failed start did not leave an idle, failed state with its reason.");
                monitor.RetryPausedStarts();
                check.Invoke(monitor, new object[] { pid, "Silent.exe", @"C:\Fake\Silent\Silent.exe", 0L, null, "test", false });
                Assert(launchAttempts == 3 && !monitor.HasPausedAutomaticStart, "Retry did not allow a new start of the paused game.");
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable("ASB_TEST_FAKE_FAILURE", null);
            manager.Dispose();
        }
    }

    private static void TestUnexpectedEngineExitBacksOff()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Hell Game\",\"normalized\":\"hellgame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("hellgame", @"C:\Fake\Hell\Hell.exe");
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return executable; }, "Local\\ASB.RuntimeExitTest." + Guid.NewGuid().ToString("N"), () => false);
        Environment.SetEnvironmentVariable("ASB_TEST_FAKE_RUNTIME_FAILURE", "11|Controller input stopped (test).");
        try
        {
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                var now = DateTime.UtcNow;
                monitor.retryClock = () => now;
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var release = typeof(ProcessMonitorService).GetMethod("ReleaseEndedSession", flags);
                var cooldowns = (Dictionary<uint, DateTime>)typeof(ProcessMonitorService).GetField("retryCooldowns", flags).GetValue(monitor);
                var tracker = (GameProcessSessionTracker)typeof(ProcessMonitorService).GetField("processSession", flags).GetValue(monitor);
                var paused = new List<string>();
                monitor.AutomaticStartPaused += (title, reason) => paused.Add(reason);
                Action<uint> attempt = pid => check.Invoke(monitor, new object[] { pid, "Hell.exe", @"C:\Fake\Hell\Hell.exe", 0L, null, "test", false });

                attempt(8201);
                Assert(launchAttempts == 1 && tracker.Contains(8201), "The first session did not start.");
                WaitUntil(() => !manager.IsSessionActive, TimeSpan.FromSeconds(5), "The engine exit was not observed.");
                release.Invoke(monitor, null);
                Assert(!tracker.HasSession && cooldowns[8201] > now.AddSeconds(8) && cooldowns[8201] < DateTime.MaxValue && paused.Count == 0,
                    "An engine that died after Ready was restarted immediately instead of after the retry delay.");
                attempt(8201);
                attempt(8202);
                Assert(launchAttempts == 1, "The game was restarted during the delay after an unexpected engine exit.");

                now = now.Add(StartRetryPolicy.RetryDelay).AddSeconds(1);
                attempt(8202);
                Assert(launchAttempts == 2, "The game was not retried after the delay.");
                WaitUntil(() => !manager.IsSessionActive, TimeSpan.FromSeconds(5), "The second engine exit was not observed.");
                release.Invoke(monitor, null);
                Assert(paused.Count == 1 && paused[0] == "Controller input stopped (test)." && monitor.HasPausedAutomaticStart,
                    "Two identical engine exits after Ready did not pause automatic starts.");
                now = now.AddMinutes(1);
                attempt(8203);
                Assert(launchAttempts == 2, "A paused activation loop restarted the engine.");

                manager.StopSession("Test stop");
                int ignoredCode;
                string ignoredReason;
                Assert(!manager.TryTakeUnexpectedExit(out ignoredCode, out ignoredReason),
                    "A stop requested by the Tray counted as an unexpected engine exit.");
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable("ASB_TEST_FAKE_RUNTIME_FAILURE", null);
            manager.Dispose();
        }
    }

    private static void TestLauncherHandoffGraceAndInstallFolder()
    {
        var game = new SupportedGame { Title = "Alpha Game", Normalized = "alphagame" };
        var t0 = new DateTime(2026, 10, 19, 12, 0, 0, DateTimeKind.Utc);
        var tracker = new GameProcessSessionTracker();
        tracker.Start(game, 100, @"D:\Games\Alpha Game\Alpha.exe", t0);
        Assert(tracker.Remove(100, t0.AddSeconds(5)) && tracker.IsAwaitingReplacement,
            "The launcher exit did not start the replacement grace period.");
        Assert(!tracker.ShouldStop(t0.AddSeconds(5).Add(GameProcessSessionTracker.LaunchExitGracePeriod).AddMilliseconds(-1)) &&
               GameProcessSessionTracker.LaunchExitGracePeriod >= TimeSpan.FromSeconds(10),
            "A launcher-to-game handoff during startup was cut after the short exit grace period.");
        Assert(tracker.ShouldStop(t0.AddSeconds(5).Add(GameProcessSessionTracker.LaunchExitGracePeriod)),
            "The startup grace period never expired.");
        tracker.Start(game, 101, @"D:\Games\Alpha Game\Alpha.exe", t0);
        var late = t0.Add(GameProcessSessionTracker.LaunchPhase).AddMinutes(10);
        Assert(tracker.Remove(101, late) && tracker.ShouldStop(late.Add(GameProcessSessionTracker.ExitGracePeriod)),
            "Closing a game after a long session kept the bridge for the startup grace period.");

        Assert(GameProcessSessionTracker.GetInstallFolder(@"D:\Games\Alpha Game\Alpha.exe") == @"D:\Games\Alpha Game\" &&
               GameProcessSessionTracker.GetInstallFolder(@"D:\Games\Alpha.exe") == null &&
               GameProcessSessionTracker.GetInstallFolder(@"C:\Alpha.exe") == null &&
               GameProcessSessionTracker.GetInstallFolder("Alpha.exe") == null,
            "A shallow or relative folder was accepted as a game install folder.");
        tracker.Start(game, 102, @"D:\Games\Alpha Game\Alpha.exe", t0);
        Assert(tracker.IsInInstallFolder(@"D:\Games\Alpha Game\Alpha\Binaries\Win64\Alpha-Win64-Shipping.exe") &&
               !tracker.IsInInstallFolder(@"D:\Games\Alpha Game 2\Other.exe") &&
               !tracker.IsInInstallFolder(@"D:\Games\Other\Other.exe"),
            "The install-folder match accepted another game or missed the shipping executable.");

        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("alphagame", @"C:\Fake\Alpha Game\Alpha.exe");
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return executable; }, "Local\\ASB.HandoffTest." + Guid.NewGuid().ToString("N"), () => false);
        try
        {
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var stopped = typeof(ProcessMonitorService).GetMethod("HandleProcessStopped", flags);
                var expire = typeof(ProcessMonitorService).GetMethod("TryStopExpiredSession", flags);
                var session = (GameProcessSessionTracker)typeof(ProcessMonitorService).GetField("processSession", flags).GetValue(monitor);
                check.Invoke(monitor, new object[] { 8301u, "Alpha.exe", @"C:\Fake\Alpha Game\Alpha.exe", 0L, null, "test", false });
                Assert(manager.IsSessionActive && session.Contains(8301), "The launcher did not start the session.");
                check.Invoke(monitor, new object[] { 8302u, "Unreal-Win64-Shipping.exe", @"C:\Fake\Alpha Game\Unreal\Binaries\Win64\Unreal-Win64-Shipping.exe", 0L, null, "test", false });
                check.Invoke(monitor, new object[] { 8303u, "Helper.exe", @"C:\Fake\Other Tool\Helper.exe", 0L, null, "test", false });
                Assert(session.Contains(8302) && !session.Contains(8303) && launchAttempts == 1,
                    "The unmatched shipping executable in the game folder was not attached, or an unrelated process was.");
                stopped.Invoke(monitor, new object[] { 8301u, "test exit" });
                Assert(!session.IsAwaitingReplacement && !(bool)expire.Invoke(monitor, null) && manager.IsSessionActive,
                    "The launcher exit stopped the bridge although the game executable was still running.");
                stopped.Invoke(monitor, new object[] { 8302u, "test exit" });
                Assert(session.IsAwaitingReplacement && !(bool)expire.Invoke(monitor, null) && manager.IsSessionActive,
                    "The bridge stopped before the replacement grace period.");
                check.Invoke(monitor, new object[] { 8304u, "Unreal-Win64-Shipping.exe", @"C:\Fake\Alpha Game\Unreal\Binaries\Win64\Unreal-Win64-Shipping.exe", 0L, null, "test", false });
                Assert(session.Contains(8304) && !session.IsAwaitingReplacement && launchAttempts == 1 && manager.IsSessionActive,
                    "A game process restarted during loading restarted the bridge instead of reattaching.");
                manager.StopSession("Test stop");
            }
        }
        finally { manager.Dispose(); }
    }

    private static void TestForegroundSwitchesNeverRestartSession()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[" +
            "{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}," +
            "{\"title\":\"Beta Game\",\"normalized\":\"betagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        const string alphaPath = @"C:\Fake\Alpha Game\Alpha.exe";
        settings.SetGameExecutable("alphagame", alphaPath);
        settings.SetGameExecutable("betagame", @"C:\Fake\Beta Game\Beta.exe");
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return executable; },
            "Local\\ASB.AltTabTest." + Guid.NewGuid().ToString("N"), () => false);
        try
        {
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var prune = typeof(ProcessMonitorService).GetMethod("PruneExitedTrackedProcesses", flags);
                var release = typeof(ProcessMonitorService).GetMethod("ReleaseEndedSession", flags);
                var expire = typeof(ProcessMonitorService).GetMethod("TryStopExpiredSession", flags);
                var session = (GameProcessSessionTracker)typeof(ProcessMonitorService).GetField("processSession", flags).GetValue(monitor);
                int detections = 0;
                monitor.GameDetected += (game, path) => detections++;
                var creation = new Dictionary<uint, long> { { 8501, 1000 }, { 8502, 2000 } };
                monitor.livenessSource = (uint id, long expected, out long ticks) =>
                {
                    ticks = 0;
                    if (!creation.TryGetValue(id, out ticks)) return ProcessLivenessState.Exited;
                    return expected > 0 && expected != ticks ? ProcessLivenessState.Replaced : ProcessLivenessState.Alive;
                };

                check.Invoke(monitor, new object[] { 8501u, "Alpha.exe", alphaPath, 0L, null, "poll", false });
                Assert(manager.IsSessionActive && session.Contains(8501) && launchAttempts == 1 && detections == 1,
                    "The game did not start one session.");

                for (int altTab = 0; altTab < 25; altTab++)
                {
                    check.Invoke(monitor, new object[] { 8601u, "Browser.exe", @"C:\Fake\Browser\Browser.exe", 0L, null, "foreground", false });
                    check.Invoke(monitor, new object[] { 8602u, "Beta.exe", @"C:\Fake\Beta Game\Beta.exe", 0L, null, "foreground", false });
                    prune.Invoke(monitor, null);
                    release.Invoke(monitor, null);
                    Assert(!(bool)expire.Invoke(monitor, null), "A foreground switch expired the session.");
                }
                Assert(manager.IsSessionActive && session.Contains(8501) && !session.Contains(8601) && !session.Contains(8602) &&
                       !session.IsAwaitingReplacement && launchAttempts == 1 && detections == 1 &&
                       manager.ActiveGameTitle == "Alpha Game",
                    "Alt+Tab to another application or game restarted, detached or replaced the session.");

                check.Invoke(monitor, new object[] { 8502u, "Alpha.exe", alphaPath, 0L, null, "foreground", false });
                check.Invoke(monitor, new object[] { 8502u, "Alpha.exe", alphaPath, 0L, null, "foreground", false });
                prune.Invoke(monitor, null);
                Assert(session.Contains(8501) && session.Contains(8502) && launchAttempts == 1 && detections == 1,
                    "The game window regaining focus as a second process restarted the session instead of attaching.");

                monitor.livenessSource = ProcessLiveness.Check;
                manager.StopSession("Test stop");
            }
        }
        finally { manager.Dispose(); }
    }

    private const uint MissingProcessId = 0x7FFFFFF0;

    private static Process FindProtectedProcess()
    {
        foreach (var name in new[] { "csrss", "smss", "wininit", "services", "lsass" })
        {
            var candidates = Process.GetProcessesByName(name);
            foreach (var candidate in candidates)
            {
                bool denied;
                try { GC.KeepAlive(candidate.HasExited); denied = false; }
                catch (Exception) { denied = true; }
                if (denied)
                {
                    foreach (var other in candidates) if (!ReferenceEquals(other, candidate)) other.Dispose();
                    return candidate;
                }
                candidate.Dispose();
            }
        }
        return null;
    }

    private static void TestProcessLivenessClassification()
    {
        using (var current = Process.GetCurrentProcess())
        {
            long ticks;
            Assert(ProcessLiveness.Check((uint)current.Id, 0, out ticks) == ProcessLivenessState.Alive &&
                   ticks == current.StartTime.ToUniversalTime().Ticks,
                "The current process was not alive or its creation time differed from Process.StartTime.");
            Assert(ProcessLiveness.Check((uint)current.Id, ticks) == ProcessLivenessState.Alive &&
                   ProcessLiveness.Check((uint)current.Id, ticks + TimeSpan.TicksPerSecond) == ProcessLivenessState.Replaced,
                "A reused PID with another creation time was not reported as a different process.");
        }

        Assert(ProcessLiveness.Check(0, 0) == ProcessLivenessState.Exited &&
               ProcessLiveness.Check(MissingProcessId, 0) == ProcessLivenessState.Exited &&
               !ProcessLiveness.IsRunning(MissingProcessId, 0) &&
               ProcessLiveness.TryGetCreationTimeUtcTicks(MissingProcessId) == 0,
            "A missing PID was not reported as exited.");

        var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "cmd.exe"), "/c exit 3")
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        uint exitedId;
        using (var child = Process.Start(start))
        {
            exitedId = (uint)child.Id;
            Assert(child.WaitForExit(10000), "The short-lived child process did not exit.");
            Assert(ProcessLiveness.Check(exitedId, 0) == ProcessLivenessState.Exited,
                "An exited process whose handle is still open was not reported as exited.");
        }
        var afterClose = ProcessLiveness.Check(exitedId, 0);
        Assert(afterClose == ProcessLivenessState.Exited || afterClose == ProcessLivenessState.Alive,
            "A closed exited process was classified as unknown.");
        if (afterClose == ProcessLivenessState.Alive)
            Console.WriteLine("NOTE: PID {0} was reused immediately after the child exited.", exitedId);

        var protectedProcess = FindProtectedProcess();
        if (protectedProcess == null)
        {
            Console.WriteLine("NOTE: no access-denied system process found; protected liveness check skipped.");
            return;
        }
        using (protectedProcess)
        {
            var pid = (uint)protectedProcess.Id;
            bool oldLogicSaysExited;
            try { oldLogicSaysExited = protectedProcess.HasExited; }
            catch (Exception) { oldLogicSaysExited = true; }
            Assert(oldLogicSaysExited, "Process.HasExited unexpectedly succeeded on " + protectedProcess.ProcessName + ".");

            long ticks;
            Assert(ProcessLiveness.Check(pid, 0, out ticks) == ProcessLivenessState.Alive && ticks > 0,
                "The protected process " + protectedProcess.ProcessName + " was not seen alive with a creation time.");
            Assert(ProcessLiveness.IsRunning(pid, ticks) &&
                   ProcessLiveness.TryGetCreationTimeUtcTicks(pid) == ticks &&
                   ProcessLiveness.Check(pid, ticks - TimeSpan.TicksPerSecond) == ProcessLivenessState.Replaced,
                "The protected process identity check did not detect a different creation time.");
        }
    }

    private static void TestProtectedGameProcessStaysAttached(string testRoot)
    {
        var protectedProcess = FindProtectedProcess();
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        const string gamePath = @"C:\Fake\Alpha Game\Alpha.exe";
        settings.SetGameExecutable("alphagame", gamePath);
        int launchAttempts = 0;
        var manager = new EngineSessionManager(() => { launchAttempts++; return executable; }, "Local\\ASB.ProtectedTest." + Guid.NewGuid().ToString("N"), () => false);
        var flags = BindingFlags.Instance | BindingFlags.NonPublic;
        var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
        var prune = typeof(ProcessMonitorService).GetMethod("PruneExitedTrackedProcesses", flags);
        var release = typeof(ProcessMonitorService).GetMethod("ReleaseEndedSession", flags);
        var expire = typeof(ProcessMonitorService).GetMethod("TryStopExpiredSession", flags);
        var observing = typeof(ProcessMonitorService).GetMethod("IsExecutableObservationActive", flags);
        try
        {
            using (var learning = new ExecutableLearningService(Path.Combine(testRoot, "protected-learning.json"), TimeSpan.FromHours(1)))
            using (var monitor = new ProcessMonitorService(catalog, manager, learning, settings, false))
            {
                var session = (GameProcessSessionTracker)typeof(ProcessMonitorService).GetField("processSession", flags).GetValue(monitor);
                if (protectedProcess == null)
                {
                    Console.WriteLine("NOTE: no access-denied system process found; protected session simulation skipped.");
                }
                else
                {
                    using (protectedProcess)
                    {
                        var pid = (uint)protectedProcess.Id;
                        check.Invoke(monitor, new object[] { pid, "Alpha.exe", gamePath, 0L, null, "test", false });
                        Assert(manager.IsSessionActive && session.Contains(pid) && launchAttempts == 1 && learning.PendingCount == 1,
                            "The protected game process did not start a session with a pending observation.");
                        for (int tick = 0; tick < 40; tick++)
                        {
                            prune.Invoke(monitor, null);
                            release.Invoke(monitor, null);
                            Assert(!(bool)expire.Invoke(monitor, null), "The protected game session expired.");
                        }
                        Assert(session.Contains(pid) && !session.IsAwaitingReplacement && manager.IsSessionActive &&
                               launchAttempts == 1 && learning.PendingCount == 1,
                            "A living protected game process was detached, restarted or had its learning cancelled.");
                        var game = catalog.GetAllGames().First(g => g.Normalized == "alphagame");
                        var systemPath = Path.Combine(Environment.SystemDirectory, protectedProcess.ProcessName + ".exe");
                        Assert((bool)observing.Invoke(monitor, new object[] { pid, systemPath, ProcessLiveness.TryGetCreationTimeUtcTicks(pid), game }) &&
                               !(bool)observing.Invoke(monitor, new object[] { pid, systemPath, ProcessLiveness.TryGetCreationTimeUtcTicks(pid) - TimeSpan.TicksPerSecond, game }),
                            "Learning did not keep observing a protected process, or ignored a reused PID.");
                    }
                    manager.StopSession("Test stop");
                    release.Invoke(monitor, null);
                }

                launchAttempts = 0;
                var creation = new Dictionary<uint, long> { { 8401, 1000 }, { 8402, 2000 } };
                bool unknown = false;
                monitor.livenessSource = (uint id, long expected, out long ticks) =>
                {
                    ticks = 0;
                    if (unknown) return ProcessLivenessState.Unknown;
                    if (!creation.TryGetValue(id, out ticks)) return ProcessLivenessState.Exited;
                    return expected > 0 && expected != ticks ? ProcessLivenessState.Replaced : ProcessLivenessState.Alive;
                };
                check.Invoke(monitor, new object[] { 8401u, "Alpha.exe", gamePath, 0L, null, "test", false });
                check.Invoke(monitor, new object[] { 8402u, "Alpha.exe", gamePath, 0L, null, "test", false });
                Assert(manager.IsSessionActive && session.Contains(8401) && session.Contains(8402) && launchAttempts == 1,
                    "The simulated game processes were not attached to one session.");
                unknown = true;
                for (int tick = 0; tick < 10; tick++) prune.Invoke(monitor, null);
                Assert(session.Contains(8401) && session.Contains(8402), "An unclassified liveness result detached a game process.");
                unknown = false;
                prune.Invoke(monitor, null);
                creation[8401] = 5000;
                prune.Invoke(monitor, null);
                Assert(!session.Contains(8401) && session.Contains(8402) && manager.IsSessionActive,
                    "A PID reused by another process stayed attached to the game session.");
                creation.Remove(8402);
                prune.Invoke(monitor, null);
                Assert(!session.Contains(8402) && session.IsAwaitingReplacement,
                    "An exited game process was not detached.");
                monitor.livenessSource = ProcessLiveness.Check;
                manager.StopSession("Test stop");
            }
        }
        finally { manager.Dispose(); }
    }

    private static void TestPreparedLaunchLifecycle()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Silent Game\",\"normalized\":\"silentgame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        var manager = new EngineSessionManager(() => executable, "Local\\ASB.PreparedTest." + Guid.NewGuid().ToString("N"), () => false);
        const string launcher = @"C:\Fake\Silent Game\Silent.exe";
        const string shipping = @"C:\Fake\Silent Game\Silent\Binaries\Win64\Silent-Win64-Shipping.exe";
        var running = new Dictionary<uint, string> { { 4, null } };
        try
        {
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                monitor.processIdSource = () => running.Keys.ToArray();
                monitor.processPathSource = pid => { string path; return running.TryGetValue(pid, out path) ? path : null; };
                var exited = new List<string>();
                monitor.GameExited += path => exited.Add(path);
                var t0 = DateTime.UtcNow;
                string error;

                settings.ForcedProfile = "standard";
                Assert(manager.StartSession("Silent Game", "standard", settings, out error), "The prepared session did not start: " + error);
                running[9001] = launcher;
                monitor.TrackPreparedLaunch(launcher, "Silent Game", 9001);
                Assert(monitor.HasPreparedLaunch && !monitor.TryEndPreparedLaunch(t0.AddSeconds(1)), "A running prepared game ended its session.");
                running.Remove(9001);
                running[9002] = shipping;
                Assert(!monitor.TryEndPreparedLaunch(t0.AddSeconds(2)) && !monitor.TryEndPreparedLaunch(t0.AddSeconds(30)),
                    "The launcher-to-game handoff of a prepared launch stopped the bridge.");
                running.Remove(9002);
                var gone = t0.AddSeconds(31);
                Assert(!monitor.TryEndPreparedLaunch(gone) &&
                       !monitor.TryEndPreparedLaunch(gone.Add(PreparedLaunchTracker.ExitGracePeriod).AddSeconds(-1)) && manager.IsSessionActive,
                    "The prepared session stopped before its exit grace period.");
                Assert(monitor.TryEndPreparedLaunch(gone.Add(PreparedLaunchTracker.ExitGracePeriod)),
                    "Closing the prepared game did not end its session.");
                Assert(!manager.IsSessionActive && !manager.IsBusy && manager.StateName == "Stopped" && manager.ActiveGameTitle == "Aucun" &&
                       settings.ForcedProfile == "none" && !monitor.HasPreparedLaunch && exited.Count == 1 && exited[0] == launcher,
                    "The prepared game exit left a stale session, forced mode or dashboard game.");
                Assert(manager.StartSession("Other Game", "standard", settings, out error), "A new start was refused after the prepared game closed: " + error);
                manager.StopSession("Test stop");

                settings.ForcedProfile = "standard";
                Assert(manager.StartSession("Forced manual bridge", "standard", settings, out error), "The forced session did not start: " + error);
                monitor.TrackPreparedLaunch(launcher, "Silent Game", 0);
                Assert(!monitor.TryEndPreparedLaunch(DateTime.UtcNow.AddMinutes(5)) && manager.IsSessionActive &&
                       settings.ForcedProfile == "standard" && !monitor.HasPreparedLaunch,
                    "A stale prepared launch stopped another forced session.");
                monitor.TrackPreparedLaunch(launcher, "Forced manual bridge", 0);
                monitor.CancelPreparedLaunch();
                Assert(!monitor.HasPreparedLaunch && !monitor.TryEndPreparedLaunch(DateTime.UtcNow.AddMinutes(5)) && manager.IsSessionActive,
                    "Turning forced activation off or on did not cancel the prepared launch.");
                manager.StopSession("Test stop");
            }
        }
        finally
        {
            settings.ForcedProfile = "none";
            manager.Dispose();
        }
    }

    private static void TestIdentityFailureQuickRetry()
    {
        var executable = Assembly.GetExecutingAssembly().Location;
        var catalog = CreateGameList("{\"games\":[{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\",\"profile\":\"standard\",\"adaptiveTriggers\":true}]}");
        var settings = new TraySettings();
        settings.SetGameExecutable("alphagame", @"C:\Fake\Alpha.exe");
        var manager = new EngineSessionManager(() => executable, "Local\\ASB.QuickRetryTest." + Guid.NewGuid().ToString("N"), () => false);
        Environment.SetEnvironmentVariable("ASB_TEST_FAKE_FAILURE", "3|APEX identity check failed: receiver asleep (test).");
        try
        {
            string error;
            int exitCode;
            Assert(!manager.StartSession("Alpha Game", "standard", settings, 0, out error, out exitCode) &&
                   exitCode == 3 && error == "APEX identity check failed: receiver asleep (test).",
                "The engine failure exit code was not reported: " + exitCode + " " + error);
            using (var monitor = new ProcessMonitorService(catalog, manager, null, settings, false))
            {
                var flags = BindingFlags.Instance | BindingFlags.NonPublic;
                var check = typeof(ProcessMonitorService).GetMethod("CheckCandidateProcess", flags);
                var cooldowns = (Dictionary<uint, DateTime>)typeof(ProcessMonitorService).GetField("retryCooldowns", flags).GetValue(monitor);
                var paused = new List<string>();
                monitor.AutomaticStartPaused += (title, reason) => paused.Add(title);
                Action<uint> attemptProcess = pid => check.Invoke(monitor, new object[] { pid, "Alpha.exe", @"C:\Fake\Alpha.exe", 0L, null, "test", false });
                var timers = (System.Collections.IDictionary)typeof(ProcessMonitorService).GetField("quickRetryTimers", flags).GetValue(monitor);

                attemptProcess(4009);
                lock (cooldowns) cooldowns[4009] = DateTime.MaxValue;
                attemptProcess(4001);
                DateTime retryAt;
                lock (cooldowns) retryAt = cooldowns[4001];
                Assert(retryAt <= DateTime.UtcNow.AddSeconds(2.5) && paused.Count == 0,
                    "An identity check failure waited for the normal detection cycle.");
                WaitUntil(() => { lock (cooldowns) return timers.Count == 0; }, TimeSpan.FromSeconds(5),
                    "Quick retry timers were not released.");
                lock (cooldowns) retryAt = cooldowns[4009];
                Assert(retryAt == DateTime.MaxValue, "A quick retry released a cooldown it did not schedule.");

                var now = DateTime.UtcNow;
                monitor.retryClock = () => now;
                for (int i = 0; i < 3; i++)
                {
                    lock (cooldowns) cooldowns.Remove(4001);
                    attemptProcess(4001);
                    lock (cooldowns) retryAt = cooldowns[4001];
                    Assert(paused.Count == 0 && !monitor.HasPausedAutomaticStart &&
                           retryAt > now.AddSeconds(8) && retryAt < DateTime.MaxValue,
                        "Repeated identity check failures stopped the normal automatic retries.");
                    now = now.Add(StartRetryPolicy.RetryDelay).AddSeconds(1);
                }
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable("ASB_TEST_FAKE_FAILURE", null);
            manager.Dispose();
        }
    }

    private static void TestDatabaseExecutableResolutionAndCollisions()
    {
        const string json = "{\"games\":[" +
            "{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true,\"profile\":\"standard\"," +
            "\"steamAppId\":10,\"steamAppIdVerified\":true,\"executables\":[\"Bin/AlphaGame.exe\",\"Shared.exe\",\"not-a-program\"]}," +
            "{\"title\":\"Beta Game\",\"normalized\":\"betagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true,\"profile\":\"standard\"," +
            "\"steamAppId\":20,\"steamAppIdVerified\":true,\"executables\":[\"BetaGame.exe\",\"shared.EXE\"]}]}";
        var gameList = CreateGameList(json);

        SupportedGame game;
        Assert(gameList.TryFindByExecutable(@"D:\Games\Alpha\ALPHAGAME.EXE", out game),
            "A database executable did not resolve case-insensitively by basename.");
        Assert(game != null && game.Title == "Alpha Game",
            "A database executable resolved to the wrong game.");
        Assert(!gameList.TryFindByExecutable(@"D:\Games\Shared.exe", out game),
            "An executable shared by two games was not rejected as ambiguous.");
        Assert(!gameList.TryFindByExecutable("not-a-program", out game),
            "A non-executable database value was indexed.");

        const string unverifiedJson = "{\"games\":[{" +
            "\"title\":\"Unverified Game\",\"normalized\":\"unverifiedgame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true," +
            "\"profile\":\"standard\",\"steamAppId\":999," +
            "\"steamAppIdVerified\":false,\"executables\":[\"Unsafe.exe\"]}]}";
        var unverifiedList = CreateGameList(unverifiedJson);
        Assert(!unverifiedList.TryFindByExecutable("Unsafe.exe", out game),
            "An executable tied to an unverified Steam AppID entered the runtime index.");
        Assert(!unverifiedList.TryFindBySteamAppId(999, out game),
            "An unverified Steam AppID entered the runtime identity index.");

        const string replacementJson = "{\"games\":[{" +
            "\"title\":\"Gamma Game\",\"normalized\":\"gammagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true," +
            "\"profile\":\"standard\",\"steamAppId\":30,\"steamAppIdVerified\":true," +
            "\"executables\":[\"GammaGame.exe\"]}]}";
        ReloadGameList(gameList, replacementJson);
        Assert(!gameList.TryFindByExecutable("AlphaGame.exe", out game),
            "Reloading the cloud database retained a stale executable mapping.");
        Assert(gameList.TryFindByExecutable("GammaGame.exe", out game) &&
               game != null && game.Title == "Gamma Game",
            "Reloading the cloud database did not publish the new executable snapshot.");
    }

    private static void TestDatabaseExecutableMissPerformance()
    {
        var gameList = CreateGameList();
        SupportedGame ignored;
        const string missingPath = @"E:\Games\Missing\Missing.exe";

        for (int i = 0; i < 5000; i++) gameList.TryFindByExecutable(missingPath, out ignored);

        var samples = new long[20000];
        for (int i = 0; i < samples.Length; i++)
        {
            long started = Stopwatch.GetTimestamp();
            gameList.TryFindByExecutable(missingPath, out ignored);
            samples[i] = Stopwatch.GetTimestamp() - started;
        }
        Array.Sort(samples);
        double p99Milliseconds = samples[(samples.Length * 99) / 100] * 1000.0 / Stopwatch.Frequency;
        Console.WriteLine("Database-executable miss p99: {0:F6} ms", p99Milliseconds);
        Assert(p99Milliseconds <= 0.05,
            "The database-executable miss exceeded the 0.05 ms p99 budget.");
    }

    private static void TestGameProcessSessionPidHandoff()
    {
        var gameList = CreateGameList();
        var game = FindGame(gameList, "Alpha Game");
        var tracker = new GameProcessSessionTracker();
        var startedAt = new DateTime(2026, 9, 3, 12, 0, 0, DateTimeKind.Utc);
        var grace = TimeSpan.FromSeconds(2);

        tracker.Start(game, 100, @"D:\Games\Alpha\Launcher.exe");
        Assert(tracker.HasSession && tracker.ProcessCount == 1,
            "Starting a detected game did not register its first PID.");

        Assert(tracker.TryAttach(game, 101, @"D:\Games\Alpha\AlphaGame.exe"),
            "A second PID for the same game was not attached.");
        Assert(tracker.ProcessCount == 2,
            "Attaching the game executable replaced the launcher PID instead of tracking both.");

        Assert(tracker.Remove(100, startedAt, grace),
            "The launcher PID was not removed.");
        Assert(tracker.ProcessCount == 1 && !tracker.IsAwaitingReplacement,
            "Closing one PID incorrectly put a multi-PID session into its exit grace period.");

        Assert(tracker.Remove(101, startedAt, grace),
            "The final game PID was not removed.");
        Assert(tracker.IsAwaitingReplacement,
            "Closing the final game PID did not start the replacement grace period.");
        Assert(!tracker.ShouldStop(startedAt.AddMilliseconds(1999)),
            "The game session expired before the full PID replacement grace period.");

        Assert(tracker.TryAttach(game, 102, @"D:\Games\Alpha\AlphaGame-Win64.exe"),
            "A replacement PID for the same game was not accepted during the grace period.");
        Assert(!tracker.IsAwaitingReplacement && !tracker.ShouldStop(startedAt.AddSeconds(3)),
            "Attaching a replacement PID did not cancel the pending teardown.");

        var otherGame = new SupportedGame
        {
            Title = "Beta Game",
            Normalized = "betagame",
            SteamAppId = 654321,
            SteamAppIdVerified = true
        };
        Assert(!tracker.TryAttach(otherGame, 200, @"D:\Games\Beta\BetaGame.exe"),
            "A PID belonging to another game was attached to the active session.");

        Assert(tracker.Remove(102, startedAt, grace),
            "The replacement game PID was not removed.");
        Assert(tracker.ShouldStop(startedAt.AddSeconds(2)),
            "The session did not expire after the replacement grace period elapsed.");
    }

    private static void TestPlatformClientsNeverCountAsGameProcesses()
    {
        var excluded = new[]
        {
            "steam.exe",
            @"C:\Program Files (x86)\Steam\steamwebhelper.exe",
            "Battle.net.exe",
            "Agent.exe",
            "EpicGamesLauncher.exe",
            "EADesktop.exe",
            "UbisoftConnect.exe",
            "GalaxyClient.exe",
            "RockstarGamesLauncher.exe",
            "XboxPcApp.exe",
            "RiotClientServices.exe",
            "Playnite.FullscreenApp.exe"
        };

        foreach (var executable in excluded)
        {
            Assert(PlatformClientProcessFilter.IsExcluded(executable),
                "A generic game-platform client was allowed to count as a game PID: " + executable);
        }

        Assert(!PlatformClientProcessFilter.IsExcluded("GenshinImpact.exe"),
            "A real game executable was rejected as a generic platform client.");
        Assert(!PlatformClientProcessFilter.IsExcluded("launcher.exe"),
            "A game-specific launcher name was rejected without evidence that it is a platform client.");
    }

    private static void TestShortGameNamesCannotFuzzyMatchUnrelatedProcesses()
    {
        const string json = "{\"games\":[" +
            "{\"title\":\"Control\",\"normalized\":\"control\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":false," +
            "\"profile\":\"standard\",\"steamAppId\":0,\"steamAppIdVerified\":false}," +
            "{\"title\":\"Alpha Game\",\"normalized\":\"alphagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true," +
            "\"profile\":\"standard\",\"steamAppId\":123456,\"steamAppIdVerified\":true}]}";
        var gameList = CreateGameList(json);

        SupportedGame resolved;
        Assert(gameList.TryFindExactGame("Control", out resolved) &&
               resolved != null && resolved.Title == "Control",
            "The real Control title no longer resolved exactly.");
        Assert(!gameList.TryFindGame("Controlify", out resolved),
            "Controlify incorrectly activated the game Control.");
        Assert(!gameList.TryFindGame("Game Controller Support", out resolved),
            "A generic controller process incorrectly activated the game Control.");
        Assert(!gameList.TryFindGame("Flydigi Control Center", out resolved),
            "A controller utility incorrectly activated the game Control.");
        Assert(gameList.TryFindGame("Alpha Game Deluxe Edition", out resolved) &&
               resolved != null && resolved.Title == "Alpha Game",
            "A sufficiently specific catalogue title lost safe fuzzy matching.");
    }

    private static void TestPidTrackingFastPathPerformance()
    {
        var tracker = new GameProcessSessionTracker();
        var game = new SupportedGame
        {
            Title = "Alpha Game",
            Normalized = "alphagame",
            SteamAppId = 123456,
            SteamAppIdVerified = true
        };
        tracker.Start(game, 100, @"D:\Games\Alpha\AlphaGame.exe");

        var sync = new object();
        for (int i = 0; i < 5000; i++)
        {
            lock (sync) tracker.Contains(999);
            PlatformClientProcessFilter.IsExcluded("AlphaGame.exe");
        }

        var samples = new long[20000];
        for (int i = 0; i < samples.Length; i++)
        {
            long started = Stopwatch.GetTimestamp();
            lock (sync) tracker.Contains(999);
            PlatformClientProcessFilter.IsExcluded("AlphaGame.exe");
            samples[i] = Stopwatch.GetTimestamp() - started;
        }

        Array.Sort(samples);
        double p99Milliseconds = samples[(samples.Length * 99) / 100] * 1000.0 / Stopwatch.Frequency;
        Console.WriteLine("PID/filter fast-path p99: {0:F6} ms", p99Milliseconds);
        Assert(p99Milliseconds <= 0.05,
            "The PID and platform-client checks exceeded the 0.05 ms p99 budget.");
    }

    private static void TestGeneratedDatabaseExecutableCoverage()
    {
        var databasePath = Path.GetFullPath(Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory,
            "..", "..", "..", "data", "supported_games.json"));
        Assert(File.Exists(databasePath), "The generated supported-games database is missing.");

        var gameList = CreateGameList(File.ReadAllText(databasePath, Encoding.UTF8));
        var executableGames = gameList.GetAllGames().Count(game => game.Executables.Length > 0);
        Assert(executableGames >= 100,
            "The generated database contains suspiciously few Discord executable mappings.");
        var manualFixGames = gameList.GetAllGames().Where(game => game.RequiresManualFix).ToList();
        Assert(manualFixGames.Count >= 20,
            "The generated database lost the PCGamingWiki RequireManualFix metadata.");
        Assert(manualFixGames.All(game => !string.IsNullOrWhiteSpace(game.ManualFixUrl)),
            "A manual-fix game has no PCGamingWiki guidance URL.");
        Assert(manualFixGames.Count(game => ManualFixGuide.BuildEntries(game).Count > 0) >= 20,
            "The generated database lost the PCGamingWiki manual-fix notes.");
        var translatableGames = manualFixGames.Where(game => ManualFixGuide.BuildEntries(game, "fr")
            .Any(entry => entry.Note.Length > 3 && !ManualFixGuide.BuildEntries(game, "en")
                .Any(english => english.Note == entry.Note))).Count();
        Assert(translatableGames >= 25,
            "The generated database lost the translations of the manual-fix notes.");

        SupportedGame resolved;
        Assert(gameList.TryFindByExecutable(@"C:\Games\Apex\r5apex.exe", out resolved) &&
               resolved != null && resolved.Title == "Apex Legends",
            "The generated Discord executable index did not resolve a known supported game.");
        Assert(gameList.TryFindByExecutable(
                   @"C:\Program Files (x86)\Steam\steamapps\common\Call of Duty HQ\cod.exe",
                   out resolved) &&
               resolved != null && resolved.Title == "Call of Duty",
            "The shared Call of Duty HQ executable was not detected.");
    }

    // Issue #29: "addedAt" drives the "Recently added" home shelf.
    private static void TestCatalogAddedDates()
    {
        var gameList = CreateGameList("{\"games\":[" +
            "{\"title\":\"Dated\",\"normalized\":\"dated\",\"adaptiveTriggers\":true,\"addedAt\":\"2026-09-29\"}," +
            "{\"title\":\"Undated\",\"normalized\":\"undated\",\"hapticFeedback\":true}," +
            "{\"title\":\"Bad Date\",\"normalized\":\"baddate\",\"hapticFeedback\":true,\"addedAt\":\"2026-02-30\"}," +
            "{\"title\":\"Timestamp\",\"normalized\":\"timestamp\",\"hapticFeedback\":true,\"addedAt\":\"2026-09-29T10:00:00Z\"}]}");
        var games = gameList.GetAllGames().ToDictionary(game => game.Normalized);
        Assert(games["dated"].AddedAt == new DateTime(2026, 9, 29), "A valid catalogue addedAt date was not parsed.");
        Assert(games["dated"].AddedAt.Value.Kind == DateTimeKind.Utc, "addedAt must be a UTC calendar date.");
        Assert(games["undated"].AddedAt == null, "A game without addedAt must stay undated.");
        Assert(games["baddate"].AddedAt == null, "An impossible addedAt date was accepted.");
        Assert(games["timestamp"].AddedAt == null, "Only plain YYYY-MM-DD addedAt values are accepted.");

        var databasePath = Path.GetFullPath(Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "data", "supported_games.json"));
        var generated = CreateGameList(File.ReadAllText(databasePath, Encoding.UTF8)).GetAllGames();
        Assert(generated.All(game => game.AddedAt.HasValue), "Every generated catalogue entry should carry addedAt.");
        Assert(generated.Any(game => game.AddedAt.Value > generated.Min(other => other.AddedAt.Value)),
            "The generated catalogue lost its post-import additions.");
    }

    private static CloudGameListService CreateGameList()
    {
        const string json = "{\"games\":[{" +
            "\"title\":\"Alpha Game\",\"normalized\":\"alphagame\"," +
            "\"adaptiveTriggers\":true,\"hapticFeedback\":true," +
            "\"profile\":\"standard\",\"steamAppId\":123456,\"steamAppIdVerified\":true}]}";
        return CreateGameList(json);
    }

    private static CloudGameListService CreateGameList(string json)
    {
        var service = new CloudGameListService();
        ReloadGameList(service, json);
        return service;
    }

    private static void ReloadGameList(CloudGameListService service, string json)
    {
        var method = typeof(CloudGameListService).GetMethod(
            "ParseAndLoadJson", BindingFlags.Instance | BindingFlags.NonPublic);
        Assert(method != null && (bool)method.Invoke(service, new object[] { json }),
            "The test game database could not be loaded.");
    }

    private static SupportedGame FindGame(CloudGameListService service, string title)
    {
        SupportedGame game;
        Assert(service.TryFindExactGame(title, out game), "The expected test game was not found.");
        return game;
    }

    private static void WaitUntil(Func<bool> condition, TimeSpan timeout, string message)
    {
        var started = Stopwatch.StartNew();
        while (started.Elapsed < timeout)
        {
            if (condition()) return;
            Thread.Sleep(10);
        }
        throw new InvalidOperationException(message);
    }

    private static bool FileExistsWithoutText(string path, string unwantedText)
    {
        try
        {
            return File.Exists(path) && !File.ReadAllText(path).Contains(unwantedText);
        }
        catch (IOException)
        {
            // Atomic persistence can hold the destination for a few
            // milliseconds. WaitUntil will retry until the writer releases it.
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static void TestSteamAlreadyRunningDetection()
    {
        var session = new DateTime(2026, 10, 9, 12, 0, 0, DateTimeKind.Utc);
        var before = session.AddMinutes(-30);
        var restarted = session.AddMinutes(-5);
        DateTime? steamStart;

        Assert(!SteamProcessChecker.Decide(new DateTime?[0], session, null, out steamStart),
            "No Steam process must not warn");
        Assert(!SteamProcessChecker.Decide(new DateTime?[] { session.AddSeconds(5) }, session, null, out steamStart),
            "Steam started after isolation must not warn");
        Assert(SteamProcessChecker.Decide(new DateTime?[] { before }, session, null, out steamStart) && steamStart == before,
            "Steam already running when isolation started must warn");
        Assert(!SteamProcessChecker.Decide(new DateTime?[] { before }, session, before, out steamStart),
            "The same Steam instance must be warned only once");
        Assert(SteamProcessChecker.Decide(new DateTime?[] { restarted }, session, before, out steamStart) && steamStart == restarted,
            "A restarted Steam instance that predates the session must warn again");
        Assert(SteamProcessChecker.Decide(new DateTime?[] { null }, session, null, out steamStart),
            "An unreadable Steam start time is treated as already running");
        Assert(!SteamProcessChecker.Decide(new DateTime?[] { null }, session, DateTime.MinValue, out steamStart),
            "An unreadable Steam start time is warned only once");
    }

    private static void TestInstallerIdentityIgnoresVersionResourcePadding(string root)
    {
        string padding = new string(' ', 45);

        Assert(ReleaseSecurity.NormalizeVersionField(null) == string.Empty,
            "a missing version field should normalize to an empty string");
        Assert(ReleaseSecurity.NormalizeVersionField(new string(' ', 20)) == string.Empty,
            "an all-padding field such as the blank Inno FileVersion should normalize to empty");
        Assert(ReleaseSecurity.NormalizeVersionField("ApexSenseBridge" + padding) == "ApexSenseBridge",
            "trailing spaces added by Inno Setup should be removed");
        Assert(ReleaseSecurity.NormalizeVersionField("\0 \t1.0.2 \0\r\n \0") == "1.0.2",
            "mixed leading and trailing whitespace and NUL padding should be removed");
        Assert(ReleaseSecurity.NormalizeVersionField("Apex SenseBridge") == "Apex SenseBridge",
            "interior spaces are part of the value and must be preserved");
        Assert(ReleaseSecurity.NormalizeVersionField("Apex\0SenseBridge") == "Apex\0SenseBridge",
            "interior NUL characters are part of the value and must be preserved");

        string[] acceptedProducts =
        {
            "ApexSenseBridge",
            "ApexSenseBridge" + padding,
            "ApexSenseBridge\0\0",
            "  apexsensebridge \0 "
        };
        foreach (string product in acceptedProducts)
        {
            Assert(ReleaseSecurity.IsExpectedProductName(product),
                "a padded ApexSenseBridge product name should be accepted: [" + product + "]");
        }

        string[] rejectedProducts =
        {
            null,
            string.Empty,
            padding,
            "ApexSenseBridgeTray",
            "ApexSenseBridge Setup" + padding,
            "Apex SenseBridge",
            "ApexSenseBridge\0Tray",
            "USBip" + padding
        };
        foreach (string product in rejectedProducts)
        {
            Assert(!ReleaseSecurity.IsExpectedProductName(product),
                "a different product name must be rejected: [" + product + "]");
        }

        Assert(ReleaseSecurity.MatchesReleaseVersion("1.0.2" + padding, "v1.0.2"),
            "a space-padded product version should match the release tag");
        Assert(ReleaseSecurity.MatchesReleaseVersion("1.0.2\0" + padding, "1.0.2"),
            "a NUL-then-space padded product version should match the release tag");
        Assert(ReleaseSecurity.MatchesReleaseVersion(" 1.0.2.0 ", " V1.0.2 "),
            "padding on the release tag side should be ignored too");
        Assert(ReleaseSecurity.MatchesReleaseVersion("1.0.2", "1.0.2" + padding),
            "a padded expected version should match an exact product version");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("1.0.3" + padding, "1.0.2"),
            "a different patch version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("1.1.2" + padding, "1.0.2"),
            "a different minor version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("2.0.2" + padding, "1.0.2"),
            "a different major version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("1.0" + padding, "1.0.2"),
            "a product version without a build number must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion(string.Empty, "1.0.2"),
            "a blank product version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion(null, "1.0.2"),
            "a missing product version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("garbage" + padding, "1.0.2"),
            "an unparsable product version must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("1.0.2", "garbage"),
            "an unparsable release tag must be rejected");
        Assert(!ReleaseSecurity.MatchesReleaseVersion("1.0.2", padding),
            "a blank release tag must be rejected");

        const string subject = "CN=ApexSenseBridge contributors, O=ApexSenseBridge";
        Assert(ReleaseSecurity.IsSamePublisher(subject, subject.ToLowerInvariant()),
            "the same signer subject should match regardless of case");
        Assert(!ReleaseSecurity.IsSamePublisher(subject, "CN=Someone Else, O=Someone Else"),
            "a different signer must be rejected");
        Assert(!ReleaseSecurity.IsSamePublisher(subject, subject + padding),
            "the signer subject is compared strictly and is never padded");
        Assert(!ReleaseSecurity.IsSamePublisher(null, null) &&
               !ReleaseSecurity.IsSamePublisher(string.Empty, string.Empty),
            "missing signer subjects must be rejected");

        const string bootstrap = "CN=SignPath Foundation, O=SignPath Foundation, L=Lewes, S=Delaware, C=US";
        Assert(ReleaseSecurity.IsBootstrapPublisher(bootstrap) &&
               ReleaseSecurity.IsBootstrapPublisher(bootstrap.ToLowerInvariant()),
            "the SignPath Foundation subject must be the accepted first publisher");
        Assert(!ReleaseSecurity.IsBootstrapPublisher(null) &&
               !ReleaseSecurity.IsBootstrapPublisher(string.Empty) &&
               !ReleaseSecurity.IsBootstrapPublisher("CN=SignPath Foundation") &&
               !ReleaseSecurity.IsBootstrapPublisher("CN=Someone Else, O=SignPath Foundation") &&
               !ReleaseSecurity.IsBootstrapPublisher("CN=SignPath Foundation, O=Someone Else") &&
               !ReleaseSecurity.IsBootstrapPublisher("CN=SignPath Foundation Fake, O=SignPath Foundation") &&
               !ReleaseSecurity.IsBootstrapPublisher(subject),
            "anything but the exact SignPath Foundation name and organization must be rejected");
        Assert(ReleaseSecurity.IsAcceptedPublisher(bootstrap, string.Empty) &&
               ReleaseSecurity.IsAcceptedPublisher(bootstrap, null) &&
               !ReleaseSecurity.IsAcceptedPublisher(subject, string.Empty) &&
               ReleaseSecurity.IsAcceptedPublisher(subject, subject) &&
               !ReleaseSecurity.IsAcceptedPublisher(bootstrap, subject),
            "an unsigned install accepts only SignPath Foundation; a signed install accepts only its own publisher");

        string innoFixture = FindRepositoryFile(Path.Combine(
            "third_party", "prerequisites", "USBip-0.9.8.0-x64.exe"));
        Assert(innoFixture != null, "the tracked Inno Setup prerequisite used as a padding fixture is missing");
        FileVersionInfo genuine = FileVersionInfo.GetVersionInfo(innoFixture);
        Assert(genuine.ProductName.Length > "USBip".Length &&
               ReleaseSecurity.NormalizeVersionField(genuine.ProductName) == "USBip",
            "a genuine Inno Setup binary pads its product name with trailing spaces");
        Assert(genuine.ProductVersion.Length > "0.9.8.0".Length &&
               ReleaseSecurity.MatchesReleaseVersion(genuine.ProductVersion, "v0.9.8"),
            "a genuine Inno Setup binary pads its product version with trailing spaces");

        string paddedSetup = CompileVersionedExecutable(
            root, "padded-setup.exe", "ApexSenseBridge" + padding, "1.0.2" + padding);
        Assert(FileVersionInfo.GetVersionInfo(paddedSetup).ProductName.Length > "ApexSenseBridge".Length,
            "the padded test executable should really carry a padded product name");
        string error;
        Assert(!ReleaseSecurity.VerifyInstaller(paddedSetup, "v1.0.2", paddedSetup, out error),
            "an unsigned setup must still be rejected");
        Assert(error != null && error.IndexOf("Authenticode", StringComparison.Ordinal) >= 0,
            "padded identity fields should pass the product and version checks and fail only the "
            + "signature check, not: " + error);
        Assert(!ReleaseSecurity.VerifyInstaller(paddedSetup, null, paddedSetup, out error) &&
               error != null && error.IndexOf("Authenticode", StringComparison.Ordinal) >= 0,
            "a padded product name should pass when no release version is required");
        Assert(!ReleaseSecurity.VerifyInstaller(paddedSetup, "v1.0.3", paddedSetup, out error) &&
               error != null && error.IndexOf("does not match", StringComparison.Ordinal) >= 0,
            "a padded setup for another release must still be rejected by version");

        string otherProduct = CompileVersionedExecutable(
            root, "other-product.exe", "SomethingElse" + padding, "1.0.2" + padding);
        Assert(!ReleaseSecurity.VerifyInstaller(otherProduct, "v1.0.2", otherProduct, out error) &&
               error != null && error.IndexOf("not an ApexSenseBridge setup", StringComparison.Ordinal) >= 0,
            "a padded setup for another product must still be rejected by product name");

        string otherVersion = CompileVersionedExecutable(
            root, "other-version.exe", "ApexSenseBridge" + padding, "9.9.9" + padding);
        Assert(!ReleaseSecurity.VerifyInstaller(otherVersion, "v1.0.2", otherVersion, out error) &&
               error != null && error.IndexOf("does not match", StringComparison.Ordinal) >= 0,
            "a padded setup with a different version must still be rejected by version");

        Assert(!ReleaseSecurity.VerifyInstaller(
                Path.Combine(root, "not-downloaded.exe"), "v1.0.2", paddedSetup, out error) &&
               error != null && error.IndexOf("missing", StringComparison.Ordinal) >= 0,
            "a missing setup file must be rejected");
    }

    private static string CompileVersionedExecutable(
        string root, string fileName, string productName, string productVersion)
    {
        string path = Path.Combine(root, fileName);
        string source =
            "using System.Reflection;\n" +
            "[assembly: AssemblyProduct(\"" + productName + "\")]\n" +
            "[assembly: AssemblyInformationalVersion(\"" + productVersion + "\")]\n" +
            "[assembly: AssemblyFileVersion(\"1.0.2.0\")]\n" +
            "internal static class Program { private static int Main() { return 0; } }\n";
        var parameters = new System.CodeDom.Compiler.CompilerParameters
        {
            GenerateExecutable = true,
            GenerateInMemory = false,
            OutputAssembly = path
        };
        using (var provider = new Microsoft.CSharp.CSharpCodeProvider())
        {
            System.CodeDom.Compiler.CompilerResults results = provider.CompileAssemblyFromSource(parameters, source);
            if (results.Errors.HasErrors)
            {
                throw new InvalidOperationException(
                    "The versioned test executable did not compile: " + results.Errors[0].ErrorText);
            }
        }
        return path;
    }

    private static string FindRepositoryFile(string relativePath)
    {
        string directory = AppDomain.CurrentDomain.BaseDirectory;
        for (int depth = 0; depth < 8 && !string.IsNullOrEmpty(directory); depth++)
        {
            string candidate = Path.Combine(directory, relativePath);
            if (File.Exists(candidate))
            {
                return candidate;
            }

            directory = Path.GetDirectoryName(directory.TrimEnd('\\'));
        }

        return null;
    }

    private static void TestStartupDiagnosticsReport()
    {
        var previousCulture = Thread.CurrentThread.CurrentCulture;
        var previousUiCulture = Thread.CurrentThread.CurrentUICulture;
        try
        {
            var chinese = new CultureInfo("zh-CN");
            Thread.CurrentThread.CurrentCulture = chinese;
            Thread.CurrentThread.CurrentUICulture = chinese;
            StartupDiagnostics.ResetForTests();

            StartupDiagnostics.Mark("settings");
            var ballast = new byte[8 * 1024 * 1024];
            ballast[ballast.Length - 1] = 1;
            StartupDiagnostics.Mark("tray-icon");
            for (int i = 0; i < StartupDiagnostics.MaxPhases * 3; i++) StartupDiagnostics.Mark("flood");
            StartupDiagnostics.Complete();
            StartupDiagnostics.Mark("late");

            var report = StartupDiagnostics.Describe();
            GC.KeepAlive(ballast);
            var phaseLines = report.Split('\n').Count(line => line.TrimStart().StartsWith("phase ", StringComparison.Ordinal));

            Assert(report.Contains("culture zh-CN/zh-CN"), "startup diagnostics must name the active culture: " + report);
            Assert(report.Contains("phase settings at ") && report.Contains("phase tray-icon at "),
                "startup diagnostics must list recorded phases in order: " + report);
            Assert(phaseLines == StartupDiagnostics.MaxPhases, "startup phase history must stay bounded, got " + phaseLines);
            Assert(!report.Contains("phase late"), "phases after startup completion must not be recorded");
            Assert(report.Contains("during flood") && report.Contains("managed ") && report.Contains("private "),
                "the failure line must report the current phase and both memory figures: " + report);
            Assert(report.Contains("address space: ") && report.Contains("largest free block "),
                "the failure report must describe the address space: " + report);
            Assert(report.All(c => c < 128), "startup diagnostics must be culture invariant ASCII: " + report);
            Assert(report.Length < 8192, "startup diagnostics must stay small, got " + report.Length);
            Assert(StartupDiagnostics.PrivateBytes() >= ballast.Length, "private bytes must include the managed heap");
            Assert(StartupDiagnostics.LargestFreeBlock() > 0, "the address-space walk must find a free block");
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = previousCulture;
            Thread.CurrentThread.CurrentUICulture = previousUiCulture;
            StartupDiagnostics.ResetForTests();
        }
    }

    private static void Assert(bool condition, string message)
    {
        assertions++;
        if (!condition) throw new InvalidOperationException(message);
    }
}
