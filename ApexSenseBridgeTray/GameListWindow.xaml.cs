using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace ApexSenseBridgeTray
{
    public partial class GameListWindow : Window
    {
        private readonly CloudGameListService gameListService;
        private readonly TraySettings settings;
        private readonly List<GameItemViewModel> allGameViewModels = new List<GameItemViewModel>();

        public GameListWindow(CloudGameListService gameListService, TraySettings settings)
        {
            this.gameListService = gameListService;
            this.settings = settings;

            InitializeComponent();
            LoadGames();
        }

        private void LoadGames()
        {
            allGameViewModels.Clear();
            var rawGames = gameListService.GetAllGames();

            foreach (var g in rawGames.OrderBy(x => x.Title))
            {
                var isExcluded = settings.IsGameExcluded(g.Normalized) || settings.IsGameExcluded(g.Title);
                allGameViewModels.Add(new GameItemViewModel
                {
                    Game = g,
                    IsExcluded = isExcluded
                });
            }

            ApplyFilter();
        }

        private void ApplyFilter()
        {
            string search = (TxtSearch.Text ?? string.Empty).Trim().ToLowerInvariant();
            bool filterAdaptive = RadAdaptive.IsChecked == true;
            bool filterHaptic = RadHaptic.IsChecked == true;
            bool filterExcluded = RadExcluded.IsChecked == true;

            List<GameItemViewModel> filtered = new List<GameItemViewModel>();

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
            string displayedStr = total > 1
                ? string.Format("{0} jeux affichés", total)
                : string.Format("{0} jeu affiché", total);

            if (excludedCount > 0)
            {
                string excludedStr = excludedCount > 1
                    ? string.Format("{0} exclus", excludedCount)
                    : string.Format("{0} exclu", excludedCount);
                TxtStats.Text = string.Format("{0} • {1}", displayedStr, excludedStr);
            }
            else
            {
                TxtStats.Text = displayedStr;
            }
        }

        private void OnWindowDrag(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
            {
                DragMove();
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

        private void OnCloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }
    }

    public class GameItemViewModel : INotifyPropertyChanged
    {
        public SupportedGame Game { get; set; }

        public string Title
        {
            get { return Game != null ? Game.Title : string.Empty; }
        }

        public string Normalized
        {
            get { return Game != null ? Game.Normalized : string.Empty; }
        }

        public bool AdaptiveTriggers
        {
            get { return Game != null && Game.AdaptiveTriggers; }
        }

        public bool HapticFeedback
        {
            get { return Game != null && Game.HapticFeedback; }
        }

        public string Profile
        {
            get { return Game != null ? Game.Profile : "standard"; }
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

        public Visibility AdaptiveVisibility
        {
            get { return AdaptiveTriggers ? Visibility.Visible : Visibility.Collapsed; }
        }

        public Visibility HapticVisibility
        {
            get { return HapticFeedback ? Visibility.Visible : Visibility.Collapsed; }
        }

        public Visibility RemappingVisibility
        {
            get { return HasCustomRemapping ? Visibility.Visible : Visibility.Collapsed; }
        }

        private bool isExcluded;
        public bool IsExcluded
        {
            get { return isExcluded; }
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

        public double CardOpacity
        {
            get { return IsExcluded ? 0.6 : 1.0; }
        }

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
