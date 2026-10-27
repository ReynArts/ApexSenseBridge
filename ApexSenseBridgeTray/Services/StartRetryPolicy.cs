using System;
using System.Collections.Generic;
using System.Linq;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class StartRetryPolicy
    {
        internal const int IdentityCheckFailedExitCode = 3;
        internal const int IdenticalFailureLimit = 2;
        internal static readonly TimeSpan RetryDelay = TimeSpan.FromSeconds(10);
        internal static readonly TimeSpan IdentityRetryDelay = TimeSpan.FromSeconds(2);
        internal static readonly TimeSpan IdenticalFailureWindow = TimeSpan.FromMinutes(3);
        internal static readonly TimeSpan GameClosedDelay = TimeSpan.FromSeconds(30);

        private sealed class FailureHistory
        {
            public int ExitCode;
            public string Reason;
            public int Repeats;
            public bool QuickRetryUsed;
            public bool Paused;
            public DateTime LastFailureUtc;
            public DateTime RetryAtUtc;
            public readonly HashSet<uint> ProcessIds = new HashSet<uint>();
            public DateTime? NoProcessSinceUtc;
        }

        private readonly Dictionary<string, FailureHistory> failures =
            new Dictionary<string, FailureHistory>(StringComparer.OrdinalIgnoreCase);
        private string controller;

        public bool HasPausedGame
        {
            get { return failures.Values.Any(failure => failure.Paused); }
        }

        public TimeSpan? RecordFailure(string game, int exitCode, string reason, DateTime nowUtc, params uint[] processIds)
        {
            var key = game ?? string.Empty;
            FailureHistory failure;
            if (!failures.TryGetValue(key, out failure))
            {
                failure = new FailureHistory();
                failures[key] = failure;
            }
            bool identical = failure.Repeats > 0 && failure.ExitCode == exitCode &&
                             string.Equals(failure.Reason, reason, StringComparison.Ordinal) &&
                             nowUtc - failure.LastFailureUtc <= IdenticalFailureWindow;
            failure.Repeats = identical ? failure.Repeats + 1 : 1;
            failure.ExitCode = exitCode;
            failure.Reason = reason;
            failure.LastFailureUtc = nowUtc;
            if (processIds != null)
                foreach (var processId in processIds)
                    if (processId != 0) failure.ProcessIds.Add(processId);
            if (failure.ProcessIds.Count > 0) failure.NoProcessSinceUtc = null;
            else if (!failure.NoProcessSinceUtc.HasValue) failure.NoProcessSinceUtc = nowUtc;

            TimeSpan? delay;
            if (exitCode == IdentityCheckFailedExitCode)
            {
                failure.Paused = false;
                delay = failure.QuickRetryUsed ? RetryDelay : IdentityRetryDelay;
                failure.QuickRetryUsed = true;
            }
            else
            {
                failure.Paused = failure.Repeats >= IdenticalFailureLimit;
                delay = failure.Paused ? (TimeSpan?)null : RetryDelay;
            }
            failure.RetryAtUtc = delay.HasValue ? nowUtc.Add(delay.Value) : DateTime.MaxValue;
            return delay;
        }

        public bool IsPaused(string game)
        {
            FailureHistory failure;
            return failures.TryGetValue(game ?? string.Empty, out failure) && failure.Paused;
        }

        public bool IsDeferred(string game, DateTime nowUtc, out DateTime retryAtUtc)
        {
            FailureHistory failure;
            retryAtUtc = DateTime.MinValue;
            if (!failures.TryGetValue(game ?? string.Empty, out failure)) return false;
            if (!failure.Paused && nowUtc >= failure.RetryAtUtc) return false;
            retryAtUtc = failure.Paused ? DateTime.MaxValue : failure.RetryAtUtc;
            return true;
        }

        public void Observe(string game, uint processId)
        {
            FailureHistory failure;
            if (processId == 0 || !failures.TryGetValue(game ?? string.Empty, out failure)) return;
            failure.ProcessIds.Add(processId);
            failure.NoProcessSinceUtc = null;
        }

        public void Forget(uint processId, DateTime nowUtc)
        {
            foreach (var failure in failures.Values)
                if (failure.ProcessIds.Remove(processId) && failure.ProcessIds.Count == 0)
                    failure.NoProcessSinceUtc = nowUtc;
        }

        public void Reset()
        {
            failures.Clear();
        }

        public bool HasEntries
        {
            get { return failures.Count > 0; }
        }

        public void Retain(ICollection<uint> runningProcessIds, DateTime nowUtc)
        {
            foreach (var failure in failures.Values)
            {
                failure.ProcessIds.RemoveWhere(processId => !runningProcessIds.Contains(processId));
                if (failure.ProcessIds.Count > 0) failure.NoProcessSinceUtc = null;
                else if (!failure.NoProcessSinceUtc.HasValue) failure.NoProcessSinceUtc = nowUtc;
            }
            var closed = failures.Where(entry => entry.Value.NoProcessSinceUtc.HasValue &&
                                                 nowUtc - entry.Value.NoProcessSinceUtc.Value >= GameClosedDelay)
                                 .Select(entry => entry.Key).ToList();
            foreach (var game in closed) failures.Remove(game);
        }

        public bool ObserveController(string status, bool sessionInProgress)
        {
            if (status == "unavailable")
            {
                if (!sessionInProgress && controller != null) controller = status;
                return false;
            }
            if (status != "apex4" && status != "apex5" && status != "apex6" && status != "disconnected")
                return false;
            var previous = controller;
            controller = status;
            return previous != null && previous != status;
        }
    }
}
