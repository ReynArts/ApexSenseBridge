using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace ApexSenseBridgeTray
{
    public partial class LearnedExecutablesWindow : Window
    {
        private readonly ExecutableLearningService learningService;
        private readonly Action bindingsChangedHandler;

        public LearnedExecutablesWindow(ExecutableLearningService learningService)
        {
            this.learningService = learningService;
            InitializeComponent();

            bindingsChangedHandler = () => Dispatcher.BeginInvoke(new Action(LoadBindings));
            if (learningService != null)
            {
                learningService.BindingsChanged += bindingsChangedHandler;
            }
            LoadBindings();
        }

        protected override void OnClosed(EventArgs e)
        {
            if (learningService != null)
            {
                learningService.BindingsChanged -= bindingsChangedHandler;
            }
            base.OnClosed(e);
        }

        private void LoadBindings()
        {
            var rows = learningService == null
                ? new LearnedExecutableRow[0]
                : learningService.GetBindings().Select(x => new LearnedExecutableRow(x)).ToArray();

            GridBindings.ItemsSource = rows;
            TxtEmpty.Visibility = rows.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
            TxtCount.Text = LocalizationManager.Format(
                rows.Length == 1 ? "Loc_LearnedCountSingular" : "Loc_LearnedCountPlural",
                rows.Length);
            UpdateSelectionButtons();
        }

        private IReadOnlyList<LearnedExecutableRow> GetSelectedRows()
        {
            return GridBindings.SelectedItems.Cast<object>()
                .OfType<LearnedExecutableRow>()
                .ToArray();
        }

        private void OnSelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            UpdateSelectionButtons();
        }

        private void UpdateSelectionButtons()
        {
            bool hasSelection = GridBindings.SelectedItems.Count > 0;
            BtnDelete.IsEnabled = hasSelection;
            BtnExport.IsEnabled = hasSelection;
        }

        private void OnSelectAllClick(object sender, RoutedEventArgs e)
        {
            GridBindings.SelectAll();
        }

        private void OnDeleteClick(object sender, RoutedEventArgs e)
        {
            var selected = GetSelectedRows();
            if (selected.Count == 0 || learningService == null) return;

            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", selected.Count),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);
            if (result != MessageBoxResult.Yes) return;

            learningService.DeleteBindings(selected.Select(x => x.Binding.Path));
            LoadBindings();
        }

        private async void OnExportClick(object sender, RoutedEventArgs e)
        {
            var selected = GetSelectedRows();
            if (selected.Count == 0 || learningService == null) return;

            var dialog = new SaveFileDialog
            {
                Title = LocalizationManager.Get("Loc_BtnExportLearned"),
                FileName = "apexsensebridge-learned-executables.json",
                DefaultExt = ".json",
                Filter = "JSON (*.json)|*.json"
            };
            if (dialog.ShowDialog(this) != true) return;

            BtnExport.IsEnabled = false;
            string error = null;
            bool success = await Task.Run(() => learningService.ExportBindings(
                selected.Select(x => x.Binding).ToArray(), dialog.FileName, out error));
            UpdateSelectionButtons();

            MessageBox.Show(
                this,
                success
                    ? LocalizationManager.Get("Loc_LearnedExportSuccess")
                    : LocalizationManager.Format("Loc_LearnedExportFailed", error ?? string.Empty),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.OK,
                success ? MessageBoxImage.Information : MessageBoxImage.Error);
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }
    }

    public sealed class LearnedExecutableRow
    {
        public LearnedExecutableBinding Binding { get; private set; }
        public string GameTitle { get; private set; }
        public string Executable { get; private set; }
        public string DetectionMethod { get; private set; }
        public int SuccessfulSessions { get; private set; }
        public string LastSeenDisplay { get; private set; }

        public LearnedExecutableRow(LearnedExecutableBinding binding)
        {
            Binding = binding;
            GameTitle = !string.IsNullOrWhiteSpace(binding.GameTitle)
                ? binding.GameTitle
                : binding.GameNormalized;
            Executable = binding.Executable;
            DetectionMethod = binding.DetectionMethod;
            SuccessfulSessions = binding.SuccessfulSessions;

            DateTime lastSeen;
            LastSeenDisplay = DateTime.TryParse(
                binding.LastSeenUtc,
                CultureInfo.InvariantCulture,
                DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal,
                out lastSeen)
                ? lastSeen.ToLocalTime().ToString("g", CultureInfo.CurrentCulture)
                : binding.LastSeenUtc;
        }
    }
}
