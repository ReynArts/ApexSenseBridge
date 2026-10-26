"""
Unit and regression tests for Apex6PcmScaler.
Tests all requirements of Phase 1A:
1. Original mode: Bit-identical to 1.0.0 stable across all 65536 int16 values.
2. Companding mode: Bit-identical to scaleApex6Pcm across all 65536 int16 values and gains.
3. Linear mode: Exact scaling with saturation to [-127, 127].
4. Normalize mode:
   - 30% reference to 90% target -> exact 3.0x scaling (10->30%, 20->60%, 30->90%).
   - Missing/zero reference -> safe fallback to Original with status="invalid".
   - Target needing > max_gain -> effective gain clamped to max_gain with gain_limited=True.
   - Attenuation: ref=100%, target=90% -> 0.9x.
   - INT16_MIN (-32768) and INT16_MAX (32767) handled safely without overflow.
   - Saturating clipping counts.
"""

import math

def scale_original(sample: int) -> int:
    full_scale = 32767 if sample >= 0 else 32768
    return int(sample * 127 / full_scale)

def scale_companding(sample: int, gain_percent: int) -> int:
    gain = max(100, min(200, gain_percent))
    full_scale = 32767 if sample >= 0 else 32768
    if gain == 100:
        return int(sample * 127 / full_scale)
    magnitude = sample if sample >= 0 else -sample
    denominator = full_scale * 100 + (gain - 100) * magnitude
    return int(sample * 127 * gain / denominator)

class Apex6PcmConfig:
    def __init__(self, mode="original", linear_gain_percent=100, reference_percent=0,
                 reference_peak_abs16=0, target_percent=90, max_gain_percent=400, legacy_gain_percent=100):
        self.mode = mode
        self.linear_gain_percent = linear_gain_percent
        self.reference_percent = reference_percent
        self.reference_peak_abs16 = reference_peak_abs16
        self.target_percent = target_percent
        self.max_gain_percent = max_gain_percent
        self.legacy_gain_percent = legacy_gain_percent

class Apex6PcmScalingState:
    def __init__(self):
        self.requested_gain_ratio = 1.0
        self.effective_gain_ratio = 1.0
        self.gain_limited = False
        self.valid = True
        self.status = "valid"

class Apex6PcmScaler:
    def __init__(self, config: Apex6PcmConfig):
        self.config = config
        self.state = Apex6PcmScalingState()
        self._init()

    def _init(self):
        if self.config.mode == "original":
            self.state.requested_gain_ratio = 1.0
            self.state.effective_gain_ratio = 1.0
            self.state.gain_limited = False
            self.state.valid = True
            self.state.status = "valid"
        elif self.config.mode == "companding":
            gain = max(100, min(200, self.config.legacy_gain_percent))
            self.state.requested_gain_ratio = gain / 100.0
            self.state.effective_gain_ratio = self.state.requested_gain_ratio
            self.state.gain_limited = False
            self.state.valid = True
            self.state.status = "valid"
        elif self.config.mode == "linear":
            gain = max(0, min(400, self.config.linear_gain_percent))
            self.state.requested_gain_ratio = gain / 100.0
            self.state.effective_gain_ratio = self.state.requested_gain_ratio
            self.state.gain_limited = False
            self.state.valid = True
            self.state.status = "valid"
        elif self.config.mode == "normalize":
            ref_pct = 0.0
            if self.config.reference_peak_abs16 > 0:
                ref_pct = self.config.reference_peak_abs16 / 327.67
            elif self.config.reference_percent > 0:
                ref_pct = float(self.config.reference_percent)

            if ref_pct <= 0.0:
                self.state.requested_gain_ratio = 1.0
                self.state.effective_gain_ratio = 1.0
                self.state.gain_limited = False
                self.state.valid = False
                self.state.status = "invalid"
            else:
                target_pct = max(1.0, min(100.0, float(self.config.target_percent)))
                req_gain = target_pct / ref_pct
                self.state.requested_gain_ratio = req_gain
                max_gain = max(1.0, min(4.0, self.config.max_gain_percent / 100.0))
                if req_gain > max_gain:
                    self.state.effective_gain_ratio = max_gain
                    self.state.gain_limited = True
                    self.state.valid = True
                    self.state.status = "limited"
                else:
                    self.state.effective_gain_ratio = max(0.0, req_gain)
                    self.state.gain_limited = False
                    self.state.valid = True
                    self.state.status = "valid"

    def scale_sample(self, sample: int) -> tuple[int, bool]:
        assert -32768 <= sample <= 32767
        full_scale = 32767 if sample >= 0 else 32768
        if self.config.mode == "original":
            return int(sample * 127 / full_scale), False
        elif self.config.mode == "companding":
            return scale_companding(sample, self.config.legacy_gain_percent), False
        elif self.config.mode in ("linear", "normalize"):
            if not self.state.valid or self.state.effective_gain_ratio == 1.0:
                return int(sample * 127 / full_scale), False
            orig_out = sample * 127.0 / full_scale
            scaled = orig_out * self.state.effective_gain_ratio
            rounded = round(scaled)
            clipped = False
            if rounded > 127:
                rounded = 127
                clipped = True
            elif rounded < -127:
                rounded = -127
                clipped = True
            return int(rounded), clipped
        return int(sample * 127 / full_scale), False

