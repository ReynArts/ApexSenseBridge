using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using Microsoft.Win32;
using System;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace ApexSenseBridgeTray
{
    public partial class BugReportWindow : Window
    {
        private const string GitHubNewIssueUrl =
            "https://github.com/ReynArts/ApexSenseBridge/issues/new";
        private const int MaximumScreenshots = 5;
        private const long MaximumScreenshotBytes = 20L * 1024L * 1024L;

        private readonly TraySettings settings;
        private readonly EngineSessionManager sessionManager;
        private readonly string controllerStatus;
        private readonly string reportId;
        private readonly ObservableCollection<ScreenshotItem> screenshots =
            new ObservableCollection<ScreenshotItem>();
        private string diagnosticMarkdown;

        public BugReportWindow(
            TraySettings settings,
            EngineSessionManager sessionManager,
            string controllerStatus)
        {
            this.settings = settings;
            this.sessionManager = sessionManager;
            this.controllerStatus = controllerStatus;
            reportId = "ASB-" + DateTime.UtcNow.ToString("yyyyMMdd") + "-" +
                       Guid.NewGuid().ToString("N").Substring(0, 6).ToUpperInvariant();

            InitializeComponent();
            LstScreenshots.ItemsSource = screenshots;
            Loaded += (sender, args) => TxtReportTitle.Focus();
            UpdateSubmitState();
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed) DragMove();
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void OnRequiredFieldChanged(object sender, TextChangedEventArgs e)
        {
            UpdateSubmitState();
        }

        private void OnConsentChanged(object sender, RoutedEventArgs e)
        {
            UpdateSubmitState();
        }

        private void UpdateSubmitState()
        {
            if (BtnContinueGithub == null || TxtReportTitle == null ||
                TxtReportDescription == null || ChkConsent == null) return;

            bool hasTitle = !string.IsNullOrWhiteSpace(TxtReportTitle.Text);
            bool hasDescription = !string.IsNullOrWhiteSpace(TxtReportDescription.Text);
            bool hasConsent = ChkConsent.IsChecked == true;
            BtnContinueGithub.IsEnabled = hasTitle && hasDescription && hasConsent;

            if (TxtValidationHint != null)
            {
                TxtValidationHint.Text = BtnContinueGithub.IsEnabled
                    ? LocalizationManager.Get("Loc_BugReportReadyHint")
                    : LocalizationManager.Get("Loc_BugReportRequiredHint");
            }
        }

        private void OnAddScreenshotsClick(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFileDialog
            {
                Title = LocalizationManager.Get("Loc_BtnAddScreenshots"),
                Filter = "Images (*.png;*.jpg;*.jpeg;*.gif;*.webp)|*.png;*.jpg;*.jpeg;*.gif;*.webp",
                Multiselect = true,
                CheckFileExists = true
            };

            if (dialog.ShowDialog(this) != true) return;

            bool rejected = false;
            foreach (string path in dialog.FileNames)
            {
                if (screenshots.Count >= MaximumScreenshots)
                {
                    rejected = true;
                    break;
                }
                if (ContainsScreenshot(path)) continue;

                try
                {
                    var file = new FileInfo(path);
                    if (!file.Exists || file.Length > MaximumScreenshotBytes)
                    {
                        rejected = true;
                        continue;
                    }
                    screenshots.Add(new ScreenshotItem(path));
                }
                catch
                {
                    rejected = true;
                }
            }

            LstScreenshots.Visibility = screenshots.Count > 0
                ? Visibility.Visible
                : Visibility.Collapsed;

            if (rejected)
            {
                MessageBox.Show(
                    LocalizationManager.Get("Loc_BugReportScreenshotLimit"),
                    LocalizationManager.Get("Loc_BugReportWindowTitle"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Information);
            }
        }

        private bool ContainsScreenshot(string path)
        {
            foreach (ScreenshotItem screenshot in screenshots)
            {
                if (string.Equals(screenshot.FullPath, path, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        private void OnRemoveScreenshotClick(object sender, RoutedEventArgs e)
        {
            var selected = LstScreenshots.SelectedItem as ScreenshotItem;
            if (selected != null) screenshots.Remove(selected);
            LstScreenshots.Visibility = screenshots.Count > 0
                ? Visibility.Visible
                : Visibility.Collapsed;
            BtnRemoveScreenshot.IsEnabled = LstScreenshots.SelectedItem != null;
        }

        private void OnScreenshotSelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (BtnRemoveScreenshot != null)
                BtnRemoveScreenshot.IsEnabled = LstScreenshots.SelectedItem != null;
        }

        private void OnPreviewDiagnosticsClick(object sender, RoutedEventArgs e)
        {
            EnsureDiagnosticsBuilt();
            TxtDiagnosticPreview.Text = diagnosticMarkdown;
            PnlDiagnosticPreview.Visibility = PnlDiagnosticPreview.Visibility == Visibility.Visible
                ? Visibility.Collapsed
                : Visibility.Visible;
        }

        private void EnsureDiagnosticsBuilt()
        {
            if (!string.IsNullOrEmpty(diagnosticMarkdown)) return;
            diagnosticMarkdown = DiagnosticReportService.BuildMarkdown(
                settings, sessionManager, controllerStatus, reportId);
        }

        private void OnContinueGithubClick(object sender, RoutedEventArgs e)
        {
            if (BtnContinueGithub.IsEnabled == false) return;

            try
            {
                EnsureDiagnosticsBuilt();
                string issueTitle = "[Bug] " + TxtReportTitle.Text.Trim();
                string issueBody = BuildIssueBody();
                string url = GitHubNewIssueUrl +
                             "?title=" + Uri.EscapeDataString(issueTitle) +
                             "&body=" + Uri.EscapeDataString(issueBody);

                CopyScreenshotsToClipboard();

                Process.Start(new ProcessStartInfo(url)
                {
                    UseShellExecute = true
                });

                AppLog.WriteLine(
                    "tray_bug_report.log",
                    "GitHub bug report draft opened (" + reportId + ", screenshots=" + screenshots.Count + ").");
                DialogResult = true;
            }
            catch (Exception ex)
            {
                AppLog.WriteLine("tray_bug_report.log", "Unable to open GitHub report draft: " + ex.Message);
                MessageBox.Show(
                    LocalizationManager.Get("Loc_BugReportOpenFailed"),
                    LocalizationManager.Get("Loc_BugReportWindowTitle"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
            }
        }

        private string BuildIssueBody()
        {
            var body = new StringBuilder(6144);
            body.AppendLine("## Description");
            body.AppendLine();
            body.AppendLine(TxtReportDescription.Text.Trim());
            body.AppendLine();

            if (screenshots.Count > 0)
            {
                body.AppendLine("## Screenshots");
                body.AppendLine();
                body.AppendLine("> " + LocalizationManager.Get("Loc_BugReportGithubPasteHint"));
                foreach (ScreenshotItem screenshot in screenshots)
                    body.AppendLine("- " + screenshot.FileName);
                body.AppendLine();
            }

            body.AppendLine("## Automatic installation report");
            body.AppendLine();
            body.AppendLine(diagnosticMarkdown);
            return body.ToString().TrimEnd();
        }

        private void CopyScreenshotsToClipboard()
        {
            if (screenshots.Count == 0) return;
            try
            {
                var files = new StringCollection();
                foreach (ScreenshotItem screenshot in screenshots)
                {
                    if (File.Exists(screenshot.FullPath)) files.Add(screenshot.FullPath);
                }
                if (files.Count > 0) Clipboard.SetFileDropList(files);
            }
            catch
            {
                // The issue draft still contains the report and the selected file names.
            }
        }

        private sealed class ScreenshotItem
        {
            public ScreenshotItem(string fullPath)
            {
                FullPath = fullPath;
                FileName = Path.GetFileName(fullPath);
            }

            public string FullPath { get; private set; }
            public string FileName { get; private set; }
        }
    }
}
