using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
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

        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            AppDomain.CurrentDomain.UnhandledException += (s, args) =>
            {
                try
                {
                    var ex = args.ExceptionObject as Exception;
                    var msg = ex != null ? ex.ToString() : (args.ExceptionObject != null ? args.ExceptionObject.ToString() : "Unknown error");
                    AppLog.WriteLine("tray_crash.log", "[UNHANDLED] " + msg);
                }
                catch { }
            };

            DispatcherUnhandledException += (s, args) =>
            {
                try
                {
                    AppLog.WriteLine(
                        "tray_crash.log", "[DISPATCHER] " + args.Exception.ToString());
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
                gameListService.GamesUpdated += monitorService.ForceCheck;
                updateChecker = new UpdateCheckerService();

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
                        AppLog.WriteLine("tray_bridge.log", msg);
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
                    AppLog.WriteLine("tray_startup_error.log", ex.ToString());
                }
                catch { }
                MessageBox.Show(LocalizationManager.Get("Loc_StartupError") + ex.Message, "ApexSenseBridge Tray", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown();
            }
        }

        private void InitializeNotifyIcon()
        {
            contextMenu = new ContextMenuStrip();
            contextMenu.Renderer = new DarkTrayMenuRenderer();
            contextMenu.Font = new Font("Segoe UI Variable Text", 9.5f, System.Drawing.FontStyle.Regular);
            if (contextMenu.Font.Name != "Segoe UI Variable Text")
            {
                contextMenu.Font = new Font("Segoe UI", 9.5f, System.Drawing.FontStyle.Regular);
            }
            contextMenu.ShowImageMargin = false;
            contextMenu.ShowCheckMargin = false;
            contextMenu.BackColor = Color.FromArgb(36, 36, 36);
            contextMenu.ForeColor = Color.FromArgb(245, 245, 245);
            contextMenu.Padding = new System.Windows.Forms.Padding(4, 6, 4, 6);
            contextMenu.Opened += (s, e) => ModernizeMenuWindow(contextMenu);
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

            notifyIcon.Click += (s, e) =>
            {
                var me = e as MouseEventArgs;
                if (me == null || me.Button == MouseButtons.Left)
                {
                    ShowMainWindow();
                }
            };
            notifyIcon.DoubleClick += (s, e) =>
            {
                var me = e as MouseEventArgs;
                if (me == null || me.Button == MouseButtons.Left)
                {
                    ShowMainWindow();
                }
            };
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
            statusMenuItem.Tag = "statusHeader";
            statusMenuItem.ForeColor = (sessionManager != null && sessionManager.IsSessionActive)
                ? Color.FromArgb(52, 211, 153)
                : Color.FromArgb(160, 165, 175);
            statusMenuItem.Font = new Font("Segoe UI Variable Text", 9f, System.Drawing.FontStyle.Regular);
            if (statusMenuItem.Font.Name != "Segoe UI Variable Text")
            {
                statusMenuItem.Font = new Font("Segoe UI", 9f, System.Drawing.FontStyle.Regular);
            }
            statusMenuItem.Padding = new System.Windows.Forms.Padding(12, 6, 12, 6);
            contextMenu.Items.Add(statusMenuItem);
            contextMenu.Items.Add(new ToolStripSeparator());

            var openItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayOpen"), null, (s, e) => ShowMainWindow());
            openItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(openItem);

            var testControllerItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayTestController"), null, (s, e) => ShowControllerTestWindow());
            testControllerItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(testControllerItem);

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
            });
            autoDetectMenuItem.Checked = settings != null && settings.AutoDetectGames;
            autoDetectMenuItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(autoDetectMenuItem);

            var checkUpdatesItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayCheckUpdates"), null, async (s, e) =>
            {
                if (updateChecker != null)
                {
                    await updateChecker.CheckForUpdatesAsync(false);
                }
            });
            checkUpdatesItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(checkUpdatesItem);

            var controlPanelItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayControlPanel"), null, (s, e) =>
            {
                var controlPath = InstallLocator.ResolveControlPanel();
                if (!string.IsNullOrWhiteSpace(controlPath) && File.Exists(controlPath))
                {
                    try { Process.Start(new ProcessStartInfo(controlPath) { UseShellExecute = true }); } catch { }
                }
            });
            controlPanelItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(controlPanelItem);

            var langMenu = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayLanguage"));
            langMenu.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            langMenu.DropDown.Renderer = new DarkTrayMenuRenderer();
            var dropDownMenu = langMenu.DropDown as ToolStripDropDownMenu;
            if (dropDownMenu != null)
            {
                dropDownMenu.ShowImageMargin = false;
                dropDownMenu.ShowCheckMargin = false;
            }
            langMenu.DropDown.BackColor = Color.FromArgb(36, 36, 36);
            langMenu.DropDown.ForeColor = Color.FromArgb(245, 245, 245);
            langMenu.DropDown.Padding = new System.Windows.Forms.Padding(4, 6, 4, 6);
            langMenu.DropDown.Opened += (s, e) => ModernizeMenuWindow(langMenu.DropDown);
            var langEnglish = new ToolStripMenuItem("English", null, (s, e) => SwitchLanguage(LocalizationManager.LangEnglish));
            var langFrench = new ToolStripMenuItem("Français", null, (s, e) => SwitchLanguage(LocalizationManager.LangFrench));
            var langSpanish = new ToolStripMenuItem("Español", null, (s, e) => SwitchLanguage(LocalizationManager.LangSpanish));
            var langChinese = new ToolStripMenuItem("简体中文", null, (s, e) => SwitchLanguage(LocalizationManager.LangChinese));
            langEnglish.Padding = new System.Windows.Forms.Padding(8, 6, 8, 6);
            langFrench.Padding = new System.Windows.Forms.Padding(8, 6, 8, 6);
            langSpanish.Padding = new System.Windows.Forms.Padding(8, 6, 8, 6);
            langChinese.Padding = new System.Windows.Forms.Padding(8, 6, 8, 6);
            langEnglish.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangEnglish;
            langFrench.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangFrench;
            langSpanish.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangSpanish;
            langChinese.Checked = LocalizationManager.CurrentLanguage == LocalizationManager.LangChinese;
            langMenu.DropDownItems.Add(langEnglish);
            langMenu.DropDownItems.Add(langFrench);
            langMenu.DropDownItems.Add(langSpanish);
            langMenu.DropDownItems.Add(langChinese);
            contextMenu.Items.Add(langMenu);

            contextMenu.Items.Add(new ToolStripSeparator());

            var exitItem = new ToolStripMenuItem(LocalizationManager.Get("Loc_TrayExit"), null, (s, e) => ExitApplication());
            exitItem.Padding = new System.Windows.Forms.Padding(8, 7, 8, 7);
            contextMenu.Items.Add(exitItem);
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
                    if (statusMenuItem != null)
                    {
                        statusMenuItem.Text = text;
                        statusMenuItem.ForeColor = Color.FromArgb(52, 211, 153);
                    }
                    if (notifyIcon != null) notifyIcon.Text = text.Length > 63 ? text.Substring(0, 60) + "..." : text;
                }
                else
                {
                    if (statusMenuItem != null)
                    {
                        statusMenuItem.Text = LocalizationManager.Get("Loc_TrayStatusStandby");
                        statusMenuItem.ForeColor = Color.FromArgb(56, 189, 248);
                    }
                    if (notifyIcon != null) notifyIcon.Text = LocalizationManager.Get("Loc_TrayTooltipStandby");
                }
            }
            catch { }
        }

        private GameListWindow dashboardWindow;
        private readonly object windowLock = new object();
        private bool isOpeningDashboard;

        private void ShowMainWindow()
        {
            try
            {
                lock (windowLock)
                {
                    if (isOpeningDashboard) return;

                    if (dashboardWindow != null)
                    {
                        if (dashboardWindow.WindowState == WindowState.Minimized)
                        {
                            dashboardWindow.WindowState = WindowState.Normal;
                        }
                        dashboardWindow.Show();
                        dashboardWindow.Activate();
                        dashboardWindow.Focus();
                        return;
                    }

                    isOpeningDashboard = true;
                }

                Dispatcher.Invoke(() =>
                {
                    lock (windowLock)
                    {
                        if (dashboardWindow == null)
                        {
                            dashboardWindow = new GameListWindow(
                                gameListService, settings, learningService,
                                sessionManager, monitorService, updateChecker,
                                initialTab: "dashboard");

                            dashboardWindow.Closed += (s, e) =>
                            {
                                lock (windowLock)
                                {
                                    dashboardWindow = null;
                                }
                            };
                        }

                        if (dashboardWindow.WindowState == WindowState.Minimized)
                        {
                            dashboardWindow.WindowState = WindowState.Normal;
                        }
                        dashboardWindow.Show();
                        dashboardWindow.Activate();
                        dashboardWindow.Focus();
                    }
                });
            }
            catch (Exception ex)
            {
                try { AppLog.WriteLine("tray_crash.log", "[ShowMainWindow] " + ex); } catch { }
            }
            finally
            {
                lock (windowLock)
                {
                    isOpeningDashboard = false;
                }
            }
        }

        private ControllerTestWindow controllerTestWindow;

        private void ShowControllerTestWindow()
        {
            try
            {
                Dispatcher.Invoke(() =>
                {
                    lock (windowLock)
                    {
                        if (controllerTestWindow != null)
                        {
                            if (controllerTestWindow.WindowState == WindowState.Minimized)
                            {
                                controllerTestWindow.WindowState = WindowState.Normal;
                            }
                            controllerTestWindow.Show();
                            controllerTestWindow.Activate();
                            controllerTestWindow.Focus();
                            return;
                        }

                        controllerTestWindow = new ControllerTestWindow(settings);
                        controllerTestWindow.Closed += (s, e) =>
                        {
                            lock (windowLock)
                            {
                                controllerTestWindow = null;
                            }
                        };

                        controllerTestWindow.Show();
                        controllerTestWindow.Activate();
                        controllerTestWindow.Focus();
                    }
                });
            }
            catch (Exception ex)
            {
                try { AppLog.WriteLine("tray_crash.log", "[ShowControllerTestWindow] " + ex); } catch { }
            }
        }

        private void ExitApplication()
        {
            if (monitorService != null) monitorService.Dispose();
            if (learningService != null) learningService.Dispose();
            if (sessionManager != null) sessionManager.StopSession("Tray exiting");

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

            if (notifyIcon != null) notifyIcon.Dispose();
            if (singleInstanceMutex != null) singleInstanceMutex.Dispose();
            base.OnExit(e);
        }
        private static void ModernizeMenuWindow(ToolStripDropDown menu)
        {
            if (menu == null || !menu.IsHandleCreated) return;
            try
            {
                int cornerPreference = NativeMethods.DWMWCP_ROUND;
                NativeMethods.DwmSetWindowAttribute(menu.Handle, NativeMethods.DWMWA_WINDOW_CORNER_PREFERENCE, ref cornerPreference, sizeof(int));
            }
            catch { }
        }
    }

    internal sealed class DarkTrayColorTable : ProfessionalColorTable
    {
        public override Color ToolStripDropDownBackground => Color.FromArgb(36, 36, 36);
        public override Color MenuBorder => Color.FromArgb(36, 36, 36);
        public override Color MenuItemBorder => Color.Transparent;
        public override Color MenuItemSelected => Color.FromArgb(255, 255, 255);
        public override Color MenuItemSelectedGradientBegin => Color.Transparent;
        public override Color MenuItemSelectedGradientEnd => Color.Transparent;
        public override Color MenuItemPressedGradientBegin => Color.Transparent;
        public override Color MenuItemPressedGradientEnd => Color.Transparent;
        public override Color ImageMarginGradientBegin => Color.FromArgb(36, 36, 36);
        public override Color ImageMarginGradientMiddle => Color.FromArgb(36, 36, 36);
        public override Color ImageMarginGradientEnd => Color.FromArgb(36, 36, 36);
        public override Color SeparatorDark => Color.FromArgb(56, 56, 56);
        public override Color SeparatorLight => Color.Transparent;
        public override Color CheckBackground => Color.Transparent;
        public override Color CheckSelectedBackground => Color.Transparent;
        public override Color CheckPressedBackground => Color.Transparent;
    }

    internal sealed class DarkTrayMenuRenderer : ToolStripProfessionalRenderer
    {
        public DarkTrayMenuRenderer() : base(new DarkTrayColorTable())
        {
            RoundedEdges = true;
        }

        protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e)
        {
        }

        protected override void OnRenderSeparator(ToolStripSeparatorRenderEventArgs e)
        {
            int y = e.Item.Height / 2;
            int startX = 14;
            int endX = e.Item.Width - 14;
            using (var pen = new Pen(Color.FromArgb(56, 56, 56), 1f))
            {
                e.Graphics.DrawLine(pen, startX, y, endX, y);
            }
        }

        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            if (e.Item.Tag as string == "statusHeader")
            {
                e.TextColor = e.Item.ForeColor.IsEmpty || e.Item.ForeColor == System.Drawing.SystemColors.ControlText
                    ? Color.FromArgb(160, 165, 175)
                    : e.Item.ForeColor;
            }
            else if (!e.Item.Enabled)
            {
                e.TextColor = Color.FromArgb(130, 130, 130);
            }
            else
            {
                e.TextColor = Color.FromArgb(250, 250, 250);
            }
            base.OnRenderItemText(e);
        }

        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
        {
            if (e.Item.Selected && e.Item.Enabled)
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                var rect = new Rectangle(4, 1, e.Item.Width - 8, e.Item.Height - 2);
                using (var path = CreateRoundedPath(rect, 4))
                using (var brush = new SolidBrush(Color.FromArgb(20, 255, 255, 255)))
                {
                    e.Graphics.FillPath(brush, path);
                }
            }
            else
            {
                base.OnRenderMenuItemBackground(e);
            }
        }

        private static GraphicsPath CreateRoundedPath(Rectangle rect, int radius)
        {
            var path = new GraphicsPath();
            int diameter = radius * 2;
            var arc = new Rectangle(rect.Location, new System.Drawing.Size(diameter, diameter));

            path.AddArc(arc, 180, 90);
            arc.X = rect.Right - diameter;
            path.AddArc(arc, 270, 90);
            arc.Y = rect.Bottom - diameter;
            path.AddArc(arc, 0, 90);
            arc.X = rect.Left;
            path.AddArc(arc, 90, 90);
            path.CloseFigure();
            return path;
        }
    }
}
