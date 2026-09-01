using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Management;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    public class ProcessMonitorService : IDisposable
    {
        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly TraySettings settings;
        private readonly Timer pollTimer;
        private readonly Dictionary<uint, DateTime> retryCooldowns = new Dictionary<uint, DateTime>();
        private readonly Dictionary<uint, long> evaluatedProcesses = new Dictionary<uint, long>();

        private ManagementEventWatcher startWatcher;
        private ManagementEventWatcher stopWatcher;

        private uint activeMonitoredPid;
        private string activeMonitoredPath;
        private bool isDisposed;
        private int isPolling;
        private DateTime nextProcessSweepUtc;
        private DateTime nextForegroundCheckUtc;

        public event Action<SupportedGame, string> GameDetected;
        public event Action<string> GameExited;

        public ProcessMonitorService(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            TraySettings settings)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.settings = settings;

            InitializeWmiWatchers();

            pollTimer = new Timer(OnPollTick, null, 100, 250);
        }

        private void InitializeWmiWatchers()
        {
            try
            {
                startWatcher = new ManagementEventWatcher(new WqlEventQuery("SELECT * FROM Win32_ProcessStartTrace"));
                startWatcher.EventArrived += OnProcessStartedWmi;
                startWatcher.Start();
            }
            catch (Exception ex)
            {
                LogEvent("WMI StartWatcher unavailable: " + ex.Message);
                if (startWatcher != null)
                {
                    try { startWatcher.Dispose(); } catch { }
                    startWatcher = null;
                }
            }

            try
            {
                stopWatcher = new ManagementEventWatcher(new WqlEventQuery("SELECT * FROM Win32_ProcessStopTrace"));
                stopWatcher.EventArrived += OnProcessStoppedWmi;
                stopWatcher.Start();
            }
            catch (Exception ex)
            {
                LogEvent("WMI StopWatcher unavailable: " + ex.Message);
                if (stopWatcher != null)
                {
                    try { stopWatcher.Dispose(); } catch { }
                    stopWatcher = null;
                }
            }
        }

        private void OnProcessStartedWmi(object sender, EventArrivedEventArgs e)
        {
            if (isDisposed) return;

            try
            {
                var processName = e.NewEvent.Properties["ProcessName"].Value as string;
                var pidObj = e.NewEvent.Properties["ProcessID"].Value;
                if (string.IsNullOrWhiteSpace(processName) || pidObj == null) return;

                uint pid = Convert.ToUInt32(pidObj);
                if (pid == 0 || pid == activeMonitoredPid) return;
                if (IsSystemOrIgnoredProcess(processName)) return;

                if (settings == null || !settings.AutoDetectGames) return;
                if (settings.ForcedProfile != null && settings.ForcedProfile != "none") return;

                CheckCandidateProcess(pid, processName, null);
            }
            catch (Exception ex)
            {
                LogEvent("[OnProcessStartedWmi] " + ex.Message);
            }
        }

        private void OnProcessStoppedWmi(object sender, EventArrivedEventArgs e)
        {
            if (isDisposed) return;

            try
            {
                var pidObj = e.NewEvent.Properties["ProcessID"].Value;
                if (pidObj == null) return;

                uint pid = Convert.ToUInt32(pidObj);
                if (pid != 0 && pid == activeMonitoredPid)
                {
                    HandleGameExited("Process stopped (WMI)");
                }
            }
            catch (Exception ex)
            {
                LogEvent("[OnProcessStoppedWmi] " + ex.Message);
            }
        }

        public void ForceCheck()
        {
            ThreadPool.QueueUserWorkItem(_ => OnPollTick(null));
        }

        private void OnPollTick(object state)
        {
            if (isDisposed) return;

            if (Interlocked.CompareExchange(ref isPolling, 1, 0) != 0)
            {
                return;
            }

            try
            {
                if (activeMonitoredPid != 0)
                {
                    try
                    {
                        var proc = Process.GetProcessById((int)activeMonitoredPid);
                        if (proc.HasExited)
                        {
                            HandleGameExited("Process terminated");
                            return;
                        }
                    }
                    catch (ArgumentException)
                    {
                        HandleGameExited("Process exited");
                        return;
                    }
                    catch (Exception)
                    {
                        HandleGameExited("Process inaccessible");
                        return;
                    }
                }

                if (settings == null || !settings.AutoDetectGames) return;
                if (settings.ForcedProfile != null && settings.ForcedProfile != "none") return;

                if (activeMonitoredPid == 0 && DateTime.UtcNow >= nextProcessSweepUtc)
                {
                    nextProcessSweepUtc = DateTime.UtcNow.AddMilliseconds(250);
                    if (ScanRunningProcesses()) return;
                }

                if (DateTime.UtcNow < nextForegroundCheckUtc) return;
                nextForegroundCheckUtc = DateTime.UtcNow.AddSeconds(1);

                var hwnd = NativeMethods.GetForegroundWindow();
                if (hwnd == IntPtr.Zero) return;

                uint pid;
                var exePath = NativeMethods.GetActiveProcessPath(hwnd, out pid);
                if (string.IsNullOrWhiteSpace(exePath) || pid == 0) return;
                if (pid == activeMonitoredPid) return;

                var fileName = Path.GetFileName(exePath);
                if (IsSystemOrIgnoredProcess(fileName)) return;

                CheckCandidateProcess(pid, fileName, exePath);
            }
            catch (Exception ex)
            {
                LogEvent("[OnPollTick] " + ex.Message);
            }
            finally
            {
                Interlocked.Exchange(ref isPolling, 0);
            }
        }

        private bool ScanRunningProcesses()
        {
            var processIds = NativeMethods.GetProcessIds();
            if (processIds.Length == 0) return false;

            var runningPids = new HashSet<uint>();
            foreach (var pid in processIds)
            {
                if (pid == 0) continue;
                runningPids.Add(pid);
                if (pid == activeMonitoredPid) return true;

                lock (evaluatedProcesses)
                {
                    if (evaluatedProcesses.ContainsKey(pid))
                    {
                        continue;
                    }
                    evaluatedProcesses[pid] = 0;
                }

                try
                {
                    using (var process = Process.GetProcessById((int)pid))
                    {
                        string fileName = process.ProcessName + ".exe";
                        if (IsSystemOrIgnoredProcess(fileName)) continue;

                        long startedAt;
                        try
                        {
                            startedAt = process.StartTime.ToUniversalTime().Ticks;
                        }
                        catch
                        {
                            startedAt = 0;
                        }
                        lock (evaluatedProcesses)
                        {
                            evaluatedProcesses[pid] = startedAt;
                        }

                        string exePath = null;
                        try
                        {
                            if (process.MainModule != null)
                            {
                                exePath = process.MainModule.FileName;
                                fileName = Path.GetFileName(exePath);
                            }
                        }
                        catch
                        {
                        }

                        if (IsSystemOrIgnoredProcess(fileName)) continue;

                        CheckCandidateProcess(pid, fileName, exePath);
                        if (activeMonitoredPid == pid) return true;
                    }
                }
                catch (ArgumentException)
                {
                    lock (evaluatedProcesses)
                    {
                        evaluatedProcesses.Remove(pid);
                    }
                }
                catch (Exception ex)
                {
                    LogEvent("[ScanRunningProcesses] Candidate failed: " + ex.Message);
                }
            }

            lock (evaluatedProcesses)
            {
                var stoppedPids = new List<uint>();
                foreach (var pid in evaluatedProcesses.Keys)
                {
                    if (!runningPids.Contains(pid)) stoppedPids.Add(pid);
                }
                foreach (var pid in stoppedPids)
                {
                    evaluatedProcesses.Remove(pid);
                }
            }
            return false;
        }

        private void CheckCandidateProcess(uint pid, string fileName, string fullPath = null)
        {
            lock (retryCooldowns)
            {
                if (retryCooldowns.ContainsKey(pid))
                {
                    if (DateTime.UtcNow < retryCooldowns[pid])
                    {
                        return;
                    }
                    retryCooldowns.Remove(pid);
                }
            }

            string exePath = fullPath;
            if (string.IsNullOrWhiteSpace(exePath))
            {
                try
                {
                    var proc = Process.GetProcessById((int)pid);
                    exePath = proc.MainModule != null ? proc.MainModule.FileName : fileName;
                }
                catch
                {
                    exePath = fileName;
                }
            }

            var exeTitle = Path.GetFileNameWithoutExtension(exePath);
            var folderName = GetParentFolderName(exePath);

            SupportedGame matchedGame;
            string matchedBy;
            if (TryResolveGame(exePath, exeTitle, folderName, fileName, out matchedGame, out matchedBy))
            {
                if (settings.IsGameExcluded(matchedGame.Normalized) ||
                    settings.IsGameExcluded(matchedGame.Title) ||
                    settings.IsGameExcluded(exeTitle) ||
                    settings.IsGameExcluded(folderName) ||
                    settings.IsGameExcluded(fileName) ||
                    (matchedGame.SteamAppId > 0 && settings.IsGameExcluded(matchedGame.SteamAppId.ToString())))
                {
                    return;
                }

                bool matchesCriteria = (settings.TriggerOnAdaptiveTriggers && matchedGame.AdaptiveTriggers) ||
                                       (settings.TriggerOnHapticFeedback && matchedGame.HapticFeedback);

                if (matchesCriteria)
                {
                    HandleGameDetected(matchedGame, pid, exePath, matchedBy);
                }
            }
        }

        private bool TryResolveGame(
            string exePath,
            string exeTitle,
            string folderName,
            string fileName,
            out SupportedGame game,
            out string matchedBy)
        {
            game = null;
            matchedBy = null;
            if (gameListService == null) return false;

            var candidates = new List<KeyValuePair<string, string>>();
            AddExecutableMetadataCandidates(exePath, candidates);
            AddCandidate(candidates, "folder name", folderName);
            AddCandidate(candidates, "executable name", exeTitle);
            AddCandidate(candidates, "file name", fileName);

            foreach (var candidate in candidates)
            {
                if (gameListService.TryFindExactGame(candidate.Value, out game))
                {
                    matchedBy = FormatMatchEvidence(candidate);
                    return true;
                }
            }

            foreach (var candidate in candidates)
            {
                if (gameListService.TryFindGame(candidate.Value, out game))
                {
                    matchedBy = FormatMatchEvidence(candidate);
                    return true;
                }
            }

            return false;
        }

        private static void AddExecutableMetadataCandidates(
            string exePath,
            ICollection<KeyValuePair<string, string>> candidates)
        {
            if (string.IsNullOrWhiteSpace(exePath) || !File.Exists(exePath)) return;

            try
            {
                var version = FileVersionInfo.GetVersionInfo(exePath);
                AddCandidate(candidates, "product name", version.ProductName);
                AddCandidate(candidates, "file description", version.FileDescription);
            }
            catch
            {
            }
        }

        private static void AddCandidate(
            ICollection<KeyValuePair<string, string>> candidates,
            string source,
            string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return;

            foreach (var candidate in candidates)
            {
                if (string.Equals(candidate.Value, value, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }
            }

            candidates.Add(new KeyValuePair<string, string>(source, value.Trim()));
        }

        private static string FormatMatchEvidence(KeyValuePair<string, string> candidate)
        {
            return candidate.Key + " '" + candidate.Value + "'";
        }

        private void HandleGameDetected(SupportedGame game, uint pid, string exePath, string matchedBy)
        {
            if (game == null) return;

            try
            {
                if (activeMonitoredPid != 0 && activeMonitoredPid != pid)
                {
                    sessionManager.StopSession("Switching to new detected game: " + game.Title);
                }

                string error;
                if (sessionManager.StartSession(game.Title, game.Profile, settings, out error))
                {
                    activeMonitoredPid = pid;
                    activeMonitoredPath = exePath;

                    LogDetection(string.Format(
                        "Matched PID {0} to '{1}' using {2}. Path: {3}",
                        pid,
                        game.Title,
                        matchedBy ?? "unknown evidence",
                        exePath));

                    var handler = GameDetected;
                    if (handler != null)
                    {
                        handler(game, exePath);
                    }
                }
                else
                {
                    lock (retryCooldowns)
                    {
                        retryCooldowns[pid] = DateTime.UtcNow.AddSeconds(10);
                    }
                    lock (evaluatedProcesses)
                    {
                        evaluatedProcesses.Remove(pid);
                    }
                }
            }
            catch (Exception ex)
            {
                LogEvent("[HandleGameDetected] " + ex.Message);
            }
        }

        private void HandleGameExited(string reason)
        {
            try
            {
                var oldPath = activeMonitoredPath;
                activeMonitoredPid = 0;
                activeMonitoredPath = null;

                sessionManager.StopSession(reason);
                if (!string.IsNullOrWhiteSpace(oldPath))
                {
                    var handler = GameExited;
                    if (handler != null)
                    {
                        handler(oldPath);
                    }
                }
            }
            catch (Exception ex)
            {
                LogEvent("[HandleGameExited] " + ex.Message);
            }
        }

        private static bool IsSystemOrIgnoredProcess(string fileName)
        {
            if (string.IsNullOrWhiteSpace(fileName)) return true;
            var lower = fileName.ToLowerInvariant();
            return lower == "system.exe" ||
                   lower == "registry.exe" ||
                   lower == "secure system.exe" ||
                   lower == "memory compression.exe" ||
                   lower == "explorer.exe" ||
                   lower == "taskmgr.exe" ||
                   lower == "apexsensebridge.exe" ||
                   lower == "apexsensebridgetray.exe" ||
                   lower == "apexsensebridgecontrol.exe" ||
                   lower == "steam.exe" ||
                   lower == "steamwebhelper.exe" ||
                   lower == "steamservice.exe" ||
                   lower == "epicgameslauncher.exe" ||
                   lower == "battle.net.exe" ||
                   lower == "agent.exe" ||
                   lower == "playnite.desktopapp.exe" ||
                   lower == "playnite.fullscreenapp.exe" ||
                   lower == "applicationframehost.exe" ||
                   lower == "shellexperiencehost.exe" ||
                   lower == "systemsettings.exe" ||
                   lower == "searchhost.exe" ||
                   lower == "startmenuexperiencehost.exe" ||
                   lower == "lockapp.exe" ||
                   lower == "devenv.exe" ||
                   lower == "cmd.exe" ||
                   lower == "powershell.exe" ||
                   lower == "pwsh.exe" ||
                   lower == "conhost.exe" ||
                   lower == "windowsterminal.exe";
        }

        private static string GetParentFolderName(string filePath)
        {
            try
            {
                var dir = Path.GetDirectoryName(filePath);
                if (string.IsNullOrWhiteSpace(dir)) return string.Empty;
                return Path.GetFileName(dir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            }
            catch
            {
                return string.Empty;
            }
        }

        private static void LogEvent(string msg)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                    DateTime.Now.ToString("s") + " " + msg + "\r\n");
            }
            catch { }
        }

        private static void LogDetection(string message)
        {
            try
            {
                File.AppendAllText(
                    Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_detection.log"),
                    DateTime.Now.ToString("s") + " " + message + "\r\n");
            }
            catch
            {
            }
        }

        public void Dispose()
        {
            if (isDisposed) return;
            isDisposed = true;

            try
            {
                if (startWatcher != null)
                {
                    startWatcher.Stop();
                    startWatcher.Dispose();
                }
            }
            catch { }

            try
            {
                if (stopWatcher != null)
                {
                    stopWatcher.Stop();
                    stopWatcher.Dispose();
                }
            }
            catch { }

            if (pollTimer != null)
            {
                pollTimer.Dispose();
            }

            if (activeMonitoredPid != 0)
            {
                try
                {
                    sessionManager.StopSession("Tray app closing");
                }
                catch { }
            }
        }
    }
}
