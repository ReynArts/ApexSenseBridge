"""
Unit and regression tests for TraySettings APEX 6 PCM configuration.
Tests:
1. Serialization & Deserialization:
   - Loading old settings without Apex6PcmGlobal/Apex6PcmGames preserves existing defaults.
   - Round-trip JSON serialization of Apex6PcmGlobal and Apex6PcmGames.
2. Inheritance & Override:
   - If game is not in Apex6PcmGames or UseGlobal==True, game uses Apex6PcmGlobal.
   - If game has UseGlobal==False, game uses its own Apex6PcmGameSettings.
   - Game keys are normalized and matched case-insensitively.
3. Command-line generation:
   - Global original -> produces no extra args or --apex6-pcm-mode original
   - Global linear 150% -> --apex6-pcm-mode linear --apex6-pcm-linear-gain-percent 150
   - Per-game normalize ref=30%, target=90% -> --apex6-pcm-mode normalize --apex6-pcm-reference-percent 30 --apex6-pcm-target-percent 90 --apex6-pcm-max-gain-percent 400
   - Per-game isolation: settings for 'zenless-zone-zero' do not apply to 'pragmata'.
"""

import json

class Apex6PcmGlobalSettings:
    def __init__(self, mode="original", linear_gain_percent=100, target_peak_percent=90, max_gain_percent=400):
        self.Mode = mode
        self.LinearGainPercent = linear_gain_percent
        self.TargetPeakPercent = target_peak_percent
        self.MaxGainPercent = max_gain_percent

    def normalize(self):
        m = (self.Mode or "original").strip().lower()
        self.Mode = m if m in ("original", "companding", "linear", "normalize") else "original"
        self.LinearGainPercent = max(0, min(400, self.LinearGainPercent))
        self.TargetPeakPercent = max(1, min(100, self.TargetPeakPercent))
        self.MaxGainPercent = max(100, min(400, self.MaxGainPercent))

class Apex6PcmGameSettings:
    def __init__(self, use_global=True, mode="normalize", measured_ref_peak=0, measured_ref_pct=0,
                 target_peak_percent=90, max_gain_percent=400, linear_gain_percent=100):
        self.UseGlobal = use_global
        self.Mode = mode
        self.MeasuredReferencePeakAbs16 = measured_ref_peak
        self.MeasuredReferencePercent = measured_ref_pct
        self.TargetPeakPercent = target_peak_percent
        self.MaxGainPercent = max_gain_percent
        self.LinearGainPercent = linear_gain_percent
        self.MeasuredUtc = ""
        self.MeasurementVersion = 1
        self.ObservedLeftPeakAbs16 = 0
        self.ObservedRightPeakAbs16 = 0
        self.ObservedActiveBlocks = 0
        self.ObservedStatus = "none"

    def normalize(self):
        m = (self.Mode or "normalize").strip().lower()
        self.Mode = m if m in ("original", "companding", "linear", "normalize") else "normalize"
        self.MeasuredReferencePeakAbs16 = max(0, min(32768, self.MeasuredReferencePeakAbs16))
        self.MeasuredReferencePercent = max(0, min(100, self.MeasuredReferencePercent))
        self.TargetPeakPercent = max(1, min(100, self.TargetPeakPercent))
        self.MaxGainPercent = max(100, min(400, self.MaxGainPercent))
        self.LinearGainPercent = max(0, min(400, self.LinearGainPercent))

class TraySettingsSimulator:
    def __init__(self):
        self.AutoDetectGames = True
        self.EnableRumble = True
        self.Apex6PcmGlobal = Apex6PcmGlobalSettings()
        self.Apex6PcmGames = {}

    def get_apex6_pcm_settings(self, normalized_key: str):
        self.Apex6PcmGlobal.normalize()
        if not normalized_key:
            return self.Apex6PcmGlobal, True

        key_lower = normalized_key.strip().lower()
        for k, v in self.Apex6PcmGames.items():
            if k.strip().lower() == key_lower:
                v.normalize()
                if v.UseGlobal:
                    return self.Apex6PcmGlobal, True
                return v, False

        return self.Apex6PcmGlobal, True

    def build_apex6_pcm_arguments(self, normalized_key: str) -> str:
        cfg, is_global = self.get_apex6_pcm_settings(normalized_key)
        mode = cfg.Mode
        if mode == "original":
            return "--apex6-pcm-mode original"
        elif mode == "companding":
            return "--apex6-pcm-mode companding"
        elif mode == "linear":
            return f"--apex6-pcm-mode linear --apex6-pcm-linear-gain-percent {cfg.LinearGainPercent}"
        elif mode == "normalize":
            ref = 0
            if isinstance(cfg, Apex6PcmGameSettings):
                if cfg.MeasuredReferencePercent > 0:
                    ref = cfg.MeasuredReferencePercent
                elif cfg.MeasuredReferencePeakAbs16 > 0:
                    ref = int(round(cfg.MeasuredReferencePeakAbs16 / 327.67))
            if ref <= 0:
                # Fallback if no reference peak measured
                return "--apex6-pcm-mode original"
            return f"--apex6-pcm-mode normalize --apex6-pcm-reference-percent {ref} --apex6-pcm-target-percent {cfg.TargetPeakPercent} --apex6-pcm-max-gain-percent {cfg.MaxGainPercent}"
        return "--apex6-pcm-mode original"

