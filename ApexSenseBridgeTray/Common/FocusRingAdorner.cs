using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace ApexSenseBridgeTray.Common
{
    // Crisp detached focus ring (replaces blurred glows clipped by scroll viewers).
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
                // A freshly shown tab has no adorner layer until its templates apply.
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

        // Render-transform animations move elements without a layout pass, so rings must be re-placed.
        public static void RefreshAll()
        {
            foreach (var adorner in Active.Values)
            {
                AdornerLayer.GetAdornerLayer(adorner.AdornedElement)?.Update(adorner.AdornedElement);
            }
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

        private static double GetCornerRadius(FrameworkElement element)
        {
            var border = element as Border;
            if (border != null) return border.CornerRadius.TopLeft;
            if (element is Button) return element.ActualHeight / 2.0;
            return 12.0;
        }
    }
}
