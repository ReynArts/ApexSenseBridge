"""
Test CLI parsing and validation logic for APEX 6 PCM normalization options.
Simulates the exact rules implemented in BridgeOptions.cpp:
1. --apex6-pcm-mode original|companding|linear|normalize
2. --apex6-pcm-linear-gain-percent 0..400
3. --apex6-pcm-reference-percent 1..100
4. --apex6-pcm-target-percent 1..100
5. --apex6-pcm-max-gain-percent 100..400
6. Conflict with legacy --apex6-haptic-gain
7. Requirement for --rumble
"""

class BridgeOptionsParser:
    def __init__(self):
        self.route_rumble = False
        self.apex6_haptic_gain_percent = 100
        self.apex6_haptic_gain_explicit = False

        self.apex6_pcm_mode = "original"
        self.apex6_pcm_mode_explicit = False
        self.apex6_pcm_linear_gain = 100
        self.apex6_pcm_reference_percent = 0
        self.apex6_pcm_target_percent = 90
        self.apex6_pcm_max_gain = 400
        self.apex6_pcm_options_explicit = False

    def parse(self, args: list[str]) -> tuple[bool, str]:
        i = 0
        while i < len(args):
            val = args[i]
            if val == "--rumble":
                self.route_rumble = True
            elif val == "--apex6-haptic-gain":
                self.apex6_haptic_gain_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-haptic-gain requires an integer percentage from 100 to 200."
                try:
                    num = int(args[i])
                    if num < 100 or num > 200 or args[i].startswith("-"):
                        raise ValueError()
                    self.apex6_haptic_gain_percent = num
                except ValueError:
                    return False, "--apex6-haptic-gain requires an integer percentage from 100 to 200."
            elif val == "--apex6-pcm-mode":
                self.apex6_pcm_mode_explicit = True
                self.apex6_pcm_options_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-pcm-mode requires one of: original, companding, linear, normalize."
                mode = args[i].lower()
                if mode not in ("original", "companding", "linear", "normalize"):
                    return False, "--apex6-pcm-mode requires one of: original, companding, linear, normalize."
                self.apex6_pcm_mode = mode
            elif val == "--apex6-pcm-linear-gain-percent":
                self.apex6_pcm_options_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-pcm-linear-gain-percent requires an integer percentage from 0 to 400."
                try:
                    num = int(args[i])
                    if num < 0 or num > 400 or args[i].startswith("-"):
                        raise ValueError()
                    self.apex6_pcm_linear_gain = num
                except ValueError:
                    return False, "--apex6-pcm-linear-gain-percent requires an integer percentage from 0 to 400."
            elif val == "--apex6-pcm-reference-percent":
                self.apex6_pcm_options_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-pcm-reference-percent requires an integer percentage from 1 to 100."
                try:
                    num = int(args[i])
                    if num < 1 or num > 100 or args[i].startswith("-"):
                        raise ValueError()
                    self.apex6_pcm_reference_percent = num
                except ValueError:
                    return False, "--apex6-pcm-reference-percent requires an integer percentage from 1 to 100."
            elif val == "--apex6-pcm-target-percent":
                self.apex6_pcm_options_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-pcm-target-percent requires an integer percentage from 1 to 100."
                try:
                    num = int(args[i])
                    if num < 1 or num > 100 or args[i].startswith("-"):
                        raise ValueError()
                    self.apex6_pcm_target_percent = num
                except ValueError:
                    return False, "--apex6-pcm-target-percent requires an integer percentage from 1 to 100."
            elif val == "--apex6-pcm-max-gain-percent":
                self.apex6_pcm_options_explicit = True
                i += 1
                if i >= len(args):
                    return False, "--apex6-pcm-max-gain-percent requires an integer percentage from 100 to 400."
                try:
                    num = int(args[i])
                    if num < 100 or num > 400 or args[i].startswith("-"):
                        raise ValueError()
                    self.apex6_pcm_max_gain = num
                except ValueError:
                    return False, "--apex6-pcm-max-gain-percent requires an integer percentage from 100 to 400."
            i += 1

        # Validation rules:
        if self.apex6_haptic_gain_explicit and self.apex6_pcm_options_explicit:
            return False, "Cannot combine legacy --apex6-haptic-gain with --apex6-pcm-* options."

        if self.apex6_haptic_gain_explicit and not self.route_rumble:
            return False, "--apex6-haptic-gain requires --rumble."

        if self.apex6_pcm_options_explicit and not self.route_rumble:
            return False, "--apex6-pcm-* options require --rumble."

        # Map legacy option to mode
        if self.apex6_haptic_gain_explicit:
            self.apex6_pcm_mode = "companding"

        return True, ""

