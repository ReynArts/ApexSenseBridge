using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Forms;
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox;

namespace ApexSenseBridgeTray
{
    public partial class App : Application
    {
        private const string AppMutexName = @"Global\ApexSenseBridgeTrayMutex";
        private Mutex singleInstanceMutex;
        private NotifyIcon notifyIcon;
        private ContextMenuStrip contextMenu;
        private ToolStripMenuItem statusMenuItem;
        private ToolStripMenuItem autoDetectMenuItem;

        private TraySettings settings;
        private CloudGameListService gameListService;
        private EngineSessionManager sessionManager;
        private ExecutableLearningService learningService;
        private ProcessMonitorService monitorService;
        private UpdateCheckerService updateChecker;
        private MainWindow mainWindow;

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            AppDomain.CurrentDomain.UnhandledException += (s, args) =>
            {
                try
                {
                    var ex = args.ExceptionObject as Exception;
                    var msg = ex != null ? ex.ToString() : (args.ExceptionObject != null ? args.ExceptionObject.ToString() : "Unknown error");
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                        DateTime.Now.ToString("s") + " [UNHANDLED] " + msg + "\r\n\r\n");
                }
                catch { }
            };

            DispatcherUnhandledException += (s, args) =>
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_crash.log"),
                        DateTime.Now.ToString("s") + " [DISPATCHER] " + args.Exception.ToString() + "\r\n\r\n");
                }
                catch { }
                args.Handled = true;
            };

            try
            {
                settings = TraySettings.Load();
                LocalizationManager.Initialize(settings.Language);
                ThemeManager.Initialize();

                bool isNewInstance;
                singleInstanceMutex = new Mutex(true, AppMutexName, out isNewInstance);
                if (!isNewInstance)
                {
                    MessageBox.Show(LocalizationManager.Get("Loc_AlreadyRunning"),
                                    LocalizationManager.Get("Loc_AppName"), MessageBoxButton.OK, MessageBoxImage.Information);
                    Shutdown();
                    return;
                }

                gameListService = new CloudGameListService();
                gameListService.Initialize();

                sessionManager = new EngineSessionManager();
                learningService = new ExecutableLearningService();
                monitorService = new ProcessMonitorService(
                    gameListService, sessionManager, learningService, settings);
                updateChecker = new UpdateCheckerService();

                mainWindow = new MainWindow(
                    gameListService, sessionManager, learningService,
                    monitorService, updateChecker, settings);

                // Loading is deliberately started only after the existing process
                // watchers are active. Until it completes, detection follows the
                // unchanged exact/metadata/fuzzy path.
                learningService.InitializeAsync();

                InitializeNotifyIcon();

                LocalizationManager.LanguageChanged += () =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try
                        {
                            BuildContextMenu();
                            UpdateTrayStatus();
                        }
                        catch { }
                    }));
                };

                monitorService.GameDetected += (game, path) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try
                        {
                            if (settings.EnableNotifications && notifyIcon != null)
                            {
                                string gameTitle = game != null ? game.Title : LocalizationManager.Get("Loc_NotificationGame");
                                string profileName = game != null ? game.Profile : LocalizationManager.Get("Loc_NotificationProfileStandard");
                                notifyIcon.ShowBalloonTip(
                                    3000,
                                    LocalizationManager.Get("Loc_NotificationActivated"),
                                    LocalizationManager.Format("Loc_NotificationGameProfile", gameTitle, profileName),
                                    ToolTipIcon.Info);
                            }
                            UpdateTrayStatus();
                        }
                        catch { }
                    }));
                };

                monitorService.GameExited += (path) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionStarted += (game, profile) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionStopped += (reason) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionError += (err) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try
                        {
                            UpdateTrayStatus();
                            if (settings.EnableNotifications && notifyIcon != null && !string.IsNullOrWhiteSpace(err))
                            {
                                notifyIcon.ShowBalloonTip(
                                    4000,
                                    LocalizationManager.Get("Loc_NotificationWarning"),
                                    err,
                                    ToolTipIcon.Warning);
                            }
                        }
                        catch { }
                    }));
                };

                sessionManager.LogMessage += (msg) =>
                {
                    try
                    {
                        File.AppendAllText(
                            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_bridge.log"),
                            DateTime.Now.ToString("s") + " " + msg + "\r\n");
                    }
                    catch { }
                };

                updateChecker.UpdateAvailable += (info) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        try
                        {
                            if (settings.EnableNotifications && notifyIcon != null && info != null && info.HasUpdate)
                            {
                                notifyIcon.ShowBalloonTip(
                                    5000,
                                    LocalizationManager.Get("Loc_UpdateAvailableNotification"),
                                    LocalizationManager.Format("Loc_UpdateAvailableBody", info.LatestVersion),
                                    ToolTipIcon.Info);
                            }
                        }
                        catch { }
                    }));
                };

                ThreadPool.QueueUserWorkItem(async _ =>
                {
                    try { await gameListService.FetchLatestFromCloudAsync(); } catch { }
                });

                ThreadPool.QueueUserWorkItem(async _ =>
                {
                    try { await updateChecker.CheckForUpdatesAsync(true); } catch { }
                });
            }
            catch (Exception ex)
            {
                try
                {
                    File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "tray_startup_error.log"), ex.ToString());
                }
                catch { }
                MessageBox.Show(LocalizationManager.Get("Loc_StartupError") + ex.Message, "ApexSenseBridge Tray", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown();
            }
        }

        private void InitializeNotifyIcon()
        {
            contextMenu = new ContextMenuStrip();
            BuildContextMenu();

            Icon appIcon = SystemIcons.Application;
            try
            {
                var iconPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Resources", "app.ico");
                if (!File.Exists(iconPath))
                {
                    iconPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "app.ico");
                }
                if (File.Exists(iconPath))
                {
                    appIcon = new Icon(iconPath);
                }
                else
                {
                    var resourceUri = new Uri("pack://application:,,,/ApexSenseBridgeTray;component/Resources/app.ico");
                    var info = System.Windows.Application.GetResourceStream(resourceUri);
                    if (info != null && info.Stream != null)
                    {
                        appIcon = new Icon(info.Stream);
                    }
                }
            }
            catch
            {
            }

            notifyIcon = new NotifyIcon();
            notifyIcon.Icon = appIcon;
            notifyIcon.ContextMenuStrip = contextMenu;
            notifyIcon.Text = LocalizationManager.Get("Loc_TrayTooltipStandby");
            notifyIcon.Visible = true;

            notifyIcon.DoubleClick += (s, e) => ShowMainWindow();
        }

        private void BuildContextMenu()
        {
            if (contextMenu == null) return;
            contextMenu.Items.Clear();

            string statusText = (sessionManager != null && sessionManager.IsSessionActive)
                ? LocalizationManager.Format("Loc_TrayStatusActive", sessionManager.ActiveGameTitle)
                : LocalizationManager.Get("Loc_TrayStatusStandby");

            statusMenuItem = new ToolStripMenuItem(statusText);
            statusMenuItem.Enabled = false;
            statusMenuItem.Font = new Font(contextMenu.Font, System.Drawing.FontStyle.Bold);
            contextMenu.Items.Add(statusMenuItem);
            contextMenu.Items.Add(new ToolStripSeparator());

            var openItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayOpen"), null, (s, e) => ShowMainWindow());
            openItem.Font = new Font(contextMenu.Font, System.Drawing.FontStyle.Bold);
            contextMenu.Items.Add(openItem);

            autoDetectMenuItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayAutoDetect"), null, (s, e) =>
            {
                settings.AutoDetectGames = !settings.AutoDetectGames;
                autoDetectMenuItem.Checked = settings.AutoDetectGames;
                settings.Save();
                if (settings.AutoDetectGames)
                {
                    monitorService.ForceCheck();
                }
                else if (sessionManager.IsSessionActive && settings.ForcedProfile == "none")
                {
                    sessionManager.StopSession("Auto-detect disabled from tray");
                }
                mainWindow.UpdateSessionStatus();
            });
            autoDetectMenuItem.Checked = settings != null && settings.AutoDetectGames;
            contextMenu.Items.Add(autoDetectMenuItem);

            contextMenu.Items.Add(new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayCheckUpdates"), null, async (s, e) =>
            {
                if (updateChecker != null)
                {
                    await updateChecker.CheckForUpdatesAsync(false);
                }
            }));

            contextMenu.Items.Add(new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayControlPanel"), null, (s, e) =>
            {
                var controlPath = InstallLocator.ResolveControlPanel();
                if (!string.IsNullOrWhiteSpace(controlPath) && File.Exists(controlPath))
                {
                    try { Process.Start(new ProcessStartInfo(controlPath) { UseShellExecute = true }); } catch { }
                }
            }));

            // Language submenu
            var langMenu = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayLanguage"));
            var langEnglish = new ToolStripMenuItem("English", null, (s, e) => SwitchLanguage(LocalizationManager.LangEnglish));
            var langFrench = new ToolStripMenuItem("Français", null, (s, e) => SwitchLanguage(LocalizationManager.LangFrench));
            langEnglish.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangEnglish;
            langFrench.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangFrench;
            langMenu.DropDownItems.Add(langEnglish);
            langMenu.DropDownItems.Add(langFrench);
            contextMenu.Items.Add(langMenu);

            contextMenu.Items.Add(new ToolStripSeparator());

            contextMenu.Items.Add(new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayExit"), null, (s, e) => ExitApplication()));
        }

        private void SwitchLanguage(string lang)
        {
            if (settings != null)
            {
                settings.Language = lang;
                settings.Save();
            }
            LocalizationManager.SetLanguage(lang);
        }

        private void UpdateTrayStatus()
        {
            try
            {
                if (sessionManager != null && sessionManager.IsSessionActive)
                {
                    var text = LocalizationManager.Format("Loc_TrayStatusActive", sessionManager.ActiveGameTitle);
                    if (statusMenuItem != null) statusMenuItem.Text = text;
                    if (notifyIcon != null) notifyIcon.Text = text.Length > 63 ? text.Substring(0, 60) + "..." : text;
                }
                else
                {
                    if (statusMenuItem != null) statusMenuItem.Text = LocalizationManager.Get("Loc_TrayStatusStandby");
                    if (notifyIcon != null) notifyIcon.Text = LocalizationManager.Get("Loc_TrayTooltipStandby");
                }
            }
            catch { }
        }

        private void ShowMainWindow()
        {
            try
            {
                mainWindow.Show();
                mainWindow.WindowState = WindowState.Normal;
                mainWindow.Activate();
            }
            catch { }
        }

        private void ExitApplication()
        {
            if (monitorService != null) monitorService.Dispose();
            if (learningService != null) learningService.Dispose();
            if (sessionManager != null) sessionManager.StopSession("Tray exiting");
            EngineSessionManager.KillOrphanProcesses();

            if (notifyIcon != null)
            {
                notifyIcon.Visible = false;
                notifyIcon.Dispose();
            }
            if (singleInstanceMutex != null)
            {
                singleInstanceMutex.ReleaseMutex();
                singleInstanceMutex.Dispose();
            }
            Shutdown();
        }

        protected override void OnExit(ExitEventArgs e)
        {
            if (monitorService != null) monitorService.Dispose();
            if (learningService != null) learningService.Dispose();
            if (sessionManager != null) sessionManager.StopSession("Tray app closing");
            EngineSessionManager.KillOrphanProcesses();

            if (notifyIcon != null) notifyIcon.Dispose();
            if (singleInstanceMutex != null) singleInstanceMutex.Dispose();
            base.OnExit(e);
        }
    }
}
