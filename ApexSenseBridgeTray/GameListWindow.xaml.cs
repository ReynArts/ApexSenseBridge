using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace ApexSenseBridgeTray
{
    public partial class GameListWindow : Window
    {
        private readonly CloudGameListService gameListService;
        private readonly TraySettings settings;
        private readonly ExecutableLearningService learningService;
        private readonly Action bindingsChangedHandler;

        private readonly List<GameItemViewModel> allGameViewModels = new List<GameItemViewModel>();
        private readonly List<LearnedItemViewModel> allLearnedViewModels = new List<LearnedItemViewModel>();

        public GameListWindow(
            CloudGameListService gameListService,
            TraySettings settings,
            ExecutableLearningService learningService = null,
            string initialTab = "games")
        {
            this.gameListService = gameListService;
            this.settings = settings;
            this.learningService = learningService;

            InitializeComponent();

            bindingsChangedHandler = () => Dispatcher.BeginInvoke(new Action(LoadLearnedItems));
            if (learningService != null)
            {
                learningService.BindingsChanged += bindingsChangedHandler;
            }

            LoadGames();
            LoadLearnedItems();

            if (string.Equals(initialTab, "learned", StringComparison.OrdinalIgnoreCase))
            {
                NavTabLearned.IsChecked = true;
            }
            else
            {
                NavTabCertified.IsChecked = true;
            }

            LocalizationManager.LanguageChanged += () =>
            {
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    try
                    {
                        ApplyFilter();
                        ApplyLearnedFilter();
                        UpdateTabTitles();
                    }
                    catch { }
                }));
            };
        }

        protected override void OnClosed(EventArgs e)
        {
            if (learningService != null)
            {
                learningService.BindingsChanged -= bindingsChangedHandler;
            }
            base.OnClosed(e);
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
            {
                DragMove();
            }
        }

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        #region Segmented Navigation Switcher

        private void OnNavTabChanged(object sender, RoutedEventArgs e)
        {
            if (!IsLoaded) return;
            UpdateActiveTab();
        }

        private void UpdateActiveTab()
        {
            bool isLearned = NavTabLearned.IsChecked == true;
            PanelCertifiedGames.Visibility = isLearned ? Visibility.Collapsed : Visibility.Visible;
            PanelLearnedExecutables.Visibility = isLearned ? Visibility.Visible : Visibility.Collapsed;

            TxtHeaderSubtitle.Text = isLearned
                ? LocalizationManager.Get("Loc_LearnedSubtitle")
                : LocalizationManager.Get("Loc_GameListSubtitle");
        }

        private void UpdateTabTitles()
        {
            UpdateActiveTab();
            TxtNavCertifiedCount.Text = allGameViewModels.Count.ToString();
            TxtNavLearnedCount.Text = allLearnedViewModels.Count.ToString();
        }

        #endregion

        #region Certified Games Logic

        private void LoadGames()
        {
            allGameViewModels.Clear();
            var rawGames = gameListService != null ? gameListService.GetAllGames() : new SupportedGame[0];

            foreach (var g in rawGames.OrderBy(x => x.Title))
            {
                var isExcluded = settings.IsGameExcluded(g.Normalized) || settings.IsGameExcluded(g.Title);
                allGameViewModels.Add(new GameItemViewModel
                {
                    Game = g,
                    IsExcluded = isExcluded
                });
            }

            TxtNavCertifiedCount.Text = allGameViewModels.Count.ToString();
            ApplyFilter();
        }

        private void ApplyFilter()
        {
            string search = (TxtSearch.Text ?? string.Empty).Trim().ToLowerInvariant();
            bool filterAdaptive = RadAdaptive.IsChecked == true;
            bool filterHaptic = RadHaptic.IsChecked == true;
            bool filterExcluded = RadExcluded.IsChecked == true;

            var filtered = new List<GameItemViewModel>();

            foreach (var item in allGameViewModels)
            {
                if (!string.IsNullOrEmpty(search))
                {
                    if (!item.Title.ToLowerInvariant().Contains(search) &&
                        !item.Profile.ToLowerInvariant().Contains(search))
                    {
                        continue;
                    }
                }

                if (filterAdaptive && !item.AdaptiveTriggers) continue;
                if (filterHaptic && !item.HapticFeedback) continue;
                if (filterExcluded && !item.IsExcluded) continue;

                filtered.Add(item);
            }

            LstGames.ItemsSource = filtered;

            int excludedCount = 0;
            foreach (var x in allGameViewModels)
            {
                if (x.IsExcluded) excludedCount++;
            }

            int total = filtered.Count;
            string displayedStr = LocalizationManager.Format(
                total > 1 ? "Loc_GamesDisplayedPlural" : "Loc_GamesDisplayedSingular",
                total);

            if (excludedCount > 0)
            {
                string excludedStr = LocalizationManager.Format(
                    excludedCount > 1 ? "Loc_GamesExcludedPlural" : "Loc_GamesExcludedSingular",
                    excludedCount);
                TxtStats.Text = string.Format("{0} • {1}", displayedStr, excludedStr);
            }
            else
            {
                TxtStats.Text = displayedStr;
            }
        }

        private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
        {
            bool hasText = !string.IsNullOrEmpty(TxtSearch.Text);
            TxtSearchPlaceholder.Visibility = hasText ? Visibility.Collapsed : Visibility.Visible;
            BtnClearSearch.Visibility = hasText ? Visibility.Visible : Visibility.Collapsed;
            ApplyFilter();
        }

        private void OnClearSearchClick(object sender, RoutedEventArgs e)
        {
            TxtSearch.Text = string.Empty;
        }

        private void OnFilterTabChanged(object sender, RoutedEventArgs e)
        {
            if (!IsLoaded) return;
            ApplyFilter();
        }

        private void OnGameExcludedToggled(object sender, RoutedEventArgs e)
        {
            FrameworkElement elem = sender as FrameworkElement;
            if (elem != null)
            {
                GameItemViewModel item = elem.DataContext as GameItemViewModel;
                if (item != null)
                {
                    settings.SetGameExcluded(item.Normalized, item.IsExcluded);
                    settings.Save();
                    ApplyFilter();
                }
            }
        }

        #endregion

        #region Learned Executables Logic

        private void LoadLearnedItems()
        {
            allLearnedViewModels.Clear();

            if (learningService != null)
            {
                var bindings = learningService.GetBindings();
                foreach (var b in bindings.OrderByDescending(x => x.SuccessfulSessions).ThenBy(x => x.GameTitle))
                {
                    var vm = new LearnedItemViewModel(b);
                    vm.PropertyChanged += OnLearnedItemPropertyChanged;
                    allLearnedViewModels.Add(vm);
                }
            }

            TxtNavLearnedCount.Text = allLearnedViewModels.Count.ToString();
            ApplyLearnedFilter();
            UpdateLearnedButtons();
        }

        private void OnLearnedItemPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == "IsSelected")
            {
                UpdateLearnedButtons();
            }
        }

        private void ApplyLearnedFilter()
        {
            string search = (TxtSearchLearned.Text ?? string.Empty).Trim().ToLowerInvariant();
            var filtered = new List<LearnedItemViewModel>();

            foreach (var item in allLearnedViewModels)
            {
                if (!string.IsNullOrEmpty(search))
                {
                    bool matchTitle = !string.IsNullOrEmpty(item.GameTitle) && item.GameTitle.ToLowerInvariant().Contains(search);
                    bool matchExe = !string.IsNullOrEmpty(item.Executable) && item.Executable.ToLowerInvariant().Contains(search);
                    bool matchPath = !string.IsNullOrEmpty(item.Path) && item.Path.ToLowerInvariant().Contains(search);
                    if (!matchTitle && !matchExe && !matchPath) continue;
                }

                filtered.Add(item);
            }

            LstLearned.ItemsSource = filtered;
            PnlLearnedEmpty.Visibility = allLearnedViewModels.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

            int count = filtered.Count;
            TxtLearnedStats.Text = LocalizationManager.Format(
                count > 1 ? "Loc_LearnedCountPlural" : "Loc_LearnedCountSingular",
                count);

            UpdateLearnedButtons();
        }

        private void OnSearchLearnedTextChanged(object sender, TextChangedEventArgs e)
        {
            bool hasText = !string.IsNullOrEmpty(TxtSearchLearned.Text);
            TxtSearchLearnedPlaceholder.Visibility = hasText ? Visibility.Collapsed : Visibility.Visible;
            BtnClearSearchLearned.Visibility = hasText ? Visibility.Visible : Visibility.Collapsed;
            ApplyLearnedFilter();
        }

        private void OnClearSearchLearnedClick(object sender, RoutedEventArgs e)
        {
            TxtSearchLearned.Text = string.Empty;
        }

        private void UpdateLearnedButtons()
        {
            int selectedCount = allLearnedViewModels.Count(x => x.IsSelected);
            bool hasSelection = selectedCount > 0;
            BtnDeleteLearned.IsEnabled = hasSelection;
            BtnExportLearned.IsEnabled = hasSelection || allLearnedViewModels.Count > 0;
        }

        private void OnSelectAllLearnedClick(object sender, RoutedEventArgs e)
        {
            bool allSelected = allLearnedViewModels.Count > 0 && allLearnedViewModels.All(x => x.IsSelected);
            foreach (var item in allLearnedViewModels)
            {
                item.IsSelected = !allSelected;
            }
            UpdateLearnedButtons();
        }

        private void OnDeleteLearnedClick(object sender, RoutedEventArgs e)
        {
            var selected = allLearnedViewModels.Where(x => x.IsSelected).ToArray();
            if (selected.Length == 0 || learningService == null) return;

            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", selected.Length),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);

            if (result != MessageBoxResult.Yes) return;

            learningService.DeleteBindings(selected.Select(x => x.Path));
            LoadLearnedItems();
        }

        private void OnDeleteSingleLearnedItemClick(object sender, RoutedEventArgs e)
        {
            var btn = sender as FrameworkElement;
            var item = btn != null ? btn.DataContext as LearnedItemViewModel : null;
            if (item == null || learningService == null) return;

            var result = MessageBox.Show(
                this,
                LocalizationManager.Format("Loc_LearnedDeleteConfirm", 1),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);

            if (result != MessageBoxResult.Yes) return;

            learningService.DeleteBindings(new[] { item.Path });
            LoadLearnedItems();
        }

        private async void OnExportLearnedClick(object sender, RoutedEventArgs e)
        {
            if (learningService == null) return;

            var selected = allLearnedViewModels.Where(x => x.IsSelected).Select(x => x.Binding).ToArray();
            if (selected.Length == 0)
            {
                selected = allLearnedViewModels.Select(x => x.Binding).ToArray();
            }
            if (selected.Length == 0) return;

            var dialog = new SaveFileDialog
            {
                Title = LocalizationManager.Get("Loc_BtnExportLearned"),
                FileName = "apexsensebridge-learned-executables.json",
                DefaultExt = ".json",
                Filter = "JSON (*.json)|*.json"
            };

            if (dialog.ShowDialog(this) != true) return;

            BtnExportLearned.IsEnabled = false;
            string error = null;
            bool success = await Task.Run(() => learningService.ExportBindings(selected, dialog.FileName, out error));
            UpdateLearnedButtons();

            MessageBox.Show(
                this,
                success
                    ? LocalizationManager.Get("Loc_LearnedExportSuccess")
                    : LocalizationManager.Format("Loc_LearnedExportFailed", error ?? string.Empty),
                LocalizationManager.Get("Loc_LearnedWindowTitle"),
                MessageBoxButton.OK,
                success ? MessageBoxImage.Information : MessageBoxImage.Error);
        }

        #endregion
    }

    public sealed class LearnedItemViewModel : INotifyPropertyChanged
    {
        public LearnedExecutableBinding Binding { get; private set; }
        public string GameTitle => !string.IsNullOrWhiteSpace(Binding.GameTitle) ? Binding.GameTitle : Binding.GameNormalized;
        public string Executable => Binding.Executable;
        public string Path => Binding.Path;
        public string DetectionMethod => !string.IsNullOrWhiteSpace(Binding.DetectionMethod) ? Binding.DetectionMethod : "Automatique";
        public string SessionsDisplay => string.Format("{0} session{1}", Binding.SuccessfulSessions, Binding.SuccessfulSessions > 1 ? "s" : "");
        public string LastSeenDisplay { get; private set; }

        private bool isSelected;
        public bool IsSelected
        {
            get => isSelected;
            set
            {
                if (isSelected != value)
                {
                    isSelected = value;
                    OnPropertyChanged("IsSelected");
                }
            }
        }

        public LearnedItemViewModel(LearnedExecutableBinding binding)
        {
            Binding = binding;

            DateTime lastSeen;
            LastSeenDisplay = DateTime.TryParse(
                binding.LastSeenUtc,
                CultureInfo.InvariantCulture,
                DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal,
                out lastSeen)
                ? lastSeen.ToLocalTime().ToString("g", CultureInfo.CurrentCulture)
                : binding.LastSeenUtc;
        }

        public event PropertyChangedEventHandler PropertyChanged;
        private void OnPropertyChanged(string prop) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(prop));
    }

    public class GameItemViewModel : INotifyPropertyChanged
    {
        private SupportedGame game;
        public SupportedGame Game
        {
            get => game;
            set
            {
                game = value;
                InitCover();
            }
        }

        public string Title => Game != null ? Game.Title : string.Empty;
        public string Normalized => Game != null ? Game.Normalized : string.Empty;
        public bool AdaptiveTriggers => Game != null && Game.AdaptiveTriggers;
        public bool HapticFeedback => Game != null && Game.HapticFeedback;
        public string Profile => Game != null ? Game.Profile : "standard";
        public string IconUrl => Game != null ? Game.IconUrl : string.Empty;

        private ImageSource coverImage;
        public ImageSource CoverImage
        {
            get => coverImage;
            private set
            {
                if (coverImage != value)
                {
                    coverImage = value;
                    OnPropertyChanged("CoverImage");
                    OnPropertyChanged("HasCoverImage");
                    OnPropertyChanged("IconVisibility");
                    OnPropertyChanged("PlaceholderVisibility");
                }
            }
        }

        public bool HasCoverImage => CoverImage != null;
        public Visibility IconVisibility => HasCoverImage ? Visibility.Visible : Visibility.Collapsed;
        public Visibility PlaceholderVisibility => HasCoverImage ? Visibility.Collapsed : Visibility.Visible;

        public string Initials
        {
            get
            {
                if (string.IsNullOrWhiteSpace(Title)) return "?";
                var parts = Title.Split(new[] { ' ', ':', '-', '\'', '’' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 2)
                    return (parts[0].Substring(0, 1) + parts[1].Substring(0, 1)).ToUpperInvariant();
                return Title.Length >= 2 ? Title.Substring(0, 2).ToUpperInvariant() : Title.ToUpperInvariant();
            }
        }

        public Brush MonogramBackground
        {
            get
            {
                int hash = Math.Abs((Title ?? "game").GetHashCode());
                var palette = new[]
                {
                    new SolidColorBrush(Color.FromRgb(26, 47, 85)),   // PlayStation Navy
                    new SolidColorBrush(Color.FromRgb(20, 60, 50)),   // Emerald slate
                    new SolidColorBrush(Color.FromRgb(60, 25, 65)),   // Royal amethyst
                    new SolidColorBrush(Color.FromRgb(70, 30, 30)),   // Deep crimson
                    new SolidColorBrush(Color.FromRgb(24, 52, 70)),   // Steel teal
                    new SolidColorBrush(Color.FromRgb(40, 40, 55))    // Dark slate
                };
                var brush = palette[hash % palette.Length];
                brush.Freeze();
                return brush;
            }
        }

        private void InitCover()
        {
            if (string.IsNullOrWhiteSpace(IconUrl)) return;
            CoverImage = CoverCacheService.GetImage(IconUrl, loadedImage =>
            {
                CoverImage = loadedImage;
            });
        }

        public bool HasCustomRemapping
        {
            get
            {
                if (Game == null || string.IsNullOrWhiteSpace(Game.Profile)) return false;
                string p = Game.Profile.ToLowerInvariant();
                return p != "standard" && p != "none" && p != "default";
            }
        }

        public Visibility AdaptiveVisibility => AdaptiveTriggers ? Visibility.Visible : Visibility.Collapsed;
        public Visibility HapticVisibility => HapticFeedback ? Visibility.Visible : Visibility.Collapsed;
        public Visibility RemappingVisibility => HasCustomRemapping ? Visibility.Visible : Visibility.Collapsed;

        private bool isExcluded;
        public bool IsExcluded
        {
            get => isExcluded;
            set
            {
                if (isExcluded != value)
                {
                    isExcluded = value;
                    OnPropertyChanged("IsExcluded");
                    OnPropertyChanged("CardOpacity");
                }
            }
        }

        public double CardOpacity => IsExcluded ? 0.6 : 1.0;

        public event PropertyChangedEventHandler PropertyChanged;
        protected void OnPropertyChanged(string name)
        {
            var handler = PropertyChanged;
            if (handler != null)
            {
                handler(this, new PropertyChangedEventArgs(name));
            }
        }
    }
}
