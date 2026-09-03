using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class GameProcessSessionTracker
    {
        private readonly Dictionary<uint, string> processes = new Dictionary<uint, string>();

        public SupportedGame ActiveGame { get; private set; }
        public string LastKnownPath { get; private set; }
        public DateTime? ExitDeadlineUtc { get; private set; }

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
            if (game == null) throw new ArgumentNullException("game");
            if (processId == 0) throw new ArgumentOutOfRangeException("processId");

            processes.Clear();
            ActiveGame = game;
            LastKnownPath = executablePath;
            ExitDeadlineUtc = null;
            processes[processId] = executablePath ?? string.Empty;
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
}
