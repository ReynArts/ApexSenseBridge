using ApexSenseBridge.Common;
using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;
using System.Threading;
using System.Threading.Tasks;

namespace ApexSenseBridgeTray.Services
{
    public struct GyroMotionState
    {
        public bool Connected;
        public int GyroX;
        public int GyroY;
        public int GyroZ;
        public int AccelX;
        public int AccelY;
        public int AccelZ;
        public double Pitch;
        public double Roll;
        public bool MotionDetected;
        public int SamplesCount;
    }

    public struct DualSenseVisualState
    {
        public bool Connected;
        public byte LeftTrigger;   // 0 - 255
        public byte RightTrigger;  // 0 - 255
        public short ThumbLX;      // -32768 to 32767
        public short ThumbLY;      // -32768 to 32767
        public short ThumbRX;      // -32768 to 32767
        public short ThumbRY;      // -32768 to 32767
        public bool DpadUp;
        public bool DpadDown;
        public bool DpadLeft;
        public bool DpadRight;
        public bool Cross;         // A
        public bool Circle;        // B
        public bool Square;        // X
        public bool Triangle;      // Y
        public bool L1;            // Left Shoulder
        public bool R1;            // Right Shoulder
        public bool L2Btn;         // Digital trigger threshold
        public bool R2Btn;         // Digital trigger threshold
        public bool L3;            // Left Thumb
        public bool R3;            // Right Thumb
        public bool Create;        // Back / Share
        public bool Options;       // Start
        public bool TouchpadClick; // Guide / Touchpad click
        public bool PsButton;      // PS / Guide
    }

    public sealed class LatencyTestResult
    {
        public bool Success { get; set; }
        public bool IsBridge { get; set; }
        public double P50Milliseconds { get; set; }
        public double P95Milliseconds { get; set; }
        public double P99Milliseconds { get; set; }
        public int Samples { get; set; }
        public double UpdateRateHz { get; set; }
        public string Error { get; set; }
    }

    public class ControllerTestService : IDisposable
    {
        private const string EngineSessionMutexName =
            @"Local\ApexSenseBridge.ActiveSession.Owner.v1";

        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_GAMEPAD
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
        private struct XINPUT_STATE
        {
            public uint dwPacketNumber;
            public XINPUT_GAMEPAD Gamepad;
        }

        private const ushort XINPUT_GAMEPAD_DPAD_UP = 0x0001;
        private const ushort XINPUT_GAMEPAD_DPAD_DOWN = 0x0002;
        private const ushort XINPUT_GAMEPAD_DPAD_LEFT = 0x0004;
        private const ushort XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008;
        private const ushort XINPUT_GAMEPAD_START = 0x0010;
        private const ushort XINPUT_GAMEPAD_BACK = 0x0020;
        private const ushort XINPUT_GAMEPAD_LEFT_THUMB = 0x0040;
        private const ushort XINPUT_GAMEPAD_RIGHT_THUMB = 0x0080;
        private const ushort XINPUT_GAMEPAD_LEFT_SHOULDER = 0x0100;
        private const ushort XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200;
        private const ushort XINPUT_GAMEPAD_A = 0x1000;
        private const ushort XINPUT_GAMEPAD_B = 0x2000;
        private const ushort XINPUT_GAMEPAD_X = 0x4000;
        private const ushort XINPUT_GAMEPAD_Y = 0x8000;

        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
        private static extern int XInputGetState14(int dwUserIndex, ref XINPUT_STATE pState);

