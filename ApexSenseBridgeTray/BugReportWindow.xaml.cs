using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using System.IO;
using System.Linq;
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

        private enum ReportCategory { Bug, Feature, Game }

        private readonly TraySettings settings;
        private readonly EngineSessionManager sessionManager;
        private readonly string controllerStatus;
        private readonly string reportId;
        private readonly ObservableCollection<ScreenshotItem> screenshots =
            new ObservableCollection<ScreenshotItem>();
        private string diagnosticMarkdown;
        private bool initialized;
        private bool sending;

        private readonly ReportCategory category;

        public BugReportWindow(
            TraySettings settings,
            EngineSessionManager sessionManager,
            string controllerStatus,
            ReportChoice choice = ReportChoice.Bug)
        {
            category = choice == ReportChoice.Feature ? ReportCategory.Feature
                : choice == ReportChoice.Game ? ReportCategory.Game
                : ReportCategory.Bug;
            this.settings = settings;
            this.sessionManager = sessionManager;
            this.controllerStatus = controllerStatus;
            reportId = DiagnosticReportService.NewReportId();

            InitializeComponent();
            LstScreenshots.ItemsSource = screenshots;
            initialized = true;
            ApplyCategory();
            Loaded += (sender, args) => TxtReportTitle.Focus();
        }

        private ReportCategory Category
        {
            get { return category; }
        }

        private bool IncludesDiagnostics
        {
            get { return Category != ReportCategory.Feature; }
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed) DragMove();
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            if (sending) return;
            Close();
        }

        private void ApplyCategory()
        {
            var category = Category;
            string geometry = category == ReportCategory.Feature ? "GeoReportFeature"
                : category == ReportCategory.Game ? "GeoReportGame" : "GeoReportBug";
            string tint = category == ReportCategory.Feature ? "#3334D399"
                : category == ReportCategory.Game ? "#331E6FE8" : "#33F59E0B";
            string stroke = category == ReportCategory.Feature ? "#34D399"
                : category == ReportCategory.Game ? "#7FB0FF" : "#FBBF24";
            var converter = new System.Windows.Media.BrushConverter();
            IconHeader.Data = (System.Windows.Media.Geometry)FindResource(geometry);
            BubbleHeader.Background = (System.Windows.Media.Brush)converter.ConvertFromString(tint);
            IconHeader.Stroke = (System.Windows.Media.Brush)converter.ConvertFromString(stroke);
            TxtHeaderTitle.Text = LocalizationManager.Get(
                category == ReportCategory.Feature ? "Loc_ReportCategoryFeature"
                : category == ReportCategory.Game ? "Loc_ReportCategoryGame"
                : "Loc_ReportCategoryBug");
            LblReportTitle.Text = LocalizationManager.Get(
                category == ReportCategory.Game ? "Loc_ReportTitleLabelGame" : "Loc_BugReportTitleLabel");
            LblReportDescription.Text = LocalizationManager.Get(
                category == ReportCategory.Feature ? "Loc_ReportDescriptionLabelFeature"
                : category == ReportCategory.Game ? "Loc_ReportDescriptionLabelGame"
                : "Loc_BugReportDescriptionLabel");

            var diagnosticsVisibility = IncludesDiagnostics ? Visibility.Visible : Visibility.Collapsed;
            PnlGameInfo.Visibility = category == ReportCategory.Game ? Visibility.Visible : Visibility.Collapsed;
            PnlDiagnosticsCard.Visibility = diagnosticsVisibility;
            PnlConsent.Visibility = diagnosticsVisibility;
            PnlAttachLogs.Visibility = diagnosticsVisibility;
            UpdateSubmitState();
        }

        private void OnRequiredFieldChanged(object sender, TextChangedEventArgs e)
        {
            UpdateSubmitState();
        }

        private void OnConsentChanged(object sender, RoutedEventArgs e)
        {
            UpdateSubmitState();
        }

        private bool IsReportComplete()
        {
            var category = Category;
            bool hasTitle = !string.IsNullOrWhiteSpace(TxtReportTitle.Text);
            bool hasDescription = category == ReportCategory.Game ||
                                  !string.IsNullOrWhiteSpace(TxtReportDescription.Text);
            bool hasConsent = !IncludesDiagnostics || ChkConsent.IsChecked == true;
            return hasTitle && hasDescription && hasConsent;
        }

        private void UpdateSubmitState()
        {
            if (!initialized) return;

            bool complete = IsReportComplete();
            BtnContinueGithub.IsEnabled = complete && !sending;
            BtnSendAnonymous.IsEnabled = complete && !sending;

            string requiredKey = Category == ReportCategory.Feature ? "Loc_ReportRequiredHintFeature"
                : Category == ReportCategory.Game ? "Loc_ReportRequiredHintGame"
                : "Loc_BugReportRequiredHint";
            TxtValidationHint.Text = LocalizationManager.Get(complete ? "Loc_BugReportReadyHint" : requiredKey);
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

        private string BuildIssueTitle()
        {
            string prefix = Category == ReportCategory.Feature ? "[Feature request] "
                : Category == ReportCategory.Game ? "[Unsupported game] "
                : "[Bug] ";
            return prefix + TxtReportTitle.Text.Trim();
        }

        private void OnContinueGithubClick(object sender, RoutedEventArgs e)
        {
            if (BtnContinueGithub.IsEnabled == false) return;

            try
            {
                if (IncludesDiagnostics) EnsureDiagnosticsBuilt();

                string bundlePath = null;
                if (IncludesDiagnostics && ChkAttachLogs.IsChecked == true)
                {
                    bundlePath = Path.Combine(Path.GetTempPath(), DiagnosticReportService.SuggestedBundleName(reportId));
                    DiagnosticReportService.CreateSupportBundle(
                        bundlePath, BuildReportText(false, null), null, true);
                }

                string url = GitHubNewIssueUrl +
                             "?title=" + Uri.EscapeDataString(BuildIssueTitle()) +
                             "&body=" + Uri.EscapeDataString(BuildIssueBody(true, bundlePath));

                CopyAttachmentsToClipboard(bundlePath);

                Process.Start(new ProcessStartInfo(url)
                {
                    UseShellExecute = true
                });

                AppLog.WriteLine(
                    "tray_bug_report.log",
                    "GitHub " + Category + " report draft opened (" + reportId + ", screenshots=" + screenshots.Count +
                    ", logs=" + (bundlePath != null) + ").");
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

        private async void OnSendAnonymouslyClick(object sender, RoutedEventArgs e)
        {
            if (sending || !IsReportComplete()) return;

            if (MessageBox.Show(
                    this,
                    LocalizationManager.Get("Loc_ReportAnonymousConfirm"),
                    LocalizationManager.Get("Loc_BugReportWindowTitle"),
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Question) != MessageBoxResult.Yes) return;

            sending = true;
            UpdateSubmitState();
            TxtValidationHint.Text = LocalizationManager.Get("Loc_ReportSending");

            string category = Category == ReportCategory.Feature ? "feature"
                : Category == ReportCategory.Game ? "game" : "bug";
            string title = TxtReportTitle.Text.Trim();
            string description = TxtReportDescription.Text.Trim();
            string gameInfo = TxtGameInfo.Text.Trim();
            bool diagnostics = IncludesDiagnostics;
            bool withLogs = diagnostics && ChkAttachLogs.IsChecked == true;
            string[] images = screenshots.Select(s => s.FullPath).ToArray();
            string diagnosticText = null;
            string bundlePath = null;

            ReportRelayResult result;
            try
            {
                if (diagnostics)
                {
                    EnsureDiagnosticsBuilt();
                    diagnosticText = diagnosticMarkdown;
                }
                if (withLogs)
                {
                    bundlePath = Path.Combine(Path.GetTempPath(), DiagnosticReportService.SuggestedBundleName(reportId));
                    string text = BuildReportText(false, null);
                    await System.Threading.Tasks.Task.Run(() =>
                        DiagnosticReportService.CreateSupportBundle(bundlePath, text, null, true));
                }

                result = await System.Threading.Tasks.Task.Run(() => ReportRelayClient.Send(
                    ReportRelayClient.DefaultEndpoint, category, title, description, gameInfo,
                    diagnosticText, reportId, bundlePath, images));
            }
            catch (Exception ex)
            {
                result = new ReportRelayResult { Error = ex.GetType().Name };
            }
            finally
            {
                try { if (bundlePath != null && File.Exists(bundlePath)) File.Delete(bundlePath); } catch { }
            }

            sending = false;
            UpdateSubmitState();

            if (result.Success)
            {
                AppLog.WriteLine("tray_bug_report.log", "Anonymous report sent (" + reportId + ", " + category + ", " + result.Url + ").");
                if (result.SkippedAttachments)
                {
                    MessageBox.Show(this, LocalizationManager.Get("Loc_ReportRelaySkipped"),
                        LocalizationManager.Get("Loc_BugReportWindowTitle"), MessageBoxButton.OK, MessageBoxImage.Information);
                }
                try { Process.Start(new ProcessStartInfo(result.Url) { UseShellExecute = true }); } catch { }
                DialogResult = true;
                return;
            }

            AppLog.WriteLine("tray_bug_report.log", "Anonymous report failed (" + reportId + "): " + result.Error);
            if (MessageBox.Show(
                    this,
                    LocalizationManager.Format("Loc_ReportRelayFailed", result.Error),
                    LocalizationManager.Get("Loc_BugReportWindowTitle"),
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) == MessageBoxResult.Yes)
            {
                ExportReportToFile();
            }
        }

        private void ExportReportToFile()
        {
            var dialog = new SaveFileDialog
            {
                Title = LocalizationManager.Get("Loc_BugReportWindowTitle"),
                Filter = "ZIP (*.zip)|*.zip",
                DefaultExt = ".zip",
                FileName = DiagnosticReportService.SuggestedBundleName(reportId)
            };
            if (dialog.ShowDialog(this) != true) return;

            try
            {
                if (IncludesDiagnostics) EnsureDiagnosticsBuilt();
                DiagnosticReportService.CreateSupportBundle(
                    dialog.FileName,
                    BuildReportText(false, null),
                    screenshots.Select(s => s.FullPath).ToArray(),
                    IncludesDiagnostics);

                AppLog.WriteLine(
                    "tray_bug_report.log",
                    "Report exported without GitHub (" + reportId + ", category=" + Category + ").");
                ShowInExplorer(dialog.FileName);
                DialogResult = true;
            }
            catch (Exception ex)
            {
                AppLog.WriteLine("tray_bug_report.log", "Unable to export the report: " + ex.Message);
                MessageBox.Show(
                    LocalizationManager.Format("Loc_ReportExportFailed", ex.Message),
                    LocalizationManager.Get("Loc_BugReportWindowTitle"),
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
            }
        }

        internal static void ShowInExplorer(string path)
        {
            try
            {
                Process.Start(new ProcessStartInfo("explorer.exe", "/select,\"" + path + "\"")
                {
                    UseShellExecute = true
                });
            }
            catch
            {
            }
        }

        private string BuildReportText(bool forGitHub, string bundlePath)
        {
            return "# " + BuildIssueTitle() + Environment.NewLine + Environment.NewLine +
                   BuildIssueBody(forGitHub, bundlePath);
        }

        private string BuildIssueBody(bool forGitHub, string bundlePath)
        {
            var category = Category;
            string description = TxtReportDescription.Text.Trim();
            var body = new StringBuilder(6144);

            if (category == ReportCategory.Game)
            {
                string info = TxtGameInfo.Text.Trim();
                body.AppendLine("## Game");
                body.AppendLine();
                body.AppendLine("- Name: " + TxtReportTitle.Text.Trim());
                if (info.Length > 0) body.AppendLine("- Executable or store link: " + info);
                body.AppendLine();
                if (description.Length > 0)
                {
                    body.AppendLine("## Details");
                    body.AppendLine();
                    body.AppendLine(description);
                    body.AppendLine();
                }
            }
            else
            {
                body.AppendLine(category == ReportCategory.Feature ? "## Request" : "## Description");
                body.AppendLine();
                body.AppendLine(description);
                body.AppendLine();
            }

            if (forGitHub && (screenshots.Count > 0 || bundlePath != null))
            {
                body.AppendLine("## Attachments");
                body.AppendLine();
                body.AppendLine("> " + LocalizationManager.Get("Loc_BugReportGithubPasteHint"));
                foreach (ScreenshotItem screenshot in screenshots)
                    body.AppendLine("- " + screenshot.FileName);
                if (bundlePath != null) body.AppendLine("- " + Path.GetFileName(bundlePath));
                body.AppendLine();
            }

            if (IncludesDiagnostics)
            {
                body.AppendLine("## Automatic installation report");
                body.AppendLine();
                body.AppendLine(diagnosticMarkdown);
            }
            return body.ToString().TrimEnd();
        }

        private void CopyAttachmentsToClipboard(string bundlePath)
        {
            if (screenshots.Count == 0 && bundlePath == null) return;
            try
            {
                var files = new StringCollection();
                foreach (ScreenshotItem screenshot in screenshots)
                {
                    if (File.Exists(screenshot.FullPath)) files.Add(screenshot.FullPath);
                }
                if (bundlePath != null && File.Exists(bundlePath)) files.Add(bundlePath);
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
