using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.IO;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class GameProcessSessionTracker
    {
        internal static readonly TimeSpan ExitGracePeriod = TimeSpan.FromSeconds(2);
        internal static readonly TimeSpan LaunchExitGracePeriod = TimeSpan.FromSeconds(10);
        internal static readonly TimeSpan LaunchPhase = TimeSpan.FromMinutes(3);

        private readonly Dictionary<uint, string> processes = new Dictionary<uint, string>();

        public SupportedGame ActiveGame { get; private set; }
        public string LastKnownPath { get; private set; }
        public DateTime? ExitDeadlineUtc { get; private set; }
        public DateTime StartedUtc { get; private set; }
        public string InstallFolder { get; private set; }

        public bool HasSession
        {
            get { return ActiveGame != null; }
        }

        public bool HasRunningProcesses
        {
            get { return processes.Count > 0; }
        }

        public bool IsAwaitingReplacement
        {
            get { return HasSession && processes.Count == 0 && ExitDeadlineUtc.HasValue; }
        }

        public int ProcessCount
        {
            get { return processes.Count; }
        }

        public void Start(SupportedGame game, uint processId, string executablePath)
        {
            Start(game, processId, executablePath, DateTime.UtcNow);
        }

        public void Start(SupportedGame game, uint processId, string executablePath, DateTime startedUtc)
        {
            if (game == null) throw new ArgumentNullException("game");
            if (processId == 0) throw new ArgumentOutOfRangeException("processId");

            processes.Clear();
            ActiveGame = game;
            LastKnownPath = executablePath;
            ExitDeadlineUtc = null;
            StartedUtc = startedUtc;
            InstallFolder = GetInstallFolder(executablePath);
            processes[processId] = executablePath ?? string.Empty;
        }

        public bool IsInInstallFolder(string executablePath)
        {
            return HasSession && InstallFolder != null && !string.IsNullOrWhiteSpace(executablePath) &&
                   executablePath.StartsWith(InstallFolder, StringComparison.OrdinalIgnoreCase);
        }

        public TimeSpan GetExitGracePeriod(DateTime nowUtc)
        {
            return nowUtc - StartedUtc < LaunchPhase ? LaunchExitGracePeriod : ExitGracePeriod;
        }

        public bool Remove(uint processId, DateTime nowUtc)
        {
            return Remove(processId, nowUtc, GetExitGracePeriod(nowUtc));
        }

        public bool TryAttach(
            SupportedGame game,
            uint processId,
            string executablePath)
        {
            if (!IsSameGame(game) || processId == 0) return false;

            processes[processId] = executablePath ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(executablePath))
            {
                LastKnownPath = executablePath;
            }
            ExitDeadlineUtc = null;
            return true;
        }

        public bool Remove(uint processId, DateTime nowUtc, TimeSpan exitGracePeriod)
        {
            if (!processes.Remove(processId)) return false;

            if (processes.Count == 0 && HasSession && !ExitDeadlineUtc.HasValue)
            {
                ExitDeadlineUtc = nowUtc.Add(exitGracePeriod);
            }
            return true;
        }

        public bool Contains(uint processId)
        {
            return processId != 0 && processes.ContainsKey(processId);
        }

        public bool Contains(uint processId, string executablePath)
        {
            string trackedPath;
            if (processId == 0 || !processes.TryGetValue(processId, out trackedPath)) return false;

            return string.Equals(
                trackedPath ?? string.Empty,
                executablePath ?? string.Empty,
                StringComparison.OrdinalIgnoreCase);
        }

        public bool IsSameGame(SupportedGame game)
        {
            if (ActiveGame == null || game == null) return false;

            if (ActiveGame.SteamAppIdVerified && ActiveGame.SteamAppId > 0 &&
                game.SteamAppIdVerified && game.SteamAppId > 0)
            {
                return ActiveGame.SteamAppId == game.SteamAppId;
            }

            var activeIdentity = GetNormalizedIdentity(ActiveGame);
            var candidateIdentity = GetNormalizedIdentity(game);
            return !string.IsNullOrWhiteSpace(activeIdentity) &&
                   string.Equals(activeIdentity, candidateIdentity, StringComparison.OrdinalIgnoreCase);
        }

        public bool ShouldStop(DateTime nowUtc)
        {
            return IsAwaitingReplacement && nowUtc >= ExitDeadlineUtc.Value;
        }

        public uint[] GetProcessIds()
        {
            var result = new uint[processes.Count];
            processes.Keys.CopyTo(result, 0);
            return result;
        }

        public void Clear()
        {
            processes.Clear();
            ActiveGame = null;
            LastKnownPath = null;
            ExitDeadlineUtc = null;
            InstallFolder = null;
        }

        internal static string GetInstallFolder(string executablePath)
        {
            if (string.IsNullOrWhiteSpace(executablePath)) return null;
            try
            {
                if (!Path.IsPathRooted(executablePath)) return null;
                var folder = Path.GetDirectoryName(Path.GetFullPath(executablePath));
                if (string.IsNullOrWhiteSpace(folder)) return null;
                var root = Path.GetPathRoot(folder) ?? string.Empty;
                var segments = folder.Substring(root.Length).Split(
                    new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar },
                    StringSplitOptions.RemoveEmptyEntries);
                if (segments.Length < 2) return null;
                return folder.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
                       Path.DirectorySeparatorChar;
            }
            catch
            {
                return null;
            }
        }

        private static string GetNormalizedIdentity(SupportedGame game)
        {
            if (game == null) return string.Empty;
            var identity = !string.IsNullOrWhiteSpace(game.Normalized)
                ? game.Normalized
                : game.Title;
            return CloudGameListService.Normalize(identity ?? string.Empty);
        }
    }

    internal sealed class PreparedLaunchTracker
    {
        internal static readonly TimeSpan ExitGracePeriod = TimeSpan.FromSeconds(15);

        private readonly Dictionary<uint, string> processes = new Dictionary<uint, string>();

        public string ExecutablePath { get; private set; }
        public string InstallFolder { get; private set; }
        public string SessionTitle { get; private set; }
        public DateTime? MissingSinceUtc { get; private set; }

        public bool IsActive
        {
            get { return ExecutablePath != null; }
        }

        public int ProcessCount
        {
            get { return processes.Count; }
        }

        public void Begin(string executablePath, string sessionTitle, uint processId, DateTime nowUtc)
        {
            if (string.IsNullOrWhiteSpace(executablePath)) throw new ArgumentNullException("executablePath");

            processes.Clear();
            ExecutablePath = executablePath;
            InstallFolder = GameProcessSessionTracker.GetInstallFolder(executablePath);
            SessionTitle = sessionTitle;
            MissingSinceUtc = null;
            if (processId != 0) processes[processId] = executablePath;
            else MissingSinceUtc = nowUtc;
        }

        public bool Matches(string candidatePath)
        {
            if (!IsActive || string.IsNullOrWhiteSpace(candidatePath)) return false;
            return string.Equals(candidatePath, ExecutablePath, StringComparison.OrdinalIgnoreCase) ||
                   (InstallFolder != null &&
                    candidatePath.StartsWith(InstallFolder, StringComparison.OrdinalIgnoreCase));
        }

        public void Update(ICollection<uint> runningProcessIds, Func<uint, string> processPath, DateTime nowUtc)
        {
            if (!IsActive || runningProcessIds == null || processPath == null) return;

            var stopped = new List<uint>();
            foreach (var entry in processes)
            {
                if (!runningProcessIds.Contains(entry.Key))
                {
                    stopped.Add(entry.Key);
                    continue;
                }
                var currentPath = processPath(entry.Key);
                if (!string.IsNullOrWhiteSpace(currentPath) &&
                    !string.Equals(currentPath, entry.Value, StringComparison.OrdinalIgnoreCase))
                {
                    stopped.Add(entry.Key);
                }
            }
            foreach (var processId in stopped) processes.Remove(processId);

            if (processes.Count == 0)
            {
                foreach (var processId in runningProcessIds)
                {
                    if (processId == 0) continue;
                    var path = processPath(processId);
                    if (Matches(path)) processes[processId] = path;
                }
            }

            if (processes.Count > 0) MissingSinceUtc = null;
            else if (!MissingSinceUtc.HasValue) MissingSinceUtc = nowUtc;
        }

        public bool ShouldEnd(DateTime nowUtc)
        {
            return IsActive && MissingSinceUtc.HasValue && nowUtc - MissingSinceUtc.Value >= ExitGracePeriod;
        }

        public void Clear()
        {
            processes.Clear();
            ExecutablePath = null;
            InstallFolder = null;
            SessionTitle = null;
            MissingSinceUtc = null;
        }
    }
}