        [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")]
        private static extern int XInputGetState910(int dwUserIndex, ref XINPUT_STATE pState);

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

        private Process activeTestProcess;
        private readonly object processLock = new object();
        private BridgeSession activeLatencySession;
        private readonly object latencySessionLock = new object();

        public DualSenseVisualState PollInputState()
        {
            var result = new DualSenseVisualState();
            XINPUT_STATE state = new XINPUT_STATE();

            for (int i = 0; i < 4; i++)
            {
                if (GetXInputState(i, ref state) == 0)
                {
                    result.Connected = true;
                    var gp = state.Gamepad;
                    result.LeftTrigger = gp.bLeftTrigger;
                    result.RightTrigger = gp.bRightTrigger;
                    result.L2Btn = gp.bLeftTrigger > 30;
                    result.R2Btn = gp.bRightTrigger > 30;
                    result.ThumbLX = gp.sThumbLX;
                    result.ThumbLY = gp.sThumbLY;
                    result.ThumbRX = gp.sThumbRX;
                    result.ThumbRY = gp.sThumbRY;

                    ushort b = gp.wButtons;
                    result.DpadUp = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
                    result.DpadDown = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
                    result.DpadLeft = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
                    result.DpadRight = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
                    result.Options = (b & XINPUT_GAMEPAD_START) != 0;
                    result.Create = (b & XINPUT_GAMEPAD_BACK) != 0;
                    result.L3 = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
                    result.R3 = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
                    result.L1 = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
                    result.R1 = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
                    result.Cross = (b & XINPUT_GAMEPAD_A) != 0;
                    result.Circle = (b & XINPUT_GAMEPAD_B) != 0;
                    result.Square = (b & XINPUT_GAMEPAD_X) != 0;
                    result.Triangle = (b & XINPUT_GAMEPAD_Y) != 0;

                    // View/Back button also maps to Touchpad Click in DualSense mode
                    result.TouchpadClick = result.Create;
                    return result;
                }
            }

            result.Connected = false;
            return result;
        }

        public Task<bool> TestTriggerAsync(string side, string mode, int level, int seconds)
        {
            string args = string.Format("test-trigger --side {0} --mode {1} --level {2} --seconds {3}",
                side.ToLowerInvariant(), mode.ToLowerInvariant(), level, seconds);
            return RunCliCommandAsync(args);
        }

        public Task<bool> TestRumbleAsync(int left, int right, int seconds)
        {
            string args = string.Format("test-rumble --left {0} --right {1} --seconds {2}", left, right, seconds);
            return RunCliCommandAsync(args);
        }

        public Task<bool> TestRgbAsync(byte r, byte g, byte b, int seconds)
        {
            string hex = string.Format("#{0:X2}{1:X2}{2:X2}", r, g, b);
            string args = string.Format("test-rgb {0}", hex);
            return RunCliCommandAsync(args);
        }

        public Task<bool> ResetAllEffectsAsync()
        {
            KillActiveTestProcess();
            return RunCliCommandAsync("clear");
        }

        public Task<LatencyTestResult> TestNativeLatencyAsync(int seconds, CancellationToken cancellationToken)
        {
            return Task.Run(() =>
            {
                var result = new LatencyTestResult { IsBridge = false };
                if (IsBridgeSessionActive())
                {
                    result.Error = "A bridge session is already active. Stop it before running the native test.";
                    return result;
                }

                XINPUT_STATE state = new XINPUT_STATE();
                int controllerIndex = FindFirstConnectedXInputIndex(ref state);

                if (controllerIndex < 0)
                {
                    result.Error = "No XInput controller is available.";
                    return result;
                }

                var samples = new List<double>(Math.Max(1000, seconds * 1000));
                uint lastPacket = state.dwPacketNumber;
                int packetChanges = 0;
                int failedReads = 0;
                var elapsed = Stopwatch.StartNew();

                while (elapsed.Elapsed < TimeSpan.FromSeconds(seconds))
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    long started = Stopwatch.GetTimestamp();
                    int status = GetXInputState(controllerIndex, ref state);
                    long finished = Stopwatch.GetTimestamp();

                    if (status == 0)
                    {
                        samples.Add((finished - started) * 1000.0 / Stopwatch.Frequency);
                        if (state.dwPacketNumber != lastPacket)
                        {
                            lastPacket = state.dwPacketNumber;
                            packetChanges++;
                        }
                    }
                    else
                    {
                        failedReads++;
                    }

                    Thread.Sleep(1);
                }

                if (samples.Count == 0)
                {
                    result.Error = failedReads > 0
                        ? "The controller disconnected during the test."
                        : "No native input sample was collected.";
                    return result;
                }

                samples.Sort();
                result.Success = true;
                result.Samples = samples.Count;
                result.P50Milliseconds = Percentile(samples, 50);
                result.P95Milliseconds = Percentile(samples, 95);
                result.P99Milliseconds = Percentile(samples, 99);
                result.UpdateRateHz = elapsed.Elapsed.TotalSeconds > 0
                    ? packetChanges / elapsed.Elapsed.TotalSeconds
                    : 0.0;
                return result;
            }, cancellationToken);
        }