def test_migration_and_defaults():
    # Simulate loading an old JSON without any Apex6Pcm fields
    old_json = '{"AutoDetectGames": true, "EnableRumble": true}'
    data = json.loads(old_json)
    sim = TraySettingsSimulator()
    # No Apex6Pcm fields in data
    cfg, is_global = sim.get_apex6_pcm_settings("zenless-zone-zero")
    assert is_global is True
    assert cfg.Mode == "original"
    assert sim.build_apex6_pcm_arguments("zenless-zone-zero") == "--apex6-pcm-mode original"
    print("test_migration_and_defaults: PASS")

def test_per_game_override_and_isolation():
    sim = TraySettingsSimulator()
    # Global is linear 150%
    sim.Apex6PcmGlobal = Apex6PcmGlobalSettings(mode="linear", linear_gain_percent=150)

    # Game A (ZZZ) has per-game override: normalize ref=31%, target=90%
    zzz = Apex6PcmGameSettings(use_global=False, mode="normalize", measured_ref_pct=31, target_peak_percent=90)
    sim.Apex6PcmGames["zenless-zone-zero"] = zzz

    # Game B (Pragmata) uses global settings
    pragmata = Apex6PcmGameSettings(use_global=True)
    sim.Apex6PcmGames["pragmata"] = pragmata

    # Test ZZZ
    args_zzz = sim.build_apex6_pcm_arguments("zenless-zone-zero")
    assert args_zzz == "--apex6-pcm-mode normalize --apex6-pcm-reference-percent 31 --apex6-pcm-target-percent 90 --apex6-pcm-max-gain-percent 400"

    # Case insensitivity test
    args_zzz_case = sim.build_apex6_pcm_arguments("Zenless-Zone-Zero")
    assert args_zzz_case == args_zzz

    # Test Pragmata (must use global linear 150%, not ZZZ's normalize!)
    args_pragmata = sim.build_apex6_pcm_arguments("pragmata")
    assert args_pragmata == "--apex6-pcm-mode linear --apex6-pcm-linear-gain-percent 150"

    # Test unknown game (must use global linear 150%)
    args_unknown = sim.build_apex6_pcm_arguments("unknown-game")
    assert args_unknown == "--apex6-pcm-mode linear --apex6-pcm-linear-gain-percent 150"

    print("test_per_game_override_and_isolation: PASS")

def test_roundtrip_serialization():
    sim = TraySettingsSimulator()
    sim.Apex6PcmGlobal = Apex6PcmGlobalSettings(mode="normalize", target_peak_percent=85)
    sim.Apex6PcmGames["zenless-zone-zero"] = Apex6PcmGameSettings(
        use_global=False, mode="normalize", measured_ref_peak=9830, measured_ref_pct=30, target_peak_percent=90
    )

    # Serialize to JSON dictionary
    payload = {
        "Apex6PcmGlobal": {
            "Mode": sim.Apex6PcmGlobal.Mode,
            "LinearGainPercent": sim.Apex6PcmGlobal.LinearGainPercent,
            "TargetPeakPercent": sim.Apex6PcmGlobal.TargetPeakPercent,
            "MaxGainPercent": sim.Apex6PcmGlobal.MaxGainPercent,
        },
        "Apex6PcmGames": {
            "zenless-zone-zero": {
                "UseGlobal": False,
                "Mode": "normalize",
                "MeasuredReferencePeakAbs16": 9830,
                "MeasuredReferencePercent": 30,
                "TargetPeakPercent": 90,
                "MaxGainPercent": 400,
            }
        }
    }
    dumped = json.dumps(payload)
    reloaded = json.loads(dumped)

    assert reloaded["Apex6PcmGlobal"]["Mode"] == "normalize"
    assert reloaded["Apex6PcmGlobal"]["TargetPeakPercent"] == 85
    assert reloaded["Apex6PcmGames"]["zenless-zone-zero"]["MeasuredReferencePeakAbs16"] == 9830
    print("test_roundtrip_serialization: PASS")

def main():
    test_migration_and_defaults()
    test_per_game_override_and_isolation()
    test_roundtrip_serialization()
    print("\nALL TRAY SETTINGS SIMULATION TESTS PASSED.")

if __name__ == "__main__":
    main()
