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
                args.Handled = true; // Never crash the UI process
            };

            try
            {
                bool isNewInstance;
                singleInstanceMutex = new Mutex(true, AppMutexName, out isNewInstance);
                if (!isNewInstance)
                {
                    MessageBox.Show("ApexSenseBridge Tray est déjà en cours d'exécution dans la barre des tâches.",
                                    "ApexSenseBridge", MessageBoxButton.OK, MessageBoxImage.Information);
                    Shutdown();
                    return;
                }

                ThemeManager.Initialize();

                settings = TraySettings.Load();
                gameListService = new CloudGameListService();
                gameListService.Initialize();

                sessionManager = new EngineSessionManager();
                monitorService = new ProcessMonitorService(gameListService, sessionManager, settings);
                updateChecker = new UpdateCheckerService();

                mainWindow = new MainWindow(gameListService, sessionManager, monitorService, updateChecker, settings);

                InitializeNotifyIcon();

                monitorService.GameDetected += (game, path) =>
                {
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try
                        {
                            if (settings.EnableNotifications && notifyIcon != null)
                            {
                                notifyIcon.ShowBalloonTip(
                                    3000,
                                    "ApexSenseBridge activé",
                                    string.Format("{0}\nProfil : {1}", game != null ? game.Title : "Jeu", game != null ? game.Profile : "Standard"),
                                    ToolTipIcon.Info);
                            }
                            UpdateTrayStatus();
                        }
                        catch { }
                    }));
                };

                monitorService.GameExited += (path) =>
                {
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionStarted += (game, profile) =>
                {
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionStopped += (reason) =>
                {
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try { UpdateTrayStatus(); } catch { }
                    }));
                };

                sessionManager.SessionError += (err) =>
                {
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try
                        {
                            UpdateTrayStatus();
                            if (settings.EnableNotifications && notifyIcon != null && !string.IsNullOrWhiteSpace(err))
                            {
                                notifyIcon.ShowBalloonTip(
                                    4000,
                                    "ApexSenseBridge — Avertissement",
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
                    Dispatcher.Invoke(new Action(() =>
                    {
                        try
                        {
                            if (settings.EnableNotifications && notifyIcon != null && info != null && info.HasUpdate)
                            {
                                notifyIcon.ShowBalloonTip(
                                    5000,
                                    "Mise à jour disponible",
                                    string.Format("ApexSenseBridge v{0} est disponible.", info.LatestVersion),
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
                MessageBox.Show("Erreur de démarrage : " + ex.Message, "ApexSenseBridge Tray", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown();
            }
        }

        private void InitializeNotifyIcon()
        {
            contextMenu = new ContextMenuStrip();

            statusMenuItem = new ToolStripMenuItem("ApexSenseBridge : En veille");
            statusMenuItem.Enabled = false;
            statusMenuItem.Font = new Font(contextMenu.Font, System.Drawing.FontStyle.Bold);
            contextMenu.Items.Add(statusMenuItem);
            contextMenu.Items.Add(new ToolStripSeparator());

            var openItem = new ToolStripMenuItem("Ouvrir l'interface...", null, (s, e) => ShowMainWindow());
            openItem.Font = new Font(contextMenu.Font, System.Drawing.FontStyle.Bold);
            contextMenu.Items.Add(openItem);

            autoDetectMenuItem = new ToolStripMenuItem("Détection automatique", null, (s, e) =>
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
            autoDetectMenuItem.Checked = settings.AutoDetectGames;
            contextMenu.Items.Add(autoDetectMenuItem);

            contextMenu.Items.Add(new ToolStripMenuItem("Rechercher les mises à jour...", null, async (s, e) =>
            {
                if (updateChecker != null)
                {
                    await updateChecker.CheckForUpdatesAsync(false);
                }
            }));

            contextMenu.Items.Add(new ToolStripMenuItem("Panneau de Contrôle...", null, (s, e) =>
            {
                var controlPath = InstallLocator.ResolveControlPanel();
                if (!string.IsNullOrWhiteSpace(controlPath) && File.Exists(controlPath))
                {
                    try { Process.Start(new ProcessStartInfo(controlPath) { UseShellExecute = true }); } catch { }
                }
            }));

            contextMenu.Items.Add(new ToolStripSeparator());

            contextMenu.Items.Add(new ToolStripMenuItem("Quitter", null, (s, e) => ExitApplication()));

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
            notifyIcon.Text = "ApexSenseBridge - En veille";
            notifyIcon.Visible = true;

            notifyIcon.DoubleClick += (s, e) => ShowMainWindow();
        }

        private void UpdateTrayStatus()
        {
            try
            {
                if (sessionManager.IsSessionActive)
                {
                    var text = string.Format("ApexSenseBridge : {0}", sessionManager.ActiveGameTitle);
                    if (statusMenuItem != null) statusMenuItem.Text = text;
                    if (notifyIcon != null) notifyIcon.Text = text.Length > 63 ? text.Substring(0, 60) + "..." : text;
                }
                else
                {
                    if (statusMenuItem != null) statusMenuItem.Text = "ApexSenseBridge : En veille";
                    if (notifyIcon != null) notifyIcon.Text = "ApexSenseBridge - En veille";
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
            if (notifyIcon != null) notifyIcon.Dispose();
            if (singleInstanceMutex != null) singleInstanceMutex.Dispose();
            base.OnExit(e);
        }
    }
}