        public Task<LatencyTestResult> TestBridgeLatencyAsync(
            int seconds, int initializationTimeoutSeconds, CancellationToken cancellationToken)
        {
            return Task.Run(() =>
            {
                var result = new LatencyTestResult { IsBridge = true };
                string telemetryPath = Path.Combine(
                    Path.GetTempPath(),
                    "ApexSenseBridge-latency-" + Guid.NewGuid().ToString("N") + ".json");
                BridgeSession session = null;

                try
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    var engine = InstallLocator.ResolveEngine();
                    if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine))
                    {
                        result.Error = "ApexSenseBridge.exe was not found.";
                        return result;
                    }

                    XINPUT_STATE xinputState = new XINPUT_STATE();
                    int xinputIndex = FindFirstConnectedXInputIndex(ref xinputState);
                    if (xinputIndex < 0)
                    {
                        result.Error = "No XInput controller is available for the bridge latency test.";
                        return result;
                    }

                    try
                    {
                        string arguments = "bridge-triggers --touchpad-profile none --telemetry-json " +
                                           QuoteArgument(telemetryPath) +
                                           " --xinput-index " +
                                           xinputIndex.ToString(CultureInfo.InvariantCulture);
                        string error;
                        session = BridgeSession.TryStart(
                            engine,
                            arguments,
                            TimeSpan.FromSeconds(Math.Max(5, initializationTimeoutSeconds)),
                            message => AppLog.WriteLine("latency_test.log", message),
                            message => AppLog.WriteLine("latency_test.log", message),
                            out error);

                        if (session == null)
                        {
                            result.Error = string.IsNullOrWhiteSpace(error)
                                ? "The bridge could not start."
                                : error;
                            return result;
                        }

                        lock (latencySessionLock)
                        {
                            activeLatencySession = session;
                        }

                        var measurement = Stopwatch.StartNew();
                        while (measurement.Elapsed < TimeSpan.FromSeconds(seconds))
                        {
                            cancellationToken.ThrowIfCancellationRequested();
                            Thread.Sleep(50);
                        }
                    }
                    finally
                    {
                        if (session != null)
                        {
                            session.StopAndWait(TimeSpan.FromSeconds(15));
                            lock (latencySessionLock)
                            {
                                if (ReferenceEquals(activeLatencySession, session))
                                {
                                    activeLatencySession = null;
                                }
                            }
                            session.Dispose();
                        }
                    }

                    cancellationToken.ThrowIfCancellationRequested();
                    if (!File.Exists(telemetryPath))
                    {
                        result.Error = "The bridge did not produce latency telemetry.";
                        return result;
                    }

                    try
                    {
                        return ParseBridgeTelemetry(File.ReadAllText(telemetryPath));
                    }
                    catch (Exception ex)
                    {
                        result.Error = "Invalid bridge telemetry: " + ex.Message;
                        return result;
                    }
                }
                catch (OperationCanceledException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    result.Error = ex.Message;
                    return result;
                }
                finally
                {
                    try { if (File.Exists(telemetryPath)) File.Delete(telemetryPath); } catch { }
                }
            }, cancellationToken);
        }

        private static int FindFirstConnectedXInputIndex(ref XINPUT_STATE state)
        {
            for (int index = 0; index < 4; index++)
            {
                if (GetXInputState(index, ref state) == 0) return index;
            }
            return -1;
        }

        private static bool IsBridgeSessionActive()
        {
            try
            {
                using (var sessionMutex = Mutex.OpenExisting(EngineSessionMutexName))
                {
                    try
                    {
                        if (!sessionMutex.WaitOne(0)) return true;
                        sessionMutex.ReleaseMutex();
                        return false;
                    }
                    catch (AbandonedMutexException)
                    {
                        sessionMutex.ReleaseMutex();
                        return false;
                    }
                }
            }
            catch (WaitHandleCannotBeOpenedException)
            {
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                return true;
            }
        }

        public void StopLatencyTest()
        {
            BridgeSession session = null;
            lock (latencySessionLock)
            {
                session = activeLatencySession;
                activeLatencySession = null;
            }
            if (session != null)
            {
                session.StopAndWait(TimeSpan.FromSeconds(15));
            }
        }

        internal static double Percentile(List<double> sortedSamples, int percentage)
        {
            if (sortedSamples == null || sortedSamples.Count == 0) return 0.0;
            int index = (int)Math.Ceiling(sortedSamples.Count * (percentage / 100.0)) - 1;
            index = Math.Max(0, Math.Min(sortedSamples.Count - 1, index));
            return sortedSamples[index];
        }

        private static string QuoteArgument(string value)
        {
            return "\"" + (value ?? string.Empty).Replace("\"", "\\\"") + "\"";
        }

        private static double ReadTelemetryNumber(Dictionary<string, object> telemetry, string key)
        {
            object value;
            if (telemetry == null || !telemetry.TryGetValue(key, out value) || value == null)
            {
                return 0.0;
            }
            return Convert.ToDouble(value, CultureInfo.InvariantCulture);
        }

        internal static LatencyTestResult ParseBridgeTelemetry(string json)
        {
            var serializer = new JavaScriptSerializer();
            var telemetry = serializer.Deserialize<Dictionary<string, object>>(json);
            var result = new LatencyTestResult { IsBridge = true };
            result.P50Milliseconds = ReadTelemetryNumber(telemetry, "forward_latency_us_p50") / 1000.0;
            result.P95Milliseconds = ReadTelemetryNumber(telemetry, "forward_latency_us_p95") / 1000.0;
            result.P99Milliseconds = ReadTelemetryNumber(telemetry, "forward_latency_us_p99") / 1000.0;
            result.Samples = (int)ReadTelemetryNumber(telemetry, "forward_latency_samples");
            result.UpdateRateHz = ReadTelemetryNumber(telemetry, "virtual_report_rate_hz");
            result.Success = result.Samples > 0;
            if (!result.Success)
            {
                result.Error = "The bridge completed without forwarding input samples.";
            }
            return result;
        }

        private Task<bool> RunCliCommandAsync(string arguments)
        {
            return Task.Run(() =>
            {
                var engine = InstallLocator.ResolveEngine();
                if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine)) return false;

                KillActiveTestProcess();

                try
                {
                    var start = new ProcessStartInfo(engine, arguments)
                    {
                        CreateNoWindow = true,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        WorkingDirectory = Path.GetDirectoryName(engine)
                    };

                    lock (processLock)
                    {
                        activeTestProcess = Process.Start(start);
                    }

                    if (activeTestProcess == null) return false;

                    activeTestProcess.WaitForExit(10000);
                    int exitCode = activeTestProcess.ExitCode;

                    lock (processLock)
                    {
                        activeTestProcess = null;
                    }

                    return exitCode == 0;
                }
                catch
                {
                    return false;
                }
            });
        }

        public async Task<GyroMotionState> TestGyroAsync(int seconds = 3)
        {
            var result = new GyroMotionState();
            var engine = InstallLocator.ResolveEngine();
            if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine)) return result;

            string args = string.Format("test-gyro --seconds {0} --json", seconds);
            try
            {
                var start = new ProcessStartInfo(engine, args)
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    WorkingDirectory = Path.GetDirectoryName(engine)
                };

                using (var proc = Process.Start(start))
                {
                    if (proc == null) return result;
                    string stdout = await proc.StandardOutput.ReadToEndAsync();
                    await Task.Run(() => proc.WaitForExit(seconds * 1000 + 4000));

                    if (!string.IsNullOrWhiteSpace(stdout))
                    {
                        result.Connected = stdout.Contains("\"received\": true");
                        result.MotionDetected = stdout.Contains("\"motion_detected\": true");

                        result.GyroX = ParseJsonInt(stdout, "gyro_x");
                        result.GyroY = ParseJsonInt(stdout, "gyro_y");
                        result.GyroZ = ParseJsonInt(stdout, "gyro_z");

                        result.AccelX = ParseJsonInt(stdout, "accel_x");
                        result.AccelY = ParseJsonInt(stdout, "accel_y");
                        result.AccelZ = ParseJsonInt(stdout, "accel_z");

                        result.Pitch = ParseJsonDouble(stdout, "pitch_deg");
                        result.Roll = ParseJsonDouble(stdout, "roll_deg");
                        result.SamplesCount = ParseJsonInt(stdout, "samples");
                    }
                }
            }
            catch (Exception ex)
            {
                AppLog.WriteLine("tray_crash.log", "[WARN] TestGyroAsync failed: " + ex.Message);
            }

            return result;
        }

        private Process activeGyroStreamProcess = null;
        private CancellationTokenSource gyroStreamCts = null;

        public void StartGyroStream(Action<GyroMotionState> onSample)
        {
            StopGyroStream();

            var engine = InstallLocator.ResolveEngine();
            if (string.IsNullOrWhiteSpace(engine) || !File.Exists(engine)) return;

            gyroStreamCts = new CancellationTokenSource();
            var token = gyroStreamCts.Token;

            Task.Run(() =>
            {
                try
                {
                    var start = new ProcessStartInfo(engine, "test-gyro --stream")
                    {
                        CreateNoWindow = true,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        WorkingDirectory = Path.GetDirectoryName(engine)
                    };

                    using (var proc = Process.Start(start))
                    {
                        if (proc == null) return;
                        activeGyroStreamProcess = proc;

                        using (token.Register(() => {
                            try { if (!proc.HasExited) proc.Kill(); } catch { }
                        }))
                        {
                            string line;
                            while (!token.IsCancellationRequested && (line = proc.StandardOutput.ReadLine()) != null)
                            {
                                if (token.IsCancellationRequested) break;
                                var sample = ParseGyroStreamLine(line);
                                if (sample.Connected && onSample != null)
                                {
                                    onSample(sample);
                                }
                            }
                        }
                    }
                }
                catch { }
                finally
                {
                    activeGyroStreamProcess = null;
                }
            }, token);
        }

        public void StopGyroStream()
        {
            try
            {
                if (gyroStreamCts != null)
                {
                    gyroStreamCts.Cancel();
                    gyroStreamCts.Dispose();
                    gyroStreamCts = null;
                }
                if (activeGyroStreamProcess != null && !activeGyroStreamProcess.HasExited)
                {
                    activeGyroStreamProcess.Kill();
                    activeGyroStreamProcess = null;
                }
            }
            catch { }
        }

        private static GyroMotionState ParseGyroStreamLine(string line)
        {
            var result = new GyroMotionState();
            if (string.IsNullOrWhiteSpace(line)) return result;

            try
            {
                var parts = line.Split(' ');
                foreach (var part in parts)
                {
                    if (part.StartsWith("GYRO:"))
                    {
                        var vals = part.Substring(5).Split(',');
                        if (vals.Length >= 3)
                        {
                            int.TryParse(vals[0], out result.GyroX);
                            int.TryParse(vals[1], out result.GyroY);
                            int.TryParse(vals[2], out result.GyroZ);
                            result.Connected = true;
                        }
                    }
                    else if (part.StartsWith("ACCEL:"))
                    {
                        var vals = part.Substring(6).Split(',');
                        if (vals.Length >= 3)
                        {
                            int.TryParse(vals[0], out result.AccelX);
                            int.TryParse(vals[1], out result.AccelY);
                            int.TryParse(vals[2], out result.AccelZ);
                        }
                    }
                    else if (part.StartsWith("PITCH:"))
                    {
                        double.TryParse(part.Substring(6), NumberStyles.Float, CultureInfo.InvariantCulture, out result.Pitch);
                    }
                    else if (part.StartsWith("ROLL:"))
                    {
                        double.TryParse(part.Substring(5), NumberStyles.Float, CultureInfo.InvariantCulture, out result.Roll);
                    }
                }

                result.MotionDetected = Math.Abs(result.GyroX) > 15 || Math.Abs(result.GyroY) > 15 || Math.Abs(result.GyroZ) > 15;
            }
            catch { }

            return result;
        }

        private static int ParseJsonInt(string json, string key)
        {
            try
            {
                string search = "\"" + key + "\":";
                int idx = json.IndexOf(search);
                if (idx >= 0)
                {
                    int start = idx + search.Length;
                    int end = json.IndexOfAny(new[] { ',', '\n', '}', '\r' }, start);
                    if (end > start)
                    {
                        int val;
                        if (int.TryParse(json.Substring(start, end - start).Trim(), out val)) return val;
                    }
                }
            }
            catch { }
            return 0;
        }

        private static double ParseJsonDouble(string json, string key)
        {
            try
            {
                string search = "\"" + key + "\":";
                int idx = json.IndexOf(search);
                if (idx >= 0)
                {
                    int start = idx + search.Length;
                    int end = json.IndexOfAny(new[] { ',', '\n', '}', '\r' }, start);
                    if (end > start)
                    {
                        double val;
                        if (double.TryParse(json.Substring(start, end - start).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out val)) return val;
                    }
                }
            }
            catch { }
            return 0.0;
        }

        public void KillActiveTestProcess()
        {
            lock (processLock)
            {
                if (activeTestProcess != null && !activeTestProcess.HasExited)
                {
                    try { activeTestProcess.Kill(); } catch { }
                    activeTestProcess = null;
                }
            }
        }

        public void Dispose()
        {
            StopGyroStream();
            KillActiveTestProcess();
            StopLatencyTest();
            try
            {
                var engine = InstallLocator.ResolveEngine();
                if (!string.IsNullOrWhiteSpace(engine) && File.Exists(engine))
                {
                    var start = new ProcessStartInfo(engine, "clear")
                    {
                        CreateNoWindow = true,
                        UseShellExecute = false
                    };
                    var p = Process.Start(start);
                    if (p != null) p.WaitForExit(1500);
                }
            }
            catch { }
        }
    }
}
