using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class DetectedController
    {
        public string Key { get; set; }
        public int Index { get; set; }
        public string Model { get; set; }
    }

    internal sealed class ControllerDetectionService : IDisposable
    {
        internal const string Ambiguous = "ambiguous";
        internal const string Multiple = "multiple";
        internal const string SeveralControllersMarker = "Several APEX controllers are connected";
        internal const int ReverifyMilliseconds = 30000;
        internal const int FailureScans = 3;
        internal const int DisconnectScans = 2;
        private const string SeveralMarker = "\u0001several";
        private static readonly Stopwatch uptime = Stopwatch.StartNew();
        private readonly Timer timer;
        internal delegate string CandidateReader(out string candidate);
        private readonly CandidateReader candidateReader;
        private readonly Func<int?, string> modelReader;
        private readonly Func<BridgeSession.SessionInfo> activeSessionReader;
        private readonly Func<bool> ownerProbe;
        private readonly Func<long> clock;
        private readonly object gate = new object();
        private readonly object publishGate = new object();
        private readonly Dictionary<string, ControllerRecord> records = new Dictionary<string, ControllerRecord>(StringComparer.Ordinal);
        private List<DetectedController> controllers = new List<DetectedController>();
        private string preferredKey = string.Empty;
        private string lastSignature = string.Empty;
        private int scanRunning;
        private string lastStatus = string.Empty;
        private string verifiedCandidate = string.Empty;
        private string verifiedModel;
        private long verifiedAt;
        private int failures;
        private int disposed;
        private int candidateCount;

        private sealed class ControllerRecord
        {
            public string Model;
            public long VerifiedAt;
            public int Failures;
        }

        public event Action<string> StatusChanged;
        public event Action ControllersChanged;

        public int CandidateCount { get { return Volatile.Read(ref candidateCount); } }

        public ControllerDetectionService() : this(ReadCandidate, index => DetectModel(index),
            () => BridgeSession.ReadActiveSession(), EngineSessionManager.IsExternalSessionActive, true) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe, bool startTimer)
            : this(candidateReader, index => modelReader(), activeSessionReader, ownerProbe, startTimer) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<int?, string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe, bool startTimer)
            : this(candidateReader, modelReader, activeSessionReader, ownerProbe,
                () => uptime.ElapsedMilliseconds, startTimer) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe,
            Func<long> clock, bool startTimer)
            : this(candidateReader, index => modelReader(), activeSessionReader, ownerProbe, clock, startTimer) { }

        internal ControllerDetectionService(CandidateReader candidateReader, Func<int?, string> modelReader,
            Func<BridgeSession.SessionInfo> activeSessionReader, Func<bool> ownerProbe,
            Func<long> clock, bool startTimer)
        {
            this.candidateReader = candidateReader;
            this.modelReader = modelReader;
            this.activeSessionReader = activeSessionReader;
            this.ownerProbe = ownerProbe;
            this.clock = clock;
            timer = new Timer(Scan, null, startTimer ? 250 : Timeout.Infinite, 2000);
        }

        public IList<DetectedController> Controllers
        {
            get
            {
                lock (gate) return CopyControllers();
            }
        }

        public DetectedController SelectedController
        {
            get
            {
                lock (gate)
                {
                    int index = EffectiveIndex(controllers);
                    return index < 0 ? null : CopyController(controllers[index]);
                }
            }
        }

        public void Select(string key)
        {
            string model = null;
            lock (gate)
            {
                preferredKey = key ?? string.Empty;
                if (controllers.Count > 1)
                {
                    int index = EffectiveIndex(controllers);
                    if (index >= 0) model = controllers[index].Model;
                }
            }
            if (model != null) Publish(model);
            RaiseControllersChanged();
        }

        internal void Scan(object state)
        {
            if (Volatile.Read(ref disposed) != 0) return;
            if (Interlocked.Exchange(ref scanRunning, 1) != 0) return;
            try
            {
                string candidate;
                string stateStatus = candidateReader(out candidate);
                Volatile.Write(ref candidateCount, CountCandidates(candidate));
                if (stateStatus == Ambiguous)
                {
                    ScanSeveral(candidate);
                    return;
                }
                ScanSingle(stateStatus, candidate);
                MirrorSingle(stateStatus, candidate);
            }
            finally { Volatile.Write(ref scanRunning, 0); }
        }

        private void ScanSingle(string stateStatus, string candidate)
        {
            if (stateStatus != null)
            {
                Downgrade(stateStatus == "disconnected" ? stateStatus : "unavailable");
                return;
            }
            // During a bridge, use its live IPC identity, not extra HID
            // readers that can steal replies/input from the active engine.
            var active = activeSessionReader();
            if (active != null || ownerProbe())
            {
                verifiedCandidate = string.Empty; // Force verification after stop.
                failures = 0;
                Publish(ParseActiveModel(active));
                return;
            }
            if (candidate != verifiedCandidate) AdoptRecordedModel(candidate);
            // Lock editing immediately when a different HID interface
            // appears, before the slower identity query completes.
            if (candidate != verifiedCandidate) Lock("unavailable");
            else if (failures == 0 && clock() - verifiedAt < ReverifyMilliseconds)
            {
                Publish(verifiedModel);
                return;
            }
            // Periodic recheck: a receiver can remain enumerated while the controller is asleep.
            var model = modelReader(null);
            string after;
            string afterStatus = candidateReader(out after);
            if (afterStatus == null && after != candidate) Lock("unavailable");
            else if (afterStatus != null || model == "unavailable")
                Downgrade(afterStatus == "disconnected" ? afterStatus : "unavailable");
            else
            {
                verifiedCandidate = candidate;
                verifiedModel = model;
                verifiedAt = clock();
                failures = 0;
                Publish(model);
            }
        }

        private void ScanSeveral(string candidate)
        {
            var fingerprints = SplitCandidates(candidate);
            var keys = new string[fingerprints.Length];
            for (int i = 0; i < keys.Length; i++) keys[i] = KeyOf(fingerprints[i]);
            var bridged = activeSessionReader();
            bool inSession = bridged != null || ownerProbe();
            long now = clock();
            lock (gate)
            {
                if (verifiedCandidate.Length != 0 && verifiedModel != null)
                    SeedRecord(KeyOf(verifiedCandidate), verifiedModel, verifiedAt);
                verifiedCandidate = string.Empty;
                verifiedModel = null;
                failures = 0;
                PruneRecords(keys);
                controllers = BuildControllers(keys);
            }
            if (inSession)
            {
                Publish(ParseActiveModel(bridged));
                RaiseControllersChanged();
                return;
            }
            var pending = new List<int>();
            var queued = new HashSet<string>(StringComparer.Ordinal);
            lock (gate)
            {
                int first = EffectiveIndex(controllers);
                if (first >= 0 && NeedsIdentity(keys[first], now) && queued.Add(keys[first])) pending.Add(first);
                for (int i = 0; i < keys.Length; i++)
                    if (i != first && NeedsIdentity(keys[i], now) && queued.Add(keys[i])) pending.Add(i);
            }
            var results = new List<KeyValuePair<int, string>>();
            foreach (int index in pending)
            {
                if (Volatile.Read(ref disposed) != 0) return;
                results.Add(new KeyValuePair<int, string>(index, modelReader(index)));
            }
            if (results.Count != 0)
            {
                string after;
                string afterStatus = candidateReader(out after);
                if (afterStatus != Ambiguous || after != candidate) return;
            }
            if (Volatile.Read(ref disposed) != 0) return;
            string model;
            long appliedAt = clock();
            lock (gate)
            {
                foreach (var result in results) ApplyResult(keys[result.Key], result.Value, appliedAt);
                controllers = BuildControllers(keys);
                int selected = EffectiveIndex(controllers);
                model = selected < 0 ? "unavailable" : controllers[selected].Model;
            }
            lock (gate)
            {
                verifiedCandidate = SeveralMarker;
                verifiedModel = null;
                failures = 0;
            }
            Publish(model);
            RaiseControllersChanged();
        }

        private void MirrorSingle(string stateStatus, string candidate)
        {
            lock (gate)
            {
                if (stateStatus == null && !string.IsNullOrEmpty(candidate))
                {
                    controllers = new List<DetectedController>
                    {
                        new DetectedController { Key = KeyOf(candidate), Index = 0, Model = ModelOfStatus(lastStatus) }
                    };
                }
                else if (stateStatus == "disconnected") controllers = new List<DetectedController>();
            }
            RaiseControllersChanged();
        }

        private bool NeedsIdentity(string key, long now)
        {
            ControllerRecord record;
            return !records.TryGetValue(key, out record) || record.Model == null || record.Failures != 0 ||
                   now - record.VerifiedAt >= ReverifyMilliseconds;
        }

        private void SeedRecord(string key, string model, long at)
        {
            ControllerRecord record;
            if (records.TryGetValue(key, out record) && record.Model != null) return;
            records[key] = new ControllerRecord { Model = model, VerifiedAt = at };
        }

        private void AdoptRecordedModel(string candidate)
        {
            ControllerRecord record;
            lock (gate)
            {
                if (!records.TryGetValue(KeyOf(candidate), out record) || record.Failures != 0 ||
                    !IsVerifiedModel(record.Model) || clock() - record.VerifiedAt >= ReverifyMilliseconds) return;
                verifiedCandidate = candidate;
                verifiedModel = record.Model;
                verifiedAt = record.VerifiedAt;
                failures = 0;
            }
        }

        private void PruneRecords(string[] keys)
        {
            var stale = new List<string>();
            foreach (var key in records.Keys)
                if (Array.IndexOf(keys, key) < 0) stale.Add(key);
            foreach (var key in stale) records.Remove(key);
        }

        private void ApplyResult(string key, string model, long now)
        {
            ControllerRecord record;
            if (!records.TryGetValue(key, out record))
            {
                record = new ControllerRecord();
                records[key] = record;
            }
            if (model == "unavailable")
            {
                record.Failures++;
                if (!IsVerifiedModel(record.Model) || record.Failures >= FailureScans) record.Model = "unavailable";
            }
            else
            {
                record.Model = model;
                record.Failures = 0;
                record.VerifiedAt = now;
            }
        }

        private List<DetectedController> BuildControllers(string[] keys)
        {
            var list = new List<DetectedController>(keys.Length);
            for (int i = 0; i < keys.Length; i++)
            {
                ControllerRecord record;
                string model = records.TryGetValue(keys[i], out record) && record.Model != null ? record.Model : "unavailable";
                list.Add(new DetectedController { Key = keys[i], Index = i, Model = model });
            }
            return list;
        }

        private int EffectiveIndex(List<DetectedController> list)
        {
            if (list.Count == 0) return -1;
            if (preferredKey.Length != 0)
                for (int i = 0; i < list.Count; i++)
                    if (string.Equals(list[i].Key, preferredKey, StringComparison.Ordinal)) return i;
            for (int i = 0; i < list.Count; i++)
                if (IsApexModel(list[i].Model)) return i;
            for (int i = 0; i < list.Count; i++)
                if (IsVerifiedModel(list[i].Model)) return i;
            return 0;
        }

        private IList<DetectedController> CopyControllers()
        {
            var copy = new List<DetectedController>(controllers.Count);
            foreach (var controller in controllers) copy.Add(CopyController(controller));
            return copy;
        }

        private static DetectedController CopyController(DetectedController source)
        {
            return new DetectedController { Key = source.Key, Index = source.Index, Model = source.Model };
        }

        private void RaiseControllersChanged()
        {
            string signature;
            lock (gate)
            {
                int selected = EffectiveIndex(controllers);
                var text = new System.Text.StringBuilder();
                for (int i = 0; i < controllers.Count; i++)
                    text.Append(i == selected ? '*' : '-').Append(controllers[i].Key).Append('|').Append(controllers[i].Model).Append('\n');
                signature = text.ToString();
            }
            Action handler;
            lock (publishGate)
            {
                if (string.Equals(lastSignature, signature, StringComparison.Ordinal)) return;
                lastSignature = signature;
                handler = ControllersChanged;
            }
            if (Volatile.Read(ref disposed) == 0 && handler != null) handler();
        }

        internal static string[] SplitCandidates(string candidates)
        {
            return string.IsNullOrEmpty(candidates) ? new string[0]
                : candidates.Split(new[] { "\n\n" }, StringSplitOptions.None);
        }

        internal static string KeyOf(string fingerprint)
        {
            var path = Regex.Match(fingerprint ?? string.Empty, @"^Path[ \t]+(\S.*)$", RegexOptions.Multiline);
            return path.Success ? path.Groups[1].Value.Trim() : fingerprint ?? string.Empty;
        }

        internal static bool IsApexModel(string model)
        {
            return model == "apex4" || model == "apex5" || model == "apex6";
        }

        private static bool IsVerifiedModel(string model)
        {
            return IsApexModel(model) || model == "unsupported";
        }

        private static string ModelOfStatus(string status)
        {
            return IsVerifiedModel(status) ? status : "unavailable";
        }

        internal static string ModelName(string model)
        {
            return model == "apex4" ? "Apex 4" : model == "apex5" ? "Apex 5" : model == "apex6" ? "Apex 6 Pro" : null;
        }

        // One missed list/identify reply must not flap a verified controller.
        private void Downgrade(string status)
        {
            int limit = status == "disconnected" ? DisconnectScans : FailureScans;
            if (verifiedCandidate.Length != 0 && ++failures < limit) return;
            Lock(status);
        }

        private void Lock(string status)
        {
            verifiedCandidate = string.Empty;
            verifiedModel = null;
            failures = 0;
            Publish(status);
        }

        private static int CountCandidates(string candidates)
        {
            return SplitCandidates(candidates).Length;
        }

        internal static bool TryParseSeveralControllers(string message, out int count)
        {
            count = 0;
            if (string.IsNullOrEmpty(message)) return false;
            int at = message.IndexOf(SeveralControllersMarker, StringComparison.Ordinal);
            if (at < 0) return false;
            var match = Regex.Match(message.Substring(at + SeveralControllersMarker.Length), @"\A \((\d{1,3}) vendor interfaces\)");
            if (match.Success) count = int.Parse(match.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture);
            return true;
        }

        internal static string ParseActiveModel(BridgeSession.SessionInfo active)
        {
            return active != null && active.Phase == SessionPhase.Ready
                ? ParseVerifiedModel("Verified: " + active.Controller) : "unavailable";
        }

        internal static string ReadCandidate(out string candidate)
        {
            candidate = string.Empty;
            string output;
            int exitCode;
            if (!RunEngine("list", 3000, out output, out exitCode)) return "unavailable";
            if (exitCode == 2) return "disconnected";
            if (exitCode != 0) return "unavailable";
            return ParseCandidate(output, out candidate);
        }

        internal static string ParseCandidate(string output, out string candidate)
        {
            candidate = string.Empty;
            var header = Regex.Match(output ?? "", @"\AFound (\d+) candidate\(s\):");
            if (!header.Success) return "unavailable";
            // Fingerprint only stable enumeration fields; the HID product string can fail transiently.
            var fingerprints = new List<string>();
            var blocks = Regex.Split(output.Substring(header.Length), @"^\s*\[\d+\][^\n]*$", RegexOptions.Multiline);
            for (int i = 1; i < blocks.Length; i++)
            {
                var lines = new List<string>();
                foreach (Match line in Regex.Matches(blocks[i],
                    @"^[ \t]*(?:VID:PID|Usage page|Reports|Path)\b[^\r\n]*", RegexOptions.Multiline))
                    lines.Add(Regex.Replace(line.Value.Trim(), @"\s+", " "));
                if (lines.Count == 0) return "unavailable";
                fingerprints.Add(string.Join("\n", lines));
            }
            if (fingerprints.Count == 0 || fingerprints.Count.ToString() != header.Groups[1].Value) return "unavailable";
            // More than one interface is ambiguous: never calibrate the first enumerated controller by accident.
            candidate = string.Join("\n\n", fingerprints);
            return fingerprints.Count == 1 ? null : Ambiguous;
        }

        private static string DetectModel(int? index)
        {
            string output;
            int exitCode;
            string arguments = index.HasValue
                ? "identify " + index.Value.ToString(System.Globalization.CultureInfo.InvariantCulture)
                : "identify";
            if (!RunEngine(arguments, 4500, out output, out exitCode)) return "unavailable";
            if (exitCode != 0) return "unavailable";
            return ParseVerifiedModel(output);
        }

        internal static bool LocksOnboardProfile(string status)
        {
            return string.Equals(status, "apex4", StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(status, "apex6", StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(status, "unsupported", StringComparison.OrdinalIgnoreCase);
        }

        internal static string ParseVerifiedModel(string output)
        {
            output = output ?? string.Empty;
            if (Regex.IsMatch(output, @"Verified:\s+Apex 4\b", RegexOptions.IgnoreCase)) return "apex4";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 5\b", RegexOptions.IgnoreCase)) return "apex5";
            if (Regex.IsMatch(output, @"Verified:\s+Apex 6 Pro\b", RegexOptions.IgnoreCase)) return "apex6";
            return "unsupported";
        }

        private static bool RunEngine(string arguments, int timeoutMilliseconds,
                                      out string output, out int exitCode)
        {
            output = string.Empty;
            exitCode = -1;
            var engine = InstallLocator.ResolveEngine();
            if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine)) return false;
            try
            {
                var start = new ProcessStartInfo(engine, arguments)
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    WorkingDirectory = Path.GetDirectoryName(engine)
                };
                using (var process = Process.Start(start))
                {
                    if (process == null) return false;
                    // Drain both pipes asynchronously so the timeout also bounds
                    // hung identification; a blocking ReadToEnd defeated it.
                    var stdout = process.StandardOutput.ReadToEndAsync();
                    var stderr = process.StandardError.ReadToEndAsync();
                    if (!process.WaitForExit(timeoutMilliseconds))
                    {
                        try { process.Kill(); } catch { }
                        return false;
                    }
                    if (!System.Threading.Tasks.Task.WaitAll(new System.Threading.Tasks.Task[] { stdout, stderr }, 500))
                        return false;
                    output = stdout.Result;
                    exitCode = process.ExitCode;
                    return true;
                }
            }
            catch { return false; }
        }

        private void Publish(string status)
        {
            if (Volatile.Read(ref disposed) != 0) return;
            lock (publishGate)
            {
                if (string.Equals(lastStatus, status, StringComparison.Ordinal)) return;
                lastStatus = status;
            }
            var handler = StatusChanged;
            if (handler != null) handler(status);
        }

        public void Dispose() { Interlocked.Exchange(ref disposed, 1); timer.Dispose(); }
    }
}
