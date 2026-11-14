using ApexSenseBridge.Common;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Services
{
    internal interface IWidgetBackend
    {
        BridgeSession.SessionInfo ReadSession();
        SessionTuningState ReadApplied(string token);
        bool Submit(string token, SessionTuningChange change);
    }

    internal sealed class LiveWidgetBackend : IWidgetBackend
    {
        public BridgeSession.SessionInfo ReadSession()
        {
            return BridgeSession.ReadActiveSession();
        }

        public SessionTuningState ReadApplied(string token)
        {
            using (var client = SessionTuningClient.Open(token))
            {
                return client == null ? null : client.ReadApplied();
            }
        }

        public bool Submit(string token, SessionTuningChange change)
        {
            using (var client = SessionTuningClient.Open(token))
            {
                return client != null && client.Submit(change);
            }
        }
    }

    internal interface IWidgetTuningStore
    {
        bool SaveGameTuning(string game, uint model, uint? trigger, uint? vibration, SessionTuningState applied);
    }

    internal sealed class WidgetControlService
    {
        internal const int ProtocolVersion = 1;
        internal const int AcknowledgeTimeoutMilliseconds = 400;
        internal static readonly string[] ProfileNames =
        {
            "none", "legacy-view-hold-swipe-up", "spider-man-2", "miles-morales",
            "ghost-of-tsushima", "warframe", "death-stranding-2", "alan-wake-2"
        };

        private readonly IWidgetBackend backend;
        private readonly IWidgetTuningStore store;
        private readonly Func<string, object[]> remapEntries;
        private readonly Action<Action> runOnUi;
        private readonly Func<string> languageCode;

        internal WidgetControlService(
            IWidgetBackend backend,
            IWidgetTuningStore store,
            Func<string, object[]> remapEntries,
            Action<Action> runOnUi,
            Func<string> languageCode)
        {
            this.backend = backend;
            this.store = store;
            this.remapEntries = remapEntries ?? (profile => new object[0]);
            this.runOnUi = runOnUi ?? (action => action());
            this.languageCode = languageCode;
        }

        internal string Handle(string requestLine)
        {
            Dictionary<string, object> request = null;
            try { request = new JavaScriptSerializer().DeserializeObject(requestLine) as Dictionary<string, object>; }
            catch { }
            if (request == null) return Serialize(Error("bad_request"));
            try
            {
                switch (Text(request, "cmd"))
                {
                    case "status": return Serialize(Status(backend.ReadSession()));
                    case "set": return Serialize(Set(request));
                    default: return Serialize(Error("unknown_command"));
                }
            }
            catch (Exception)
            {
                return Serialize(Error("internal_error"));
            }
        }

        private Dictionary<string, object> Status(BridgeSession.SessionInfo info)
        {
            var result = Base(true);
            result["language"] = Language();
            if (info == null)
            {
                result["active"] = false;
                result["phase"] = "Stopped";
                return result;
            }
            var applied = backend.ReadApplied(info.Token);
            result["active"] = true;
            result["phase"] = info.Phase.ToString();
            result["owner"] = info.Owner ?? string.Empty;
            result["game"] = info.Game ?? string.Empty;
            result["profile"] = info.Profile ?? string.Empty;
            result["controller"] = info.Controller ?? string.Empty;
            result["controllerLost"] = info.ControllerLost;
            result["canSave"] = store != null && IsTrayOwned(info);
            result["profiles"] = ProfileNames;
            if (applied != null)
            {
                result["tuning"] = Tuning(applied);
                result["remaps"] = remapEntries(ProfileName(applied.TouchpadProfile));
            }
            return result;
        }

        private Dictionary<string, object> Set(Dictionary<string, object> request)
        {
            var info = backend.ReadSession();
            if (info == null) return Error("no_session");
            var before = backend.ReadApplied(info.Token);
            if (before == null) return Error("tuning_unavailable");

            var change = new SessionTuningChange();
            var any = false;
            int number;
            if (TryNumber(request, "trigger", 0, 100, out number)) { change.TriggerStrengthPercent = (uint)number; any = true; }
            if (TryNumber(request, "vibration", 0, MaximumVibration(before), out number)) { change.VibrationStrengthPercent = (uint)number; any = true; }
            if (TryNumber(request, "touchpadProfile", 0, ProfileNames.Length - 1, out number)) { change.TouchpadProfile = (uint)number; any = true; }
            bool flag;
            if (TryFlag(request, "gripMute", out flag)) { change.GripMute = flag; any = true; }
            if (TryFlag(request, "triggerEffectsOff", out flag)) { change.TriggerEffectsOff = flag; any = true; }
            if (!any) return Error("nothing_to_set");
            if (!backend.Submit(info.Token, change)) return Error("submit_failed");

            var acknowledged = false;
            var waited = 0;
            while (waited <= AcknowledgeTimeoutMilliseconds)
            {
                var now = backend.ReadApplied(info.Token);
                if (now != null && now.RequestSequence != before.RequestSequence) { acknowledged = true; break; }
                System.Threading.Thread.Sleep(10);
                waited += 10;
            }

            var saved = false;
            bool wantsSave;
            if (TryFlag(request, "save", out wantsSave) && wantsSave && store != null && IsTrayOwned(info) &&
                (change.TriggerStrengthPercent.HasValue || change.VibrationStrengthPercent.HasValue))
            {
                var game = info.Game;
                var model = before.ControllerModel;
                var trigger = change.TriggerStrengthPercent;
                var vibration = change.VibrationStrengthPercent;
                runOnUi(() => saved = store.SaveGameTuning(game, model, trigger, vibration, before));
            }

            var result = Status(backend.ReadSession());
            result["acknowledged"] = acknowledged;
            result["saved"] = saved;
            return result;
        }

        private string Language()
        {
            try { return languageCode == null ? string.Empty : (languageCode() ?? string.Empty); }
            catch { return string.Empty; }
        }

        private static bool IsTrayOwned(BridgeSession.SessionInfo info)
        {
            return string.Equals(info.Owner, "Tray", StringComparison.OrdinalIgnoreCase) &&
                   !string.IsNullOrWhiteSpace(info.Game);
        }

        private static int MaximumVibration(SessionTuningState applied)
        {
            return applied.ControllerModel == 6 ? 100 : 200;
        }

        private static string ProfileName(uint index)
        {
            return index < ProfileNames.Length ? ProfileNames[index] : "none";
        }

        private static Dictionary<string, object> Tuning(SessionTuningState applied)
        {
            return new Dictionary<string, object>
            {
                { "trigger", (int)applied.TriggerStrengthPercent },
                { "vibration", (int)applied.VibrationStrengthPercent },
                { "gripMute", applied.GripMute },
                { "triggerEffectsOff", applied.TriggerEffectsOff },
                { "touchpadProfile", (int)applied.TouchpadProfile },
                { "touchpadProfileName", ProfileName(applied.TouchpadProfile) },
                { "slot", applied.ApexProfileSlot.HasValue ? (object)(int)applied.ApexProfileSlot.Value : null },
                { "rate", (int)applied.SampleRateHz },
                { "model", (int)applied.ControllerModel },
                { "maxVibration", MaximumVibration(applied) }
            };
        }

        private static Dictionary<string, object> Base(bool ok)
        {
            return new Dictionary<string, object> { { "ok", ok }, { "v", ProtocolVersion } };
        }

        private static Dictionary<string, object> Error(string code)
        {
            var result = Base(false);
            result["error"] = code;
            return result;
        }

        private static string Serialize(Dictionary<string, object> value)
        {
            return new JavaScriptSerializer().Serialize(value);
        }

        private static string Text(Dictionary<string, object> request, string key)
        {
            object value;
            return request.TryGetValue(key, out value) && value != null
                ? Convert.ToString(value, CultureInfo.InvariantCulture)
                : string.Empty;
        }

        private static bool TryNumber(Dictionary<string, object> request, string key, int min, int max, out int number)
        {
            number = 0;
            object value;
            if (!request.TryGetValue(key, out value) || value == null || value is bool || value is string) return false;
            try { number = Convert.ToInt32(value, CultureInfo.InvariantCulture); }
            catch { return false; }
            return number >= min && number <= max;
        }

        private static bool TryFlag(Dictionary<string, object> request, string key, out bool flag)
        {
            flag = false;
            object value;
            if (!request.TryGetValue(key, out value) || !(value is bool)) return false;
            flag = (bool)value;
            return true;
        }
    }
}