def test_valid_normalize_args():
    parser = BridgeOptionsParser()
    ok, err = parser.parse([
        "--rumble",
        "--apex6-pcm-mode", "normalize",
        "--apex6-pcm-reference-percent", "30",
        "--apex6-pcm-target-percent", "90",
        "--apex6-pcm-max-gain-percent", "400"
    ])
    assert ok, f"Failed: {err}"
    assert parser.apex6_pcm_mode == "normalize"
    assert parser.apex6_pcm_reference_percent == 30
    assert parser.apex6_pcm_target_percent == 90
    assert parser.apex6_pcm_max_gain == 400
    print("test_valid_normalize_args: PASS")

def test_valid_linear_args():
    parser = BridgeOptionsParser()
    ok, err = parser.parse([
        "--rumble",
        "--apex6-pcm-mode", "linear",
        "--apex6-pcm-linear-gain-percent", "250"
    ])
    assert ok, f"Failed: {err}"
    assert parser.apex6_pcm_mode == "linear"
    assert parser.apex6_pcm_linear_gain == 250
    print("test_valid_linear_args: PASS")

def test_legacy_option_backward_compat():
    parser = BridgeOptionsParser()
    ok, err = parser.parse([
        "--rumble",
        "--apex6-haptic-gain", "150"
    ])
    assert ok, f"Failed: {err}"
    assert parser.apex6_pcm_mode == "companding"
    assert parser.apex6_haptic_gain_percent == 150
    print("test_legacy_option_backward_compat: PASS")

def test_conflict_rejected():
    parser = BridgeOptionsParser()
    ok, err = parser.parse([
        "--rumble",
        "--apex6-haptic-gain", "150",
        "--apex6-pcm-mode", "normalize"
    ])
    assert not ok
    assert "Cannot combine legacy --apex6-haptic-gain" in err
    print("test_conflict_rejected: PASS")

def test_missing_rumble_rejected():
    parser = BridgeOptionsParser()
    ok, err = parser.parse([
        "--apex6-pcm-mode", "normalize"
    ])
    assert not ok
    assert "require --rumble" in err
    print("test_missing_rumble_rejected: PASS")

def test_range_errors():
    for flag, bad_val in [
        ("--apex6-pcm-mode", "turbo"),
        ("--apex6-pcm-linear-gain-percent", "500"),
        ("--apex6-pcm-reference-percent", "0"),
        ("--apex6-pcm-reference-percent", "101"),
        ("--apex6-pcm-target-percent", "0"),
        ("--apex6-pcm-target-percent", "101"),
        ("--apex6-pcm-max-gain-percent", "50"),
        ("--apex6-pcm-max-gain-percent", "450"),
    ]:
        parser = BridgeOptionsParser()
        ok, err = parser.parse(["--rumble", flag, bad_val])
        assert not ok, f"Expected failure for {flag} {bad_val}"
    print("test_range_errors: PASS")

def main():
    test_valid_normalize_args()
    test_valid_linear_args()
    test_legacy_option_backward_compat()
    test_conflict_rejected()
    test_missing_rumble_rejected()
    test_range_errors()
    print("\nALL BRIDGE OPTIONS APEX6 PCM TESTS PASSED.")

if __name__ == "__main__":
    main()
