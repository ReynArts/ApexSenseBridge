using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace ApexSenseBridgeTray.Services
{
    public class ProcessMonitorService : IDisposable
    {
        private readonly CloudGameListService gameListService;
        private readonly EngineSessionManager sessionManager;
        private readonly TraySettings settings;
        private readonly Timer pollTimer;
        private uint activeMonitoredPid;
        private string activeMonitoredPath;
        private bool isDisposed;
        private int isPolling;

        public event Action<SupportedGame, string> GameDetected; // game, exePath
        public event Action<string> GameExited; // exePath

        public ProcessMonitorService(
            CloudGameListService gameListService,
            EngineSessionManager sessionManager,
            TraySettings settings)
        {
            this.gameListService = gameListService;
            this.sessionManager = sessionManager;
            this.settings = settings;

            pollTimer = new Timer(OnPollTick, null, 1000, 1000);
        }

        public void ForceCheck()
        {
            ThreadPool.QueueUserWorkItem(_ => OnPollTick(null));
        }

        private void OnPollTick(object state)
        {
            if (isDisposed) return;

            // Prevent re-entrant polling if previous tick is still running
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

                if (settings == null) return;

                if (settings.ForcedProfile != null && settings.ForcedProfile != "none")
                {
                    return;
                }

                if (!settings.AutoDetectGames)
                {
                    return;
                }

                var hwnd = NativeMethods.GetForegroundWindow();
                if (hwnd == IntPtr.Zero) return;

                uint pid;
                var exePath = NativeMethods.GetActiveProcessPath(hwnd, out pid);
                if (string.IsNullOrWhiteSpace(exePath) || pid == 0) return;

                if (pid == activeMonitoredPid) return;

                var fileName = Path.GetFileName(exePath);
                if (IsSystemOrIgnoredProcess(fileName)) return;

                var folderName = GetParentFolderName(exePath);
                var exeTitle = Path.GetFileNameWithoutExtension(exePath);
                var windowTitle = NativeMethods.GetActiveWindowTitle(hwnd);

                SupportedGame matchedGame = null;

                if (gameListService != null &&
                    (gameListService.TryFindGame(folderName, out matchedGame) ||
                     gameListService.TryFindGame(windowTitle, out matchedGame) ||
                     gameListService.TryFindGame(exeTitle, out matchedGame)))
                {
                    if (matchedGame != null)
                    {
                        if (settings.IsGameExcluded(matchedGame.Normalized) || settings.IsGameExcluded(matchedGame.Title))
                        {
                            return;
                        }

                        bool matchesCriteria = (settings.TriggerOnAdaptiveTriggers && matchedGame.AdaptiveTriggers) ||
                                               (settings.TriggerOnHapticFeedback && matchedGame.HapticFeedback);

                        if (matchesCriteria)
                        {
                            HandleGameDetected(matchedGame, pid, exePath);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                        DateTime.Now.ToString("s") + " [OnPollTick] " + ex.ToString() + "\r\n");
                }
                catch { }
            }
            finally
            {
                Interlocked.Exchange(ref isPolling, 0);
            }
        }

        private void HandleGameDetected(SupportedGame game, uint pid, string exePath)
        {
            if (game == null) return;

            try
            {
                if (activeMonitoredPid != 0 && activeMonitoredPid != pid)
                {
                    sessionManager.StopSession("Switching to new detected game: " + game.Title);
                }

                activeMonitoredPid = pid;
                activeMonitoredPath = exePath;

                string error;
                if (sessionManager.StartSession(game.Title, game.Profile, settings, out error))
                {
                    var handler = GameDetected;
                    if (handler != null)
                    {
                        handler(game, exePath);
                    }
                }
            }
            catch (Exception ex)
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                        DateTime.Now.ToString("s") + " [HandleGameDetected] " + ex.ToString() + "\r\n");
                }
                catch { }
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
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                        DateTime.Now.ToString("s") + " [HandleGameExited] " + ex.ToString() + "\r\n");
                }
                catch { }
            }
        }

        private static bool IsSystemOrIgnoredProcess(string fileName)
        {
            if (string.IsNullOrWhiteSpace(fileName)) return true;
            var lower = fileName.ToLowerInvariant();
            return lower == "explorer.exe" ||
                   lower == "taskmgr.exe" ||
                   lower == "apexsensebridge.exe" ||
                   lower == "apexsensebridgetray.exe" ||
                   lower == "apexsensebridgecontrol.exe" ||
                   lower == "steam.exe" ||
                   lower == "steamwebhelper.exe" ||
                   lower == "epicgameslauncher.exe" ||
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

        public void Dispose()
        {
            if (isDisposed) return;
            isDisposed = true;
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
