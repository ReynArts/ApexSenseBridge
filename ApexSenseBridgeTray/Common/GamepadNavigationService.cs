using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Threading;

namespace ApexSenseBridgeTray.Common
{
    public enum GamepadButtonAction
    {
        Select, // A / Cross
        Back,   // B / Circle
        ActionX,// X / Square
        ActionY,// Y / Triangle
        Menu,   // Start / Options
        View    // Back / Share
    }

    public class GamepadNavigationService : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct XINPUT_GAMEPAD
        {
            public ushort wButtons;
            public byte bLeftTrigger;
            public byte bRightTrigger;
            public short sThumbLX;
            public short sThumbLY;
            public short sThumbRX;
            public short sThumbRY;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct XINPUT_STATE
        {
            public uint dwPacketNumber;
            public XINPUT_GAMEPAD Gamepad;
        }

        private const ushort XINPUT_GAMEPAD_DPAD_UP        = 0x0001;
        private const ushort XINPUT_GAMEPAD_DPAD_DOWN      = 0x0002;
        private const ushort XINPUT_GAMEPAD_DPAD_LEFT      = 0x0004;
        private const ushort XINPUT_GAMEPAD_DPAD_RIGHT     = 0x0008;
        private const ushort XINPUT_GAMEPAD_START          = 0x0010;
        private const ushort XINPUT_GAMEPAD_BACK           = 0x0020;
        private const ushort XINPUT_GAMEPAD_LEFT_THUMB     = 0x0040;
        private const ushort XINPUT_GAMEPAD_RIGHT_THUMB    = 0x0080;
        private const ushort XINPUT_GAMEPAD_LEFT_SHOULDER  = 0x0100;
        private const ushort XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200;
        private const ushort XINPUT_GAMEPAD_A              = 0x1000;
        private const ushort XINPUT_GAMEPAD_B              = 0x2000;
        private const ushort XINPUT_GAMEPAD_X              = 0x4000;
        private const ushort XINPUT_GAMEPAD_Y              = 0x8000;

        private const int LEFT_STICK_DEADZONE = 8500;
        private const int LEFT_STICK_RELEASE_ZONE = 5500;
        private const int RIGHT_STICK_DEADZONE = 7500;

        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
        private static extern int XInputGetState14(int dwUserIndex, ref XINPUT_STATE pState);

