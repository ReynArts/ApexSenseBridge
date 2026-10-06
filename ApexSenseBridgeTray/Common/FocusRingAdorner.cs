using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace ApexSenseBridgeTray.Common
{
    /// <summary>
    /// Console focus ring: a crisp white outline drawn a few pixels outside the focused
    /// element (PS5 style). Replaces blurred glows, which smeared over covers and got
    /// clipped by scroll viewers.
    /// </summary>
    internal sealed class FocusRingAdorner : Adorner
    {
        public const double Gap = 5.0;
        private static readonly Pen RingPen = CreatePen();
        private static readonly Dictionary<UIElement, FocusRingAdorner> Active = new Dictionary<UIElement, FocusRingAdorner>();
        private static readonly HashSet<UIElement> Pending = new HashSet<UIElement>();
        private readonly double cornerRadius;

        private FocusRingAdorner(UIElement element, double cornerRadius) : base(element)
        {
            this.cornerRadius = cornerRadius;
            IsHitTestVisible = false;
            // Adorners do not follow their element's visibility on their own (e.g. a hidden tab).
            // Rings are only requested for navigable elements, and IsVisible can still be stale
            // right after a tab switch, so start visible and follow later changes.
            element.IsVisibleChanged += OnAdornedVisibilityChanged;
        }

        private void OnAdornedVisibilityChanged(object sender, DependencyPropertyChangedEventArgs e)
        {
            Visibility = AdornedElement.IsVisible ? Visibility.Visible : Visibility.Collapsed;
        }

        private static Pen CreatePen()
        {
            var pen = new Pen(Brushes.White, 2.5);
            pen.Freeze();
            return pen;
        }

        protected override void OnRender(DrawingContext drawingContext)
        {
            var bounds = new Rect(AdornedElement.RenderSize);
            bounds.Inflate(Gap, Gap);
            double radius = cornerRadius + Gap;
            drawingContext.DrawRoundedRectangle(null, RingPen, bounds, radius, radius);
        }

        /// <summary>Shows the ring around <paramref name="element"/>. No-op outside a live visual tree.</summary>
        public static void Show(FrameworkElement element)
        {
            Show(element, true);
        }

        private static void Show(FrameworkElement element, bool retry)
        {
            if (element == null || Active.ContainsKey(element)) return;
            var layer = AdornerLayer.GetAdornerLayer(element);
            if (layer == null)
            {
                // A tab shown a moment ago has not applied its templates yet: try again after layout.
                if (retry)
                {
                    Pending.Add(element);
                    element.Dispatcher.BeginInvoke(new System.Action(() =>
                    {
                        if (Pending.Remove(element)) Show(element, false);
                    }), System.Windows.Threading.DispatcherPriority.Loaded);
                }
                return;
            }
            var adorner = new FocusRingAdorner(element, GetCornerRadius(element));
            layer.Add(adorner);
            Active[element] = adorner;
        }

        public static void Hide(FrameworkElement element)
        {
            FocusRingAdorner adorner;
            if (element != null) Pending.Remove(element);
            if (element == null || !Active.TryGetValue(element, out adorner)) return;
            Active.Remove(element);
            element.IsVisibleChanged -= adorner.OnAdornedVisibilityChanged;
            var layer = AdornerLayer.GetAdornerLayer(element);
            if (layer != null) layer.Remove(adorner);
        }

        // Follow the element's own rounding so the ring stays concentric (pills use half their height).
        private static double GetCornerRadius(FrameworkElement element)
        {
            var border = element as Border;
            if (border != null) return border.CornerRadius.TopLeft;
            if (element is Button) return element.ActualHeight / 2.0;
            return 12.0;
        }
    }
}
