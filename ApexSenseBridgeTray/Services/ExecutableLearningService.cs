using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Services
{
    public sealed class ExecutableLearningService : IDisposable
    {
        private const int SchemaVersion = 1;
        private const int MaximumBindings = 2048;

        private readonly string storagePath;
        private readonly string logDirectory;
        private readonly TimeSpan stabilityDelay;
        private readonly object mutationLock = new object();
        private readonly object pendingLock = new object();
        private readonly object persistenceLock = new object();

        private Dictionary<string, LearnedExecutableBinding> snapshot =
            new Dictionary<string, LearnedExecutableBinding>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<uint, PendingObservation> pendingObservations =
            new Dictionary<uint, PendingObservation>();
        private int initializationStarted;
        private int persistenceDirty;
        private volatile bool isDisposed;

        public event Action BindingsChanged;
        public event Action StateChanged;

        public ExecutableLearningService()
            : this(DefaultStoragePath, TimeSpan.FromSeconds(30))
        {
        }

        public ExecutableLearningService(string storagePath, TimeSpan stabilityDelay)
        {
            if (string.IsNullOrWhiteSpace(storagePath))
            {
                throw new ArgumentException("A storage path is required.", "storagePath");
            }
            if (stabilityDelay < TimeSpan.Zero)
            {
                throw new ArgumentOutOfRangeException("stabilityDelay");
            }

            this.storagePath = storagePath;
            var storageDirectory = Path.GetDirectoryName(Path.GetFullPath(storagePath));
            logDirectory = Path.Combine(storageDirectory ?? string.Empty, "logs");
            this.stabilityDelay = stabilityDelay;
        }

        private static string DefaultStoragePath
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "ApexSenseBridge", "learned_executables.json");
            }
        }

        public int Count
        {
            get { return Volatile.Read(ref snapshot).Count; }
        }

        public int PendingCount
        {
            get
            {
                lock (pendingLock)
                {
                    return pendingObservations.Count;
                }
            }
        }

        public void InitializeAsync()
        {
            if (Interlocked.Exchange(ref initializationStarted, 1) != 0) return;

            ThreadPool.QueueUserWorkItem(_ =>
            {
                try
                {
                    LoadFromDisk();
                }
                catch (Exception ex)
                {
                    Log("learning load failed: " + ex.Message);
                }
            });
        }

        public bool TryResolve(
            string executablePath,
            CloudGameListService gameListService,
            out SupportedGame game)
        {
            game = null;
            if (string.IsNullOrWhiteSpace(executablePath) || gameListService == null) return false;

            var normalizedPath = NormalizeStoredPath(executablePath);
            if (string.IsNullOrWhiteSpace(normalizedPath)) return false;

            LearnedExecutableBinding binding;
            var current = Volatile.Read(ref snapshot);
            if (!current.TryGetValue(normalizedPath, out binding) || binding == null) return false;

            if (binding.SteamAppId > 0 &&
                gameListService.TryFindBySteamAppId(binding.SteamAppId, out game))
            {
                return true;
            }

            return gameListService.TryFindExactGame(binding.GameNormalized, out game);
        }

        public IReadOnlyList<LearnedExecutableBinding> GetBindings()
        {
            var current = Volatile.Read(ref snapshot);
            return current.Values
                .Select(x => x.Clone())
                .OrderBy(x => x.GameTitle, StringComparer.OrdinalIgnoreCase)
                .ThenBy(x => x.Executable, StringComparer.OrdinalIgnoreCase)
                .ThenBy(x => x.Path, StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }

        public void BeginObservation(
            uint processId,
            string executablePath,
            SupportedGame game,
            string detectionMethod,
            Func<uint, string, bool> isStillActive)
        {
            if (isDisposed || processId == 0 || game == null || isStillActive == null) return;
            if (string.IsNullOrWhiteSpace(executablePath) || !Path.IsPathRooted(executablePath))
            {
                Log(string.Format(
                    CultureInfo.InvariantCulture,
                    "learning skipped: PID {0} has no absolute executable path ('{1}')",
                    processId,
                    executablePath ?? string.Empty));
                return;
            }

            var normalizedPath = NormalizeStoredPath(executablePath);
            if (string.IsNullOrWhiteSpace(normalizedPath))
            {
                Log(string.Format(
                    CultureInfo.InvariantCulture,
                    "learning skipped: PID {0} executable path could not be normalized",
                    processId));
                return;
            }

            var observation = new PendingObservation
            {
                ProcessId = processId,
                ExecutablePath = normalizedPath,
                GameTitle = game.Title ?? string.Empty,
                GameNormalized = game.Normalized ?? string.Empty,
                SteamAppId = game.SteamAppIdVerified ? game.SteamAppId : 0,
                DetectionMethod = detectionMethod ?? string.Empty,
                IsStillActive = isStillActive
            };

            lock (pendingLock)
            {
                if (isDisposed) return;

                PendingObservation existing;
                if (pendingObservations.TryGetValue(processId, out existing))
                {
                    if (!existing.ValidationStarted &&
                        string.Equals(existing.ExecutablePath, normalizedPath,
                            StringComparison.OrdinalIgnoreCase) &&
                        string.Equals(existing.GameNormalized, observation.GameNormalized,
                            StringComparison.OrdinalIgnoreCase))
                    {
                        // Repeated WMI/poll/foreground sightings must not restart the
                        // stability window indefinitely.
                        existing.IsStillActive = isStillActive;
                        return;
                    }

                    CancelPendingNoLock(existing);
                    pendingObservations.Remove(processId);
                }

                observation.ValidationTimer = new Timer(
                    ValidatePendingObservation,
                    observation,
                    Timeout.InfiniteTimeSpan,
                    Timeout.InfiniteTimeSpan);
                pendingObservations[processId] = observation;
                observation.ValidationTimer.Change(stabilityDelay, Timeout.InfiniteTimeSpan);
            }

            Log(string.Format(
                CultureInfo.InvariantCulture,
                "learning pending: PID {0}, game '{1}', executable '{2}', method '{3}'",
                processId,
                observation.GameTitle,
                observation.ExecutablePath,
                observation.DetectionMethod));
            RaiseStateChanged();
        }

        public void CancelObservation(uint processId, string reason = null)
        {
            bool changed = false;
            int cancelledCount = 0;
            lock (pendingLock)
            {
                if (processId == 0)
                {
                    cancelledCount = pendingObservations.Count;
                    changed = cancelledCount > 0;
                    CancelAllPendingNoLock();
                }
                else
                {
                    PendingObservation observation;
                    if (pendingObservations.TryGetValue(processId, out observation))
                    {
                        pendingObservations.Remove(processId);
                        CancelPendingNoLock(observation);
                        cancelledCount = 1;
                        changed = true;
                    }
                }
            }

            if (changed)
            {
                Log(string.Format(
                    CultureInfo.InvariantCulture,
                    "learning cancelled: {0} pending observation(s), PID {1}, reason '{2}'",
                    cancelledCount,
                    processId == 0 ? "all" : processId.ToString(CultureInfo.InvariantCulture),
                    string.IsNullOrWhiteSpace(reason) ? "not specified" : reason));
                RaiseStateChanged();
            }
        }

        public int DeleteBindings(IEnumerable<string> executablePaths)
        {
            if (executablePaths == null) return 0;

            var requested = new HashSet<string>(
                executablePaths.Where(x => !string.IsNullOrWhiteSpace(x)),
                StringComparer.OrdinalIgnoreCase);
            if (requested.Count == 0) return 0;

            bool pendingChanged = false;
            lock (pendingLock)
            {
                var cancelledProcessIds = pendingObservations
                    .Where(x => requested.Contains(x.Value.ExecutablePath))
                    .Select(x => x.Key)
                    .ToArray();
                foreach (var processId in cancelledProcessIds)
                {
                    var observation = pendingObservations[processId];
                    pendingObservations.Remove(processId);
                    CancelPendingNoLock(observation);
                    pendingChanged = true;
                }
            }

            int deleted = 0;
            lock (mutationLock)
            {
                if (isDisposed) return 0;
                var current = Volatile.Read(ref snapshot);
                var replacement = new Dictionary<string, LearnedExecutableBinding>(
                    current, StringComparer.OrdinalIgnoreCase);

                foreach (var path in requested)
                {
                    if (replacement.Remove(path)) deleted++;
                }

                if (deleted > 0)
                {
                    Volatile.Write(ref snapshot, replacement);
                }
            }

            if (deleted > 0)
            {
                SchedulePersistence();
                RaiseBindingsChanged();
            }
            else if (pendingChanged)
            {
                RaiseStateChanged();
            }
            return deleted;
        }

        public bool ExportBindings(
            IEnumerable<LearnedExecutableBinding> bindings,
            string outputPath,
            out string error)
        {
            error = null;
            if (bindings == null || string.IsNullOrWhiteSpace(outputPath))
            {
                error = "No learned executable was selected for export.";
                return false;
            }

            try
            {
                if (string.Equals(
                    Path.GetFullPath(outputPath),
                    Path.GetFullPath(storagePath),
                    StringComparison.OrdinalIgnoreCase))
                {
                    error = "The local learning cache cannot be used as an export destination.";
                    return false;
                }

                var selected = bindings
                    .Where(x => x != null && !string.IsNullOrWhiteSpace(x.Executable))
                    .Select(x => x.Clone())
                    .ToArray();
                if (selected.Length == 0)
                {
                    error = "No learned executable was selected for export.";
                    return false;
                }

                var json = BuildExportJson(selected);
                File.WriteAllText(outputPath, json, new UTF8Encoding(false));
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private void ValidatePendingObservation(object state)
        {
            var observation = state as PendingObservation;
            if (observation == null || isDisposed) return;

            lock (pendingLock)
            {
                PendingObservation current;
                if (!pendingObservations.TryGetValue(observation.ProcessId, out current) ||
                    !ReferenceEquals(current, observation) || observation.IsCancelled)
                {
                    return;
                }

                observation.ValidationStarted = true;
                if (observation.ValidationTimer != null)
                {
                    observation.ValidationTimer.Dispose();
                    observation.ValidationTimer = null;
                }
            }

            bool stable = false;
            try
            {
                stable = observation.IsStillActive(
                    observation.ProcessId, observation.ExecutablePath);
            }
            catch (Exception ex)
            {
                Log("learning validation failed: " + ex.Message);
            }

            lock (pendingLock)
            {
                PendingObservation current;
                if (!pendingObservations.TryGetValue(observation.ProcessId, out current) ||
                    !ReferenceEquals(current, observation) || observation.IsCancelled)
                {
                    return;
                }

                pendingObservations.Remove(observation.ProcessId);
            }

            if (!stable || isDisposed)
            {
                Log(string.Format(
                    CultureInfo.InvariantCulture,
                    "learning rejected: PID {0}, executable '{1}' was no longer the same running process after {2:F1} seconds",
                    observation.ProcessId,
                    observation.ExecutablePath,
                    stabilityDelay.TotalSeconds));
                RaiseStateChanged();
                return;
            }

            UpsertValidatedBinding(observation);
        }

        private void UpsertValidatedBinding(PendingObservation observation)
        {
            var now = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
            LearnedExecutableBinding validated;

            lock (mutationLock)
            {
                if (isDisposed) return;
                var current = Volatile.Read(ref snapshot);
                var replacement = new Dictionary<string, LearnedExecutableBinding>(
                    current, StringComparer.OrdinalIgnoreCase);

                LearnedExecutableBinding existing;
                if (replacement.TryGetValue(observation.ExecutablePath, out existing) && existing != null)
                {
                    validated = existing.Clone();
                    validated.GameTitle = observation.GameTitle;
                    validated.GameNormalized = observation.GameNormalized;
                    validated.SteamAppId = observation.SteamAppId;
                    if (!observation.DetectionMethod.StartsWith(
                        "learned exact path", StringComparison.OrdinalIgnoreCase))
                    {
                        validated.DetectionMethod = observation.DetectionMethod;
                    }
                    validated.LastSeenUtc = now;
                    validated.SuccessfulSessions = existing.SuccessfulSessions == int.MaxValue
                        ? int.MaxValue
                        : Math.Max(1, existing.SuccessfulSessions + 1);
                }
                else
                {
                    validated = new LearnedExecutableBinding
                    {
                        Path = observation.ExecutablePath,
                        Executable = Path.GetFileName(observation.ExecutablePath),
                        GameTitle = observation.GameTitle,
                        GameNormalized = observation.GameNormalized,
                        SteamAppId = observation.SteamAppId,
                        DetectionMethod = observation.DetectionMethod,
                        FirstSeenUtc = now,
                        LastSeenUtc = now,
                        SuccessfulSessions = 1
                    };
                }

                replacement[observation.ExecutablePath] = validated;
                TrimToCapacity(replacement);
                Volatile.Write(ref snapshot, replacement);
            }

            Log(string.Format(
                CultureInfo.InvariantCulture,
                "learning validated: PID {0}, game '{1}', executable '{2}'",
                observation.ProcessId,
                observation.GameTitle,
                observation.ExecutablePath));
            SchedulePersistence();
            RaiseBindingsChanged();
        }

        private void LoadFromDisk()
        {
            if (isDisposed) return;
            if (!File.Exists(storagePath)) return;

            Dictionary<string, LearnedExecutableBinding> loaded;
            try
            {
                var json = File.ReadAllText(storagePath, Encoding.UTF8);
                loaded = ParseLocalJson(json);
            }
            catch (Exception ex)
            {
                Log("learning cache ignored: " + ex.Message);
                return;
            }

            lock (mutationLock)
            {
                if (isDisposed) return;
                var live = Volatile.Read(ref snapshot);
                foreach (var item in live)
                {
                    loaded[item.Key] = item.Value;
                }
                TrimToCapacity(loaded);
                Volatile.Write(ref snapshot, loaded);
            }

            RaiseBindingsChanged();
        }

        private static Dictionary<string, LearnedExecutableBinding> ParseLocalJson(string json)
        {
            var result = new Dictionary<string, LearnedExecutableBinding>(StringComparer.OrdinalIgnoreCase);
            if (string.IsNullOrWhiteSpace(json)) return result;

            var serializer = new JavaScriptSerializer { MaxJsonLength = int.MaxValue };
            var root = serializer.Deserialize<Dictionary<string, object>>(json);
            if (root == null || ReadInt(root, "version") != SchemaVersion)
            {
                throw new InvalidDataException("Unsupported learned executable cache version.");
            }

            object rawBindings;
            var enumerable = root.TryGetValue("bindings", out rawBindings)
                ? rawBindings as IEnumerable
                : null;
            if (enumerable == null) return result;

            foreach (var raw in enumerable)
            {
                var item = raw as Dictionary<string, object>;
                if (item == null) continue;

                var path = NormalizeStoredPath(ReadString(item, "path"));
                var normalized = CloudGameListService.Normalize(ReadString(item, "gameNormalized"));
                var steamAppId = ReadInt(item, "steamAppId");
                var firstSeen = NormalizeTimestamp(ReadString(item, "firstSeenUtc"));
                var lastSeen = NormalizeTimestamp(ReadString(item, "lastSeenUtc"));

                if (string.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path) ||
                    (string.IsNullOrWhiteSpace(normalized) && steamAppId <= 0) ||
                    string.IsNullOrWhiteSpace(firstSeen) || string.IsNullOrWhiteSpace(lastSeen))
                {
                    continue;
                }

                result[path] = new LearnedExecutableBinding
                {
                    Path = path,
                    Executable = Path.GetFileName(path),
                    GameTitle = ReadString(item, "gameTitle"),
                    GameNormalized = normalized,
                    SteamAppId = steamAppId,
                    DetectionMethod = ReadString(item, "detectionMethod"),
                    FirstSeenUtc = firstSeen,
                    LastSeenUtc = lastSeen,
                    SuccessfulSessions = Math.Max(1, ReadInt(item, "successfulSessions"))
                };

            }
            return result;
        }

        private void SchedulePersistence()
        {
            Interlocked.Increment(ref persistenceDirty);
            ThreadPool.QueueUserWorkItem(_ => PersistCurrentSnapshot());
        }

        private void PersistCurrentSnapshot()
        {
            lock (persistenceLock)
            {
                var dirtyGeneration = Volatile.Read(ref persistenceDirty);
                var tempPath = storagePath + ".tmp";
                try
                {
                    var current = Volatile.Read(ref snapshot);
                    var json = BuildLocalJson(current.Values);
                    var directory = Path.GetDirectoryName(storagePath);
                    if (!string.IsNullOrWhiteSpace(directory) && !Directory.Exists(directory))
                    {
                        Directory.CreateDirectory(directory);
                    }

                    File.WriteAllText(tempPath, json, new UTF8Encoding(false));
                    if (File.Exists(storagePath))
                    {
                        File.Replace(tempPath, storagePath, null, true);
                    }
                    else
                    {
                        File.Move(tempPath, storagePath);
                    }
                    Interlocked.CompareExchange(ref persistenceDirty, 0, dirtyGeneration);
                }
                catch (Exception ex)
                {
                    Log("learning persistence failed: " + ex.Message);
                    try
                    {
                        if (File.Exists(tempPath)) File.Delete(tempPath);
                    }
                    catch
                    {
                    }
                }
            }
        }

        private static string BuildLocalJson(IEnumerable<LearnedExecutableBinding> bindings)
        {
            var serializer = new JavaScriptSerializer();
            var ordered = bindings
                .Where(x => x != null)
                .OrderBy(x => x.Path, StringComparer.OrdinalIgnoreCase)
                .ToArray();
            var sb = new StringBuilder();
            sb.Append("{\n  \"version\": 1,\n  \"bindings\": [");

            for (int i = 0; i < ordered.Length; i++)
            {
                var item = ordered[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append("    {\n");
                AppendJsonProperty(sb, serializer, "path", item.Path, true);
                AppendJsonProperty(sb, serializer, "executable", item.Executable, true);
                AppendJsonProperty(sb, serializer, "gameTitle", item.GameTitle, true);
                AppendJsonProperty(sb, serializer, "gameNormalized", item.GameNormalized, true);
                AppendJsonNumber(sb, "steamAppId", item.SteamAppId, true);
                AppendJsonProperty(sb, serializer, "detectionMethod", item.DetectionMethod, true);
                AppendJsonProperty(sb, serializer, "firstSeenUtc", item.FirstSeenUtc, true);
                AppendJsonProperty(sb, serializer, "lastSeenUtc", item.LastSeenUtc, true);
                AppendJsonNumber(sb, "successfulSessions", item.SuccessfulSessions, false);
                sb.Append("    }");
            }

            if (ordered.Length > 0) sb.Append('\n');
            sb.Append("  ]\n}\n");
            return sb.ToString();
        }

        private static string BuildExportJson(IEnumerable<LearnedExecutableBinding> bindings)
        {
            var serializer = new JavaScriptSerializer();
            var groups = bindings
                .GroupBy(x => new ExportKey(x.SteamAppId, x.GameNormalized ?? string.Empty))
                .Select(g => new
                {
                    Key = g.Key,
                    Executables = g.Select(x => Path.GetFileName(x.Executable))
                        .Where(x => !string.IsNullOrWhiteSpace(x))
                        .Distinct(StringComparer.OrdinalIgnoreCase)
                        .OrderBy(x => x, StringComparer.OrdinalIgnoreCase)
                        .ToArray()
                })
                .Where(x => x.Executables.Length > 0)
                .OrderBy(x => x.Key.SteamAppId)
                .ThenBy(x => x.Key.Normalized, StringComparer.OrdinalIgnoreCase)
                .ToArray();

            var sb = new StringBuilder();
            sb.Append("{\n  \"version\": 1,\n  \"games\": [");
            for (int i = 0; i < groups.Length; i++)
            {
                var group = groups[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append("    {\n");
                AppendJsonNumber(sb, "steamAppId", group.Key.SteamAppId, true);
                AppendJsonProperty(sb, serializer, "normalized", group.Key.Normalized, true);
                sb.Append("      \"executables\": [");
                for (int j = 0; j < group.Executables.Length; j++)
                {
                    if (j > 0) sb.Append(", ");
                    sb.Append(serializer.Serialize(group.Executables[j]));
                }
                sb.Append("]\n    }");
            }
            if (groups.Length > 0) sb.Append('\n');
            sb.Append("  ]\n}\n");
            return sb.ToString();
        }

        private static void AppendJsonProperty(
            StringBuilder sb,
            JavaScriptSerializer serializer,
            string name,
            string value,
            bool comma)
        {
            sb.Append("      \"").Append(name).Append("\": ")
                .Append(serializer.Serialize(value ?? string.Empty));
            sb.Append(comma ? ",\n" : "\n");
        }

        private static void AppendJsonNumber(StringBuilder sb, string name, int value, bool comma)
        {
            sb.Append("      \"").Append(name).Append("\": ")
                .Append(value.ToString(CultureInfo.InvariantCulture));
            sb.Append(comma ? ",\n" : "\n");
        }

        private static string ReadString(Dictionary<string, object> item, string key)
        {
            object value;
            return item.TryGetValue(key, out value) && value != null
                ? value.ToString()
                : string.Empty;
        }

        private static int ReadInt(Dictionary<string, object> item, string key)
        {
            int value;
            return int.TryParse(ReadString(item, key), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out value) ? value : 0;
        }

        private static string NormalizeStoredPath(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) return string.Empty;
            try
            {
                return Path.GetFullPath(path.Trim());
            }
            catch
            {
                return string.Empty;
            }
        }

        private static string NormalizeTimestamp(string value)
        {
            DateTime parsed;
            if (!DateTime.TryParse(value, CultureInfo.InvariantCulture,
                DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out parsed))
            {
                return string.Empty;
            }
            return parsed.ToUniversalTime().ToString("o", CultureInfo.InvariantCulture);
        }

        private static void TrimToCapacity(Dictionary<string, LearnedExecutableBinding> bindings)
        {
            if (bindings.Count <= MaximumBindings) return;

            var removeCount = bindings.Count - MaximumBindings;
            var oldest = bindings.Values
                .OrderBy(x => x.LastSeenUtc, StringComparer.Ordinal)
                .ThenBy(x => x.Path, StringComparer.OrdinalIgnoreCase)
                .Take(removeCount)
                .Select(x => x.Path)
                .ToArray();
            foreach (var path in oldest) bindings.Remove(path);
        }

        private static void CancelPendingNoLock(PendingObservation observation)
        {
            if (observation == null) return;
            observation.IsCancelled = true;
            if (observation.ValidationTimer != null)
            {
                observation.ValidationTimer.Dispose();
                observation.ValidationTimer = null;
            }
        }

        private void CancelAllPendingNoLock()
        {
            foreach (var observation in pendingObservations.Values)
            {
                CancelPendingNoLock(observation);
            }
            pendingObservations.Clear();
        }

        private void RaiseBindingsChanged()
        {
            var handler = BindingsChanged;
            if (handler != null)
            {
                try { handler(); } catch { }
            }
            RaiseStateChanged();
        }

        private void RaiseStateChanged()
        {
            var handler = StateChanged;
            if (handler == null) return;
            try { handler(); } catch { }
        }

        private void Log(string message)
        {
            ThreadPool.QueueUserWorkItem(_ => WriteLog(message));
        }

        private void WriteLog(string message)
        {
            AppLog.WriteLine(logDirectory, "tray_detection.log", message);
        }

        public void Dispose()
        {
            lock (pendingLock)
            {
                if (isDisposed) return;
                isDisposed = true;
                CancelAllPendingNoLock();
            }

            // Synchronize with a validation that may have passed its final
            // cancellation check immediately before disposal.
            lock (mutationLock)
            {
            }

            // A validated association must survive an immediate tray shutdown;
            // queued ThreadPool persistence is only an optimization.
            if (Volatile.Read(ref persistenceDirty) != 0)
            {
                PersistCurrentSnapshot();
            }
        }

        private sealed class PendingObservation
        {
            public uint ProcessId;
            public string ExecutablePath;
            public string GameTitle;
            public string GameNormalized;
            public int SteamAppId;
            public string DetectionMethod;
            public Func<uint, string, bool> IsStillActive;
            public Timer ValidationTimer;
            public bool ValidationStarted;
            public bool IsCancelled;
        }

        private sealed class ExportKey : IEquatable<ExportKey>
        {
            public readonly int SteamAppId;
            public readonly string Normalized;

            public ExportKey(int steamAppId, string normalized)
            {
                SteamAppId = steamAppId;
                Normalized = normalized ?? string.Empty;
            }

            public bool Equals(ExportKey other)
            {
                return other != null && SteamAppId == other.SteamAppId &&
                    string.Equals(Normalized, other.Normalized, StringComparison.OrdinalIgnoreCase);
            }

            public override bool Equals(object obj)
            {
                return Equals(obj as ExportKey);
            }

            public override int GetHashCode()
            {
                unchecked
                {
                    return (SteamAppId * 397) ^ StringComparer.OrdinalIgnoreCase.GetHashCode(Normalized);
                }
            }
        }
    }
}
