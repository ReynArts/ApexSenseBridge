using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Data.Json;

namespace ApexSenseBridgeWidget
{
    internal sealed class TuningState
    {
        public int Trigger;
        public int Vibration;
        public bool GripMute;
        public bool TriggerEffectsOff;
        public int TouchpadProfile;
        public string TouchpadProfileName = "none";
        public int SampleRate;
        public int Model;
        public int MaxVibration = 100;
    }

    internal sealed class RemapEntry
    {
        public string[] Buttons = new string[0];
        public bool Hold;
        public string HoldLabel = string.Empty;
        public string Output = string.Empty;
        public string OutputLabel = string.Empty;
        public string Use = string.Empty;
    }

    internal sealed class BridgeStatus
    {
        public bool TrayReachable;
        public bool Active;
        public string Phase = "Stopped";
        public string Owner = string.Empty;
        public string Game = string.Empty;
        public string Controller = string.Empty;
        public bool ControllerLost;
        public bool CanSave;
        public string[] Profiles = new string[0];
        public RemapEntry[] Remaps = new RemapEntry[0];
        public TuningState Tuning;
        public bool Acknowledged;
        public bool Saved;
        public string Error = string.Empty;
        public string Language = string.Empty;
    }

    internal static class BridgeClient
    {
        private const string PipeName = "ApexSenseBridge.Widget.v1";
        private const int ConnectTimeoutMilliseconds = 600;
        private const int ReplyTimeoutMilliseconds = 1500;

        public static Task<BridgeStatus> StatusAsync()
        {
            return RequestAsync("{\"v\":1,\"cmd\":\"status\"}");
        }

        public static Task<BridgeStatus> SetAsync(int? trigger, int? vibration, bool? gripMute, bool? triggerEffectsOff,
                                                  int? touchpadProfile, bool save)
        {
            var request = new JsonObject();
            request["v"] = JsonValue.CreateNumberValue(1);
            request["cmd"] = JsonValue.CreateStringValue("set");
            if (trigger.HasValue) request["trigger"] = JsonValue.CreateNumberValue(trigger.Value);
            if (vibration.HasValue) request["vibration"] = JsonValue.CreateNumberValue(vibration.Value);
            if (gripMute.HasValue) request["gripMute"] = JsonValue.CreateBooleanValue(gripMute.Value);
            if (triggerEffectsOff.HasValue) request["triggerEffectsOff"] = JsonValue.CreateBooleanValue(triggerEffectsOff.Value);
            if (touchpadProfile.HasValue) request["touchpadProfile"] = JsonValue.CreateNumberValue(touchpadProfile.Value);
            if (save) request["save"] = JsonValue.CreateBooleanValue(true);
            return RequestAsync(request.Stringify());
        }

        private static async Task<BridgeStatus> RequestAsync(string requestLine)
        {
            try
            {
                using (var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous))
                {
                    using (var connectTimeout = new CancellationTokenSource(ConnectTimeoutMilliseconds))
                    {
                        await pipe.ConnectAsync(connectTimeout.Token);
                    }
                    var bytes = new UTF8Encoding(false).GetBytes(requestLine + "\n");
                    await pipe.WriteAsync(bytes, 0, bytes.Length);
                    await pipe.FlushAsync();
                    using (var replyTimeout = new CancellationTokenSource(ReplyTimeoutMilliseconds))
                    {
                        var reader = new StreamReader(pipe, Encoding.UTF8);
                        var readTask = reader.ReadLineAsync();
                        var done = await Task.WhenAny(readTask, Task.Delay(ReplyTimeoutMilliseconds, replyTimeout.Token));
                        if (done != readTask) return Unreachable("timeout");
                        replyTimeout.Cancel();
                        return Parse(await readTask);
                    }
                }
            }
            catch (Exception)
            {
                return Unreachable("unreachable");
            }
        }

        private static BridgeStatus Unreachable(string reason)
        {
            return new BridgeStatus { TrayReachable = false, Error = reason };
        }

