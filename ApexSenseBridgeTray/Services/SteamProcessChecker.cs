using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace ApexSenseBridgeTray.Services
{
    internal static class SteamProcessChecker
    {
        private const string SteamProcessName = "steam";
        private static readonly object warnLock = new object();
        private static DateTime? warnedSteamStartUtc;

        internal static bool ShouldWarn(DateTime sessionStartUtc)
        {
            var starts = new List<DateTime?>();
            try
            {
                foreach (var process in Process.GetProcessesByName(SteamProcessName))
                {
                    using (process)
                    {
                        try { starts.Add(process.StartTime.ToUniversalTime()); }
                        catch (Exception) { starts.Add(null); }
                    }
                }
            }
            catch (Exception)
            {
                return false;
            }

            lock (warnLock)
            {
                DateTime? steamStartUtc;
                if (!Decide(starts, sessionStartUtc, warnedSteamStartUtc, out steamStartUtc)) return false;
                warnedSteamStartUtc = steamStartUtc;
                return true;
            }
        }

        // Warn once per Steam instance that was already running when the controller was hidden.
        internal static bool Decide(IEnumerable<DateTime?> steamStartsUtc, DateTime sessionStartUtc,
            DateTime? warnedSteamStartUtc, out DateTime? steamStartUtc)
        {
            steamStartUtc = null;
            bool runningBefore = false;
            foreach (var start in steamStartsUtc)
            {
                if (start.HasValue && start.Value >= sessionStartUtc) continue;
                runningBefore = true;
                var key = start ?? DateTime.MinValue;
                if (!steamStartUtc.HasValue || key < steamStartUtc.Value) steamStartUtc = key;
            }
            return runningBefore && steamStartUtc != warnedSteamStartUtc;
        }
    }
}