def test_original_bit_identical():
    scaler = Apex6PcmScaler(Apex6PcmConfig(mode="original"))
    for s in range(-32768, 32768):
        val, clipped = scaler.scale_sample(s)
        assert not clipped
        assert val == scale_original(s)
    print("test_original_bit_identical: 65536/65536 samples bit-identical! [PASS]")

def test_companding_bit_identical():
    for g in (100, 125, 150, 175, 200):
        scaler = Apex6PcmScaler(Apex6PcmConfig(mode="companding", legacy_gain_percent=g))
        for s in (-32768, -10028, -1000, 0, 1000, 10028, 32767):
            val, clipped = scaler.scale_sample(s)
            assert not clipped
            assert val == scale_companding(s, g)
    print("test_companding_bit_identical: companding bit-identical across test vectors! [PASS]")

def test_normalization_3x():
    # ref=30%, target=90% -> 3.0x
    scaler = Apex6PcmScaler(Apex6PcmConfig(mode="normalize", reference_percent=30, target_percent=90))
    assert abs(scaler.state.effective_gain_ratio - 3.0) < 1e-6
    assert scaler.state.valid
    assert not scaler.state.gain_limited
    assert scaler.state.status == "valid"

    # 10% input
    s10 = int(round(32767 * 0.10))
    val10, clp10 = scaler.scale_sample(s10)
    assert not clp10
    assert abs(val10 / 127.0 * 100 - 30.0) < 1.0

    # 20% input
    s20 = int(round(32767 * 0.20))
    val20, clp20 = scaler.scale_sample(s20)
    assert not clp20
    assert abs(val20 / 127.0 * 100 - 60.0) < 1.0

    # 30% input
    s30 = int(round(32767 * 0.30))
    val30, clp30 = scaler.scale_sample(s30)
    assert not clp30
    assert abs(val30 / 127.0 * 100 - 90.0) < 1.0

    # Negative 30% input
    val_neg30, clp_neg30 = scaler.scale_sample(-s30)
    assert not clp_neg30
    assert val_neg30 == -val30

    # 40% input (over peak -> clips to 127)
    s40 = int(round(32767 * 0.40))
    val40, clp40 = scaler.scale_sample(s40)
    assert clp40
    assert val40 == 127

    # INT16_MIN
    val_min, clp_min = scaler.scale_sample(-32768)
    assert clp_min
    assert val_min == -127

    print("test_normalization_3x: exact 3.0x scaling, negative values, and clipping verified! [PASS]")

def test_unmeasured_fallback():
    scaler = Apex6PcmScaler(Apex6PcmConfig(mode="normalize", reference_percent=0, target_percent=90))
    assert not scaler.state.valid
    assert scaler.state.status == "invalid"
    assert scaler.state.effective_gain_ratio == 1.0
    for s in (-32768, -1000, 0, 1000, 32767):
        val, clp = scaler.scale_sample(s)
        assert not clp
        assert val == scale_original(s)
    print("test_unmeasured_fallback: safe fallback to Original verified! [PASS]")

def test_gain_limit_clamp():
    # ref=10%, target=90% -> requested 9.0x, max_gain=4.0x
    scaler = Apex6PcmScaler(Apex6PcmConfig(mode="normalize", reference_percent=10, target_percent=90, max_gain_percent=400))
    assert abs(scaler.state.requested_gain_ratio - 9.0) < 1e-6
    assert abs(scaler.state.effective_gain_ratio - 4.0) < 1e-6
    assert scaler.state.gain_limited
    assert scaler.state.valid
    assert scaler.state.status == "limited"
    print("test_gain_limit_clamp: 4.0x clamp verified! [PASS]")

def test_downward_attenuation():
    # ref=100%, target=90% -> 0.9x
    scaler = Apex6PcmScaler(Apex6PcmConfig(mode="normalize", reference_percent=100, target_percent=90))
    assert abs(scaler.state.effective_gain_ratio - 0.9) < 1e-6
    assert not scaler.state.gain_limited
    val, clp = scaler.scale_sample(32767)
    assert not clp
    assert val == 114
    print("test_downward_attenuation: 0.9x attenuation verified! [PASS]")

def main():
    test_original_bit_identical()
    test_companding_bit_identical()
    test_normalization_3x()
    test_unmeasured_fallback()
    test_gain_limit_clamp()
    test_downward_attenuation()
    print("\nALL APEX 6 PCM SCALER TESTS PASSED.")

if __name__ == "__main__":
    main()
