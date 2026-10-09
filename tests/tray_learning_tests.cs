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
            TestLatencyStatisticsAndTelemetry();
            TestGyroStreamParsing();
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
            TestPerGameApexProfileSettings();
            TestManualBridgeModeIsNotPersisted();
            TestSharedBridgeArguments();
            TestControllerCalibrations();
            TestControllerCalibrationDetection();
            TestGameExecutableSettings();
            TestSessionStatusDiscovery();
            TestRecoveryPolicy();
            TestInterruptedSessionRecovery();
            TestExternalRecoveryOwnership();
            TestRecoveryCancelledDuringStartup();
            TestRecoveryGameLifetimeTracking();
            TestEngineMainEntry(testRoot);
            TestGameProcessSessionPidHandoff();
            TestPlatformClientsNeverCountAsGameProcesses();
            TestPidTrackingFastPathPerformance();
            TestMissPerformance(testRoot);
            TestSteamAlreadyRunningDetection();
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

        game.AdaptiveTriggersManualFix = false;
        Assert(GameActivationPolicy.ShouldActivate(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A native feature was incorrectly blocked by another feature's manual-fix state.");
        Assert(!GameActivationPolicy.IsBlockedByManualFix(
                game, settings, "DyingLightGame", "Dying Light", "DyingLightGame.exe"),
            "A game with a ready selected feature was incorrectly reported as fully blocked.");
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
        var arguments = settings.BuildControllerCalibrationArguments();
        Assert(arguments.Contains("--controller-calibration apex4:0:60:24:0:0:400:225") &&
            arguments.Contains("--controller-calibration apex5:35:200:24:0:1:100:100") &&
            arguments.Contains("--controller-calibration apex6:80:90:0:1:0:100:100"),
            "Controller profiles were not bounded/serialized to the native protocol.");
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
        var sessionArguments = (string)builder.Invoke(null, new object[] { "standard", settings, 3 });
        Assert(sessionArguments.EndsWith(arguments, StringComparison.Ordinal) &&
            !sessionArguments.Contains("--sync-lightbar") && !sessionArguments.Contains("--haptic-threshold"),
            "Session launch used legacy global tuning instead of verified-model selection.");
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
            Assert(lastPublished == "unavailable", "Persistent ambiguous enumeration kept calibration unlocked.");
            candidateState = null; candidate = "path-b";
            detector.Scan(null);
            Assert(lastPublished == "apex6", "The remaining controller was not verified again.");
            candidateState = ControllerDetectionService.Ambiguous; candidate = "path-x\n\npath-y";
            detector.Scan(null);
            Assert(lastPublished == "unavailable", "Ambiguous enumeration without the verified controller kept it unlocked.");
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
            bool fail = Array.IndexOf(args, "--fail") >= 0;
            progress.Write(0, 0x50534241u);
            progress.Write(4, 1u);
            foreach (uint stages in new uint[] { 1, 3, 7 })
            {
                progress.Write(8, stages);
                Thread.Sleep(150);
            }
            progress.Write(8, fail ? 7u : 15u);
            var bytes = Encoding.UTF8.GetBytes(fail ? "Isolation impossible (test)." : "ASB_READY|APEX test USB");
            view.Write(0, 0x53425341u);
            view.Write(4, (ushort)1);
            view.Write(6, (ushort)(fail ? 5 : 2));
            view.Write(8, fail ? 11 : 0);
            view.Write(12, (uint)bytes.Length);
            view.WriteArray(16, bytes, 0, bytes.Length);
            ready.Set();
            if (fail) return 11;
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

    private static void Assert(bool condition, string message)
    {
        assertions++;
        if (!condition) throw new InvalidOperationException(message);
    }
}