        [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")]
        private static extern int XInputGetState910(int dwUserIndex, ref XINPUT_STATE pState);

        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        private static bool useXInput910 = false;

        private static int GetXInputState(int userIndex, ref XINPUT_STATE state)
        {
            if (useXInput910)
            {
                try { return XInputGetState910(userIndex, ref state); } catch { return -1; }
            }

            try
            {
                return XInputGetState14(userIndex, ref state);
            }
            catch (DllNotFoundException)
            {
                useXInput910 = true;
                try { return XInputGetState910(userIndex, ref state); } catch { return -1; }
            }
            catch
            {
                return -1;
            }
        }

        private readonly Window targetWindow;
        private readonly DispatcherTimer pollTimer;
        private readonly uint currentProcessId;
        private IntPtr windowHandle;
        private bool isGamepadActive;
        private bool isControllerConnected;
        private ushort lastButtons;
        private int connectedUserIndex = -1;
        private int scanCounter = 0;
        private DateTime lastGamepadActivityUtc = DateTime.MinValue;
        private Point lastMousePosition;
        private bool hasMousePosition;

        // Auto-repeat state for direction
        private FocusNavigationDirection? activeDirection;
        private DateTime directionStartTime;
        private DateTime directionLastRepeatTime;
        private const int InitialRepeatDelayMs = 260;
        private const int RepeatIntervalMs = 110;

        public event Action<FocusNavigationDirection> DirectionNavigated;
        public event Action UpPressed;
        public event Action DownPressed;
        public event Action LeftPressed;
        public event Action RightPressed;

        public event Action<int> TabCycleRequested;
        public event Action<GamepadButtonAction> ActionPressed;
        public event Action<double> ScrollRequested;
        public event Action<bool> InputModeChanged;
        public event Action<bool> ConnectionChanged;

        public bool IsGamepadActive
        {
            get { return isGamepadActive; }
            private set
            {
                if (isGamepadActive != value)
                {
                    isGamepadActive = value;
                    var handler = InputModeChanged;
                    if (handler != null) handler(isGamepadActive);
                }
            }
        }

        public bool IsControllerConnected
        {
            get { return isControllerConnected; }
            private set
            {
                if (isControllerConnected == value) return;
                isControllerConnected = value;
                var handler = ConnectionChanged;
                if (handler != null) handler(value);
            }
        }

        public GamepadNavigationService(Window window)
        {
            if (window == null) throw new ArgumentNullException("window");
            targetWindow = window;
            currentProcessId = (uint)Process.GetCurrentProcess().Id;

            targetWindow.Loaded += OnWindowLoaded;
            targetWindow.MouseMove += OnWindowMouseMove;
            targetWindow.PreviewMouseDown += OnWindowMouseDown;
            targetWindow.PreviewMouseWheel += OnWindowMouseWheel;
            targetWindow.Closed += OnWindowClosed;

            pollTimer = new DispatcherTimer(DispatcherPriority.Input)
            {
                Interval = TimeSpan.FromMilliseconds(20)
            };
            pollTimer.Tick += OnPollTick;

            if (targetWindow.IsLoaded)
            {
                var helper = new WindowInteropHelper(targetWindow);
                windowHandle = helper.Handle;
                pollTimer.Start();
            }
        }

        private void OnWindowLoaded(object sender, RoutedEventArgs e)
        {
            var helper = new WindowInteropHelper(targetWindow);
            windowHandle = helper.Handle;
            if (!pollTimer.IsEnabled) pollTimer.Start();
        }

        private void OnWindowClosed(object sender, EventArgs e)
        {
            Dispose();
        }

        private void OnWindowMouseMove(object sender, MouseEventArgs e)
        {
            Point position = e.GetPosition(targetWindow);
            if (!hasMousePosition)
            {
                lastMousePosition = position;
                hasMousePosition = true;
                return;
            }

            double dx = position.X - lastMousePosition.X;
            double dy = position.Y - lastMousePosition.Y;
            lastMousePosition = position;
            if ((dx * dx) + (dy * dy) < 36.0) return;
            if ((DateTime.UtcNow - lastGamepadActivityUtc).TotalMilliseconds < 650.0) return;
            IsGamepadActive = false;
        }

        private void OnWindowMouseDown(object sender, MouseButtonEventArgs e)
        {
            IsGamepadActive = false;
        }

        private void OnWindowMouseWheel(object sender, MouseWheelEventArgs e)
        {
            IsGamepadActive = false;
        }

        public void Start()
        {
            if (!pollTimer.IsEnabled) pollTimer.Start();
        }

        public void Stop()
        {
            if (pollTimer.IsEnabled) pollTimer.Stop();
        }

        public static bool IsNavigationSuspended { get; set; }

        private bool IsApplicationForeground()
        {
            if (IsNavigationSuspended) return false;
            if (!targetWindow.IsVisible || !targetWindow.IsActive) return false;

            IntPtr fg = GetForegroundWindow();
            if (fg == IntPtr.Zero) return false;

            if (windowHandle != IntPtr.Zero && fg == windowHandle) return true;

            if (Application.Current != null)
            {
                foreach (Window w in Application.Current.Windows)
                {
                    if (w != targetWindow && w.IsVisible)
                    {
                        var h = new WindowInteropHelper(w).Handle;
                        if (h != IntPtr.Zero && h == fg) return false;
                    }
                }
            }

            uint fgPid;
            GetWindowThreadProcessId(fg, out fgPid);
            return fgPid == currentProcessId;
        }

        private void OnPollTick(object sender, EventArgs e)
        {
            if (!IsApplicationForeground())
            {
                lastButtons = 0;
                activeDirection = null;
                return;
            }

            XINPUT_STATE state = new XINPUT_STATE();
            bool foundController = false;
            bool scannedControllers = false;

            // Fast path: poll known connected controller
            if (connectedUserIndex >= 0)
            {
                if (GetXInputState(connectedUserIndex, ref state) == 0)
                {
                    foundController = true;
                    IsControllerConnected = true;
                    SafeProcessGamepadState(ref state.Gamepad);
                }
                else
                {
                    connectedUserIndex = -1;
                    scanCounter = 44;
                }
            }

            // Slow path: scan for connected controller only periodically or on loss
            if (!foundController)
            {
                scanCounter++;
                if (scanCounter % 45 == 0 || connectedUserIndex == -1 && scanCounter < 5)
                {
                    scannedControllers = true;
                    for (int i = 0; i < 4; i++)
                    {
                        if (GetXInputState(i, ref state) == 0)
                        {
                            connectedUserIndex = i;
                            foundController = true;
                            IsControllerConnected = true;
                            SafeProcessGamepadState(ref state.Gamepad);
                            break;
                        }
                    }
                }

                if (!foundController)
                {
                    lastButtons = 0;
                    activeDirection = null;
                    if (scannedControllers) IsControllerConnected = false;
                }
            }
        }

        private void SafeProcessGamepadState(ref XINPUT_GAMEPAD pad)
        {
            try
            {
                ProcessGamepadState(ref pad);
            }
            catch
            {
                lastButtons = 0;
                activeDirection = null;
            }
        }

        private void ProcessGamepadState(ref XINPUT_GAMEPAD pad)
        {
            ushort buttons = pad.wButtons;
            short thumbLX = pad.sThumbLX;
            short thumbLY = pad.sThumbLY;
            short thumbRX = pad.sThumbRX;
            short thumbRY = pad.sThumbRY;

            int absLX = Math.Abs((int)thumbLX);
            int absLY = Math.Abs((int)thumbLY);
            int absRX = Math.Abs((int)thumbRX);
            int absRY = Math.Abs((int)thumbRY);
            bool hasActivity = (buttons != 0) ||
                               absLX > LEFT_STICK_DEADZONE ||
                               absLY > LEFT_STICK_DEADZONE ||
                               absRX > RIGHT_STICK_DEADZONE ||
                               absRY > RIGHT_STICK_DEADZONE;

            if (hasActivity)
            {
                lastGamepadActivityUtc = DateTime.UtcNow;
                if (!IsGamepadActive) IsGamepadActive = true;
            }

            if (!IsGamepadActive && !hasActivity) return;

            // Direction calculation (D-Pad priority, then Left Thumbstick)
            FocusNavigationDirection? currentDir = null;

            if ((buttons & XINPUT_GAMEPAD_DPAD_UP) != 0)
                currentDir = FocusNavigationDirection.Up;
            else if ((buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0)
                currentDir = FocusNavigationDirection.Down;
            else if ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0)
                currentDir = FocusNavigationDirection.Left;
            else if ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0)
                currentDir = FocusNavigationDirection.Right;
            else if (activeDirection == FocusNavigationDirection.Left && thumbLX < -LEFT_STICK_RELEASE_ZONE && absLX >= absLY * 0.7)
                currentDir = FocusNavigationDirection.Left;
            else if (activeDirection == FocusNavigationDirection.Right && thumbLX > LEFT_STICK_RELEASE_ZONE && absLX >= absLY * 0.7)
                currentDir = FocusNavigationDirection.Right;
            else if (activeDirection == FocusNavigationDirection.Up && thumbLY > LEFT_STICK_RELEASE_ZONE && absLY >= absLX * 0.7)
                currentDir = FocusNavigationDirection.Up;
            else if (activeDirection == FocusNavigationDirection.Down && thumbLY < -LEFT_STICK_RELEASE_ZONE && absLY >= absLX * 0.7)
                currentDir = FocusNavigationDirection.Down;
            else if (absLX > LEFT_STICK_DEADZONE || absLY > LEFT_STICK_DEADZONE)
            {
                if (absLX > absLY)
                    currentDir = thumbLX < 0 ? FocusNavigationDirection.Left : FocusNavigationDirection.Right;
                else
                    currentDir = thumbLY < 0 ? FocusNavigationDirection.Down : FocusNavigationDirection.Up;
            }

            DateTime now = DateTime.UtcNow;

            if (currentDir.HasValue)
            {
                if (activeDirection != currentDir)
                {
                    activeDirection = currentDir;
                    directionStartTime = now;
                    directionLastRepeatTime = now;
                    FireDirection(currentDir.Value);
                }
                else
                {
                    double totalHold = (now - directionStartTime).TotalMilliseconds;
                    double sinceLast = (now - directionLastRepeatTime).TotalMilliseconds;
                    if (totalHold >= InitialRepeatDelayMs && sinceLast >= RepeatIntervalMs)
                    {
                        directionLastRepeatTime = now;
                        FireDirection(currentDir.Value);
                    }
                }
            }
            else
            {
                activeDirection = null;
            }

            // Bumpers for tab switching (Edge triggered)
            bool lbPressed = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            bool rbPressed = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            bool lbLast = (lastButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            bool rbLast = (lastButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;

            if (lbPressed && !lbLast)
            {
                var handler = TabCycleRequested;
                if (handler != null) handler(-1);
            }
            if (rbPressed && !rbLast)
            {
                var handler = TabCycleRequested;
                if (handler != null) handler(1);
            }

            // Face Buttons (Edge triggered)
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_A, GamepadButtonAction.Select);
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_B, GamepadButtonAction.Back);
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_X, GamepadButtonAction.ActionX);
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_Y, GamepadButtonAction.ActionY);
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_START, GamepadButtonAction.Menu);
            CheckButtonEdge(buttons, lastButtons, XINPUT_GAMEPAD_BACK, GamepadButtonAction.View);

            // Right thumbstick smooth scrolling (vertical or horizontal)
            if (absRY > RIGHT_STICK_DEADZONE || absRX > RIGHT_STICK_DEADZONE)
            {
                double scrollDelta = 0;
                if (absRY >= absRX)
                {
                    double normalized = (double)thumbRY / 32767.0;
                    scrollDelta = -normalized * 26.0;
                }
                else
                {
                    double normalized = (double)thumbRX / 32767.0;
                    scrollDelta = normalized * 26.0;
                }
                var handler = ScrollRequested;
                if (handler != null) handler(scrollDelta);
            }

            lastButtons = buttons;
        }

        private void CheckButtonEdge(ushort current, ushort last, ushort mask, GamepadButtonAction action)
        {
            if ((current & mask) != 0 && (last & mask) == 0)
            {
                var handler = ActionPressed;
                if (handler != null) handler(action);
            }
        }

        private void FireDirection(FocusNavigationDirection dir)
        {
            switch (dir)
            {
                case FocusNavigationDirection.Up:
                    if (UpPressed != null) UpPressed();
                    break;
                case FocusNavigationDirection.Down:
                    if (DownPressed != null) DownPressed();
                    break;
                case FocusNavigationDirection.Left:
                    if (LeftPressed != null) LeftPressed();
                    break;
                case FocusNavigationDirection.Right:
                    if (RightPressed != null) RightPressed();
                    break;
            }

            var handler = DirectionNavigated;
            if (handler != null)
            {
                handler(dir);
            }
            else
            {
                try
                {
                    var focused = Keyboard.FocusedElement as UIElement;
                    if (focused != null)
                    {
                        focused.MoveFocus(new TraversalRequest(dir));
                    }
                    else
                    {
                        targetWindow.MoveFocus(new TraversalRequest(FocusNavigationDirection.First));
                    }
                }
                catch { }
            }
        }

        public void Dispose()
        {
            pollTimer.Stop();
            targetWindow.Loaded -= OnWindowLoaded;
            targetWindow.MouseMove -= OnWindowMouseMove;
            targetWindow.PreviewMouseDown -= OnWindowMouseDown;
            targetWindow.PreviewMouseWheel -= OnWindowMouseWheel;
            targetWindow.Closed -= OnWindowClosed;
        }
    }
}
