using System;
using Windows.UI;
using Windows.UI.Text;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Markup;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Shapes;

namespace ApexSenseBridgeWidget
{
    internal static class GlyphFactory
    {
        private static readonly Brush Primary = new SolidColorBrush(Colors.White);
        private static readonly Brush Coral = new SolidColorBrush(Color.FromArgb(0xFF, 0xF4, 0x7F, 0x66));
        private static readonly Brush Muted = new SolidColorBrush(Color.FromArgb(0xFF, 0xA9, 0xAE, 0xB9));

        private static Brush Highlight
        {
            get { return Coral; }
        }

        public static UIElement BuildEntry(RemapEntry entry)
        {
            var row = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                Margin = new Thickness(0, 6, 0, 2)
            };
            if (entry.Hold)
            {
                row.Children.Add(new TextBlock
                {
                    Text = entry.HoldLabel,
                    FontSize = 12,
                    FontWeight = FontWeights.SemiBold,
                    Foreground = Highlight,
                    Margin = new Thickness(0, 0, 8, 0),
                    VerticalAlignment = VerticalAlignment.Center
                });
            }
            for (var i = 0; i < entry.Buttons.Length; i++)
            {
                if (i > 0)
                {
                    row.Children.Add(new TextBlock
                    {
                        Text = "+",
                        FontSize = 12,
                        Foreground = Muted,
                        Margin = new Thickness(5, 0, 5, 0),
                        VerticalAlignment = VerticalAlignment.Center
                    });
                }
                row.Children.Add(Button(entry.Buttons[i]));
            }
            row.Children.Add(new Path
            {
                Data = Geo("M 1,5 L 15,5 M 10,1 L 15,5 L 10,9"),
                Width = 16,
                Height = 10,
                Stroke = Muted,
                StrokeThickness = 1.6,
                StrokeStartLineCap = PenLineCap.Round,
                StrokeEndLineCap = PenLineCap.Round,
                StrokeLineJoin = PenLineJoin.Round,
                Margin = new Thickness(10, 0, 10, 0),
                VerticalAlignment = VerticalAlignment.Center
            });
            row.Children.Add(Touchpad(entry.Output));
            return row;
        }

        private static Geometry Geo(string data)
        {
            return (Geometry)XamlBindingHelper.ConvertValue(typeof(Geometry), data);
        }

        private static UIElement Button(string token)
        {
            switch (token)
            {
                case "View":
                    return Xbox(70, 70, 0.38, canvas =>
                    {
                        canvas.Children.Add(XboxPath("M 35,2.5 A 32.5,32.5 0 1 1 34.99,2.5 Z", 4.2));
                        canvas.Children.Add(XboxPath("M 31.25,32.5 L 51.25,32.5 L 51.25,47.5 L 31.25,47.5 Z", 3.2));
                        canvas.Children.Add(XboxPath("m38.75 25v-5h-20v15h5", 3.2));
                    });
                case "Menu":
                    return Xbox(70, 70, 0.38, canvas =>
                    {
                        canvas.Children.Add(XboxPath("M 35,2.5 A 32.5,32.5 0 1 1 34.99,2.5 Z", 4.2));
                        canvas.Children.Add(XboxPath("m17.5 35h35 M17.5 25h35 M17.5 45h35", 3.2));
                    });
                case "DpadUp":
                    return Dpad("M 9,3 L 15,3 L 15,9 L 9,9 Z");
                case "DpadRight":
                    return Dpad("M 15,9 L 21,9 L 21,15 L 15,15 Z");
                case "LB":
                    return Bumper(true);
                case "RB":
                    return Bumper(false);
                case "RS":
                    return Stick();
                case "ABXY":
                    var faces = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
                    faces.Children.Add(Face("A", 0x34, 0xD3, 0x99));
                    faces.Children.Add(Face("B", 0xF8, 0x71, 0x71));
                    faces.Children.Add(Face("X", 0x60, 0xA5, 0xFA));
                    faces.Children.Add(Face("Y", 0xFB, 0xBF, 0x24));
                    return faces;
                default:
                    return new TextBlock
                    {
                        Text = token,
                        FontSize = 12,
                        FontWeight = FontWeights.SemiBold,
                        Foreground = Primary,
                        VerticalAlignment = VerticalAlignment.Center
                    };
            }
        }