        private static BridgeStatus Parse(string line)
        {
            JsonObject root;
            if (string.IsNullOrEmpty(line) || !JsonObject.TryParse(line, out root)) return Unreachable("bad_reply");
            var status = new BridgeStatus { TrayReachable = true };
            if (!root.GetNamedBoolean("ok", false))
            {
                status.Error = root.GetNamedString("error", "error");
                return status;
            }
            status.Language = root.GetNamedString("language", string.Empty);
            status.Active = root.GetNamedBoolean("active", false);
            status.Phase = root.GetNamedString("phase", "Stopped");
            status.Owner = root.GetNamedString("owner", string.Empty);
            status.Game = root.GetNamedString("game", string.Empty);
            status.Controller = root.GetNamedString("controller", string.Empty);
            status.ControllerLost = root.GetNamedBoolean("controllerLost", false);
            status.CanSave = root.GetNamedBoolean("canSave", false);
            status.Acknowledged = root.GetNamedBoolean("acknowledged", false);
            status.Saved = root.GetNamedBoolean("saved", false);
            status.Profiles = Strings(root, "profiles");
            status.Remaps = Remaps(root);
            JsonObject tuning;
            if (root.ContainsKey("tuning") && root.GetNamedValue("tuning").ValueType == JsonValueType.Object)
            {
                tuning = root.GetNamedObject("tuning");
                status.Tuning = new TuningState
                {
                    Trigger = (int)tuning.GetNamedNumber("trigger", 100),
                    Vibration = (int)tuning.GetNamedNumber("vibration", 100),
                    GripMute = tuning.GetNamedBoolean("gripMute", false),
                    TriggerEffectsOff = tuning.GetNamedBoolean("triggerEffectsOff", false),
                    TouchpadProfile = (int)tuning.GetNamedNumber("touchpadProfile", 0),
                    TouchpadProfileName = tuning.GetNamedString("touchpadProfileName", "none"),
                    SampleRate = (int)tuning.GetNamedNumber("rate", 0),
                    Model = (int)tuning.GetNamedNumber("model", 0),
                    MaxVibration = (int)tuning.GetNamedNumber("maxVibration", 100)
                };
            }
            return status;
        }

        private static RemapEntry[] Remaps(JsonObject root)
        {
            var result = new System.Collections.Generic.List<RemapEntry>();
            if (!root.ContainsKey("remaps") || root.GetNamedValue("remaps").ValueType != JsonValueType.Array) return result.ToArray();
            foreach (var item in root.GetNamedArray("remaps"))
            {
                if (item.ValueType != JsonValueType.Object) continue;
                var entry = item.GetObject();
                var buttons = new System.Collections.Generic.List<string>();
                if (entry.ContainsKey("buttons") && entry.GetNamedValue("buttons").ValueType == JsonValueType.Array)
                {
                    foreach (var button in entry.GetNamedArray("buttons"))
                    {
                        if (button.ValueType == JsonValueType.String) buttons.Add(button.GetString());
                    }
                }
                result.Add(new RemapEntry
                {
                    Buttons = buttons.ToArray(),
                    Hold = entry.GetNamedBoolean("hold", false),
                    HoldLabel = entry.GetNamedString("holdLabel", string.Empty),
                    Output = entry.GetNamedString("output", string.Empty),
                    OutputLabel = entry.GetNamedString("outputLabel", string.Empty),
                    Use = entry.GetNamedString("use", string.Empty)
                });
            }
            return result.ToArray();
        }

        private static string[] Strings(JsonObject root, string key)
        {
            if (!root.ContainsKey(key) || root.GetNamedValue(key).ValueType != JsonValueType.Array) return new string[0];
            var array = root.GetNamedArray(key);
            var result = new string[array.Count];
            for (var i = 0; i < result.Length; i++) result[i] = array.GetStringAt((uint)i);
            return result;
        }
    }
}
