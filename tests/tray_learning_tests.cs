using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
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

    public static int Main()
    {
        var testRoot = Path.Combine(
            Path.GetTempPath(),
            "ApexSenseBridge-LearningTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(testRoot);

        try
        {
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
            TestActivationPolicyStillAppliesAfterLearning();
            TestPerGameApexProfileSettings();
            TestGameProcessSessionPidHandoff();
            TestPlatformClientsNeverCountAsGameProcesses();
            TestPidTrackingFastPathPerformance();
            TestMissPerformance(testRoot);
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

        settings.SetGameExcluded(game.Normalized, true);
        Assert(!GameActivationPolicy.ShouldActivate(
                game, settings, "AlphaGame", "Alpha", "AlphaGame.exe"),
            "A learned association bypassed the game exclusion policy.");

        settings.SetGameExcluded(game.Normalized, false);
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

    private static void Assert(bool condition, string message)
    {
        assertions++;
        if (!condition) throw new InvalidOperationException(message);
    }
}