        private static UIElement Xbox(double width, double height, double scale, Action<Canvas> draw)
        {
            var canvas = new Canvas { Width = width, Height = height };
            draw(canvas);
            return new Viewbox
            {
                Width = width * scale,
                Height = height * scale,
                Child = canvas,
                Margin = new Thickness(1, 0, 1, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
        }

        private static Path XboxPath(string data, double thickness)
        {
            return new Path
            {
                Data = Geo(data),
                Stroke = Primary,
                StrokeThickness = thickness,
                StrokeStartLineCap = PenLineCap.Flat,
                StrokeEndLineCap = PenLineCap.Flat,
                StrokeLineJoin = PenLineJoin.Miter
            };
        }

        private static Path GlyphPath(string data, bool filled, Brush brush, double thickness)
        {
            var path = new Path
            {
                Data = Geo(data),
                Stroke = brush,
                StrokeThickness = thickness,
                StrokeStartLineCap = PenLineCap.Round,
                StrokeEndLineCap = PenLineCap.Round,
                StrokeLineJoin = PenLineJoin.Round
            };
            if (filled) path.Fill = brush;
            return path;
        }

        private static UIElement Bumper(bool left)
        {
            var body = left
                ? "m8.4021 50s-2.3172 0.000001 -3.815-1.768-1.117-4.0537-1.117-4.0537l2.9524-17.711s0.49076-2.9453 2.516-5.1393 4.922-2.9184 4.922-2.9184l37.625-9.0888s2.733-0.74932 5.6239 0.22637c2.8908 0.97568 4.6108 3.2279 4.6108 3.2279l2.9909 3.7386s1.3081 1.6351 1.8684 3.7263c0.56033 2.0912 0.09896 4.6922 0.09896 4.6922l-3.482 20.89s-0.3013 1.8083-1.7 2.9932c-1.3988 1.185-3.232 1.185-3.232 1.185z"
                : "m61.598 50s2.3172 0.000001 3.815-1.768 1.117-4.0537 1.117-4.0537l-2.9524-17.711s-0.49076-2.9453-2.516-5.1393-4.922-2.9184-4.922-2.9184l-37.625-9.0888s-2.733-0.74932-5.6239 0.22637c-2.8908 0.97568-4.6108 3.2279-4.6108 3.2279l-2.9909 3.7386s-1.3081 1.6351-1.8684 3.7263-0.098956 4.6922-0.098956 4.6922l3.482 20.89s0.3013 1.8083 1.7001 2.9932c1.3987 1.185 3.232 1.185 3.232 1.185z";
            return Xbox(70, 52, 0.52, canvas =>
            {
                canvas.Children.Add(XboxPath(body, 3));
                var label = new TextBlock
                {
                    Text = left ? "LB" : "RB",
                    Width = 70,
                    TextAlignment = TextAlignment.Center,
                    FontSize = 21,
                    LineHeight = 21,
                    LineStackingStrategy = LineStackingStrategy.BlockLineHeight,
                    FontWeight = FontWeights.Bold,
                    Foreground = Primary
                };
                Canvas.SetTop(label, 23);
                canvas.Children.Add(label);
            });
        }

        private static UIElement Stick()
        {
            var canvas = new Canvas { Width = 36, Height = 36 };
            var cap = new Border
            {
                Width = 20,
                Height = 20,
                CornerRadius = new CornerRadius(10),
                Background = Highlight,
                Child = new TextBlock
                {
                    Text = "R",
                    FontSize = 12,
                    LineHeight = 12,
                    LineStackingStrategy = LineStackingStrategy.BlockLineHeight,
                    TextAlignment = TextAlignment.Center,
                    FontWeight = FontWeights.Bold,
                    Foreground = new SolidColorBrush(Color.FromArgb(0xFF, 0x0F, 0x13, 0x18)),
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };
            Canvas.SetLeft(cap, 8);
            Canvas.SetTop(cap, 8);
            canvas.Children.Add(cap);
            foreach (var arrow in new[]
            {
                "M 18,0.5 L 22,5.5 L 14,5.5 Z",
                "M 18,35.5 L 22,30.5 L 14,30.5 Z",
                "M 0.5,18 L 5.5,14 L 5.5,22 Z",
                "M 35.5,18 L 30.5,14 L 30.5,22 Z"
            })
            {
                canvas.Children.Add(GlyphPath(arrow, true, Primary, 1.6));
            }
            return new Viewbox
            {
                Width = 30,
                Height = 30,
                Child = canvas,
                Margin = new Thickness(2, 0, 2, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
        }

        private static UIElement Dpad(string pressedArm)
        {
            var canvas = new Canvas { Width = 24, Height = 24 };
            canvas.Children.Add(GlyphPath("M 9,3 L 15,3 L 15,9 L 21,9 L 21,15 L 15,15 L 15,21 L 9,21 L 9,15 L 3,15 L 3,9 L 9,9 Z", false, Primary, 1.4));
            canvas.Children.Add(GlyphPath(pressedArm, true, Highlight, 1.4));
            return new Viewbox
            {
                Width = 28,
                Height = 28,
                Child = canvas,
                Margin = new Thickness(2, 0, 2, 0),
                VerticalAlignment = VerticalAlignment.Center
            };
        }

        private static UIElement Face(string letter, byte r, byte g, byte b)
        {
            var color = Color.FromArgb(0xFF, r, g, b);
            return new Border
            {
                Width = 26,
                Height = 26,
                CornerRadius = new CornerRadius(13),
                BorderThickness = new Thickness(1.6),
                Background = new SolidColorBrush(Color.FromArgb(0x1F, r, g, b)),
                BorderBrush = new SolidColorBrush(color),
                Margin = new Thickness(2, 0, 2, 0),
                VerticalAlignment = VerticalAlignment.Center,
                Child = new TextBlock
                {
                    Text = letter,
                    FontSize = 12,
                    LineHeight = 12,
                    LineStackingStrategy = LineStackingStrategy.BlockLineHeight,
                    TextAlignment = TextAlignment.Center,
                    FontWeight = FontWeights.Bold,
                    Foreground = new SolidColorBrush(color),
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };
        }

        private static UIElement Touchpad(string output)
        {
            const string leftHalf = "M 5,1 L 22,1 L 22,25 L 8,25 Q 3.5,25 3,20.5 L 1.2,5 Q 1,1 5,1 Z";
            const string rightHalf = "M 22,1 L 39,1 Q 43,1 42.8,5 L 41,20.5 Q 40.5,25 36,25 L 22,25 Z";
            const string frame = "M 5,1 L 39,1 Q 43,1 42.8,5 L 41,20.5 Q 40.5,25 36,25 L 8,25 Q 3.5,25 3,20.5 L 1.2,5 Q 1,1 5,1 Z";
            var highlight = Highlight;
            var pad = new Grid { Width = 44, Height = 26, VerticalAlignment = VerticalAlignment.Center };

            if (output == "ClickLeft" || output == "ClickRight")
            {
                var half = new Path
                {
                    Data = Geo(output == "ClickLeft" ? leftHalf : rightHalf),
                    Fill = highlight,
                    Opacity = 0.85
                };
                pad.Children.Add(half);
            }
            pad.Children.Add(new Path
            {
                Data = Geo(frame),
                Stroke = Muted,
                StrokeThickness = 1.6,
                StrokeLineJoin = PenLineJoin.Round
            });
            pad.Children.Add(new Border
            {
                Width = 1,
                Margin = new Thickness(0, 5, 0, 5),
                HorizontalAlignment = HorizontalAlignment.Center,
                Opacity = 0.5,
                Background = Muted
            });

            string geometry = null;
            switch (output)
            {
                case "Click":
                    pad.Children.Add(new Border
                    {
                        Width = 9,
                        Height = 9,
                        CornerRadius = new CornerRadius(5),
                        HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center,
                        Background = highlight
                    });
                    break;
                case "SwipeLeft":
                    geometry = "M 34,13 L 10,13 M 16,7 L 10,13 L 16,19";
                    break;
                case "SwipeUpDown":
                    geometry = "M 22,4 L 22,22 M 17,9 L 22,4 L 27,9 M 17,17 L 22,22 L 27,17";
                    break;
                case "SwipeFlick":
                    geometry = "M 14,20 L 30,6 M 22,6 L 30,6 L 30,14";
                    break;
                case "SwipeFour":
                    geometry = "M 22,4 L 22,22 M 18,8 L 22,4 L 26,8 M 18,18 L 22,22 L 26,18 " +
                               "M 9,13 L 35,13 M 13,9 L 9,13 L 13,17 M 31,9 L 35,13 L 31,17";
                    break;
            }
            if (geometry != null)
            {
                var swipe = GlyphPath(geometry, false, highlight, 1.6);
                swipe.Width = 44;
                swipe.Height = 26;
                pad.Children.Add(swipe);
            }
            return pad;
        }
    }
}
