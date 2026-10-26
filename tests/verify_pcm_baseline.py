"""
Baseline verification script for APEX 6 Pro PCM data path and gain models.
Verifies the exact math of:
1. Original mapping: int16 [-32768, 32767] -> int8 [-127, 127]
2. Legacy companding: scaleApex6Pcm(sample, gain) for gain 100..200
3. Linear proportional scaling: target / reference
4. ZZZ case study: reference peak 10028 (~30.6%), target 90%
"""

import math

def scale_original(sample: int) -> int:
    assert -32768 <= sample <= 32767
    full_scale = 32767 if sample >= 0 else 32768
    return int(sample * 127 / full_scale)

def scale_apex6_pcm_legacy(sample: int, gain_percent: int = 100) -> int:
    gain = max(100, min(200, gain_percent))
    full_scale = 32767 if sample >= 0 else 32768
    if gain == 100:
        return int(sample * 127 / full_scale)
    magnitude = sample if sample >= 0 else -sample
    denominator = full_scale * 100 + (gain - 100) * magnitude
    return int(sample * 127 * gain / denominator)

def scale_linear(sample: int, gain_ratio: float, max_val: int = 127) -> tuple[int, bool]:
    """Linear scaling with saturation clip to [-max_val, max_val]."""
    full_scale = 32767 if sample >= 0 else 32768
    # Baseline original float output in range [-127, 127]
    original_val = sample * 127.0 / full_scale
    scaled = original_val * gain_ratio
    rounded = round(scaled)
    clipped = False
    if rounded > max_val:
        rounded = max_val
        clipped = True
    elif rounded < -max_val:
        rounded = -max_val
        clipped = True
    return int(rounded), clipped

def main():
    print("=== APEX 6 Pro PCM Baseline Math Analysis ===")

    # 1. ZZZ peak analysis
    zzz_sample = 10028 # ~30.6% of 32767
    zzz_pct = zzz_sample / 32767.0 * 100.0
    print(f"ZZZ observed peak: {zzz_sample} / 32767 ({zzz_pct:.2f}%)")

    orig_out = scale_original(zzz_sample)
    print(f"Original output (gain=100%): {orig_out} / 127 ({orig_out / 127 * 100:.2f}%)")

    comp_150 = scale_apex6_pcm_legacy(zzz_sample, 150)
    comp_200 = scale_apex6_pcm_legacy(zzz_sample, 200)
    print(f"Legacy companding gain=150%: {comp_150} / 127 ({comp_150 / 127 * 100:.2f}%)")
    print(f"Legacy companding gain=200%: {comp_200} / 127 ({comp_200 / 127 * 100:.2f}%)")

    # 2. Linear Normalization: ref=30.6%, target=90%
    target_pct = 90.0
    ref_pct = zzz_pct
    gain_ratio = target_pct / ref_pct
    print(f"\nLinear Normalization: Reference={ref_pct:.2f}%, Target={target_pct:.2f}% -> Gain Ratio = {gain_ratio:.4f}x")

    # Test proportionality across sample amplitudes
    test_samples = [
        ("3.06% (quiet)", int(32767 * 0.0306)),
        ("10.2% (low)", int(32767 * 0.102)),
        ("20.4% (mid)", int(32767 * 0.204)),
        ("30.6% (peak)", zzz_sample),
        ("40.0% (over-peak event)", int(32767 * 0.40)),
        ("Full-scale (100%)", 32767),
    ]

    print("\nSample Amplitude Scaling Table (Ref=30.6%, Target=90%, Ratio=2.94x):")
    print(f"{'Input Sample':<25} {'Orig out':<10} {'Compand 200%':<14} {'Linear 2.94x':<14} {'Clipped?'}")
    print("-" * 75)
    for label, s in test_samples:
        orig = scale_original(s)
        comp = scale_apex6_pcm_legacy(s, 200)
        lin, clp = scale_linear(s, gain_ratio)
        orig_pct = orig / 127 * 100
        comp_pct = comp / 127 * 100
        lin_pct = lin / 127 * 100
        print(f"{label:<25} {orig:3d} ({orig_pct:4.1f}%)   {comp:3d} ({comp_pct:4.1f}%)     {lin:3d} ({lin_pct:4.1f}%)     {clp}")

    # 3. Verify exact 3.0x scaling requirement (30% -> 90%)
    ref_30 = int(round(32767 * 0.30))
    s_10 = int(round(32767 * 0.10))
    s_20 = int(round(32767 * 0.20))
    s_30 = ref_30

    out_10, _ = scale_linear(s_10, 3.0)
    out_20, _ = scale_linear(s_20, 3.0)
    out_30, _ = scale_linear(s_30, 3.0)

    pct_10 = out_10 / 127.0 * 100.0
    pct_20 = out_20 / 127.0 * 100.0
    pct_30 = out_30 / 127.0 * 100.0
    print(f"\nExact 3.0x Verification:")
    print(f"10% input ({s_10}) -> output {out_10} / 127 ({pct_10:.1f}%) [Expected ~30%]")
    print(f"20% input ({s_20}) -> output {out_20} / 127 ({pct_20:.1f}%) [Expected ~60%]")
    print(f"30% input ({s_30}) -> output {out_30} / 127 ({pct_30:.1f}%) [Expected ~90%]")

    assert abs(pct_10 - 30.0) < 1.0, f"10% scaling failed: {pct_10}"
    assert abs(pct_20 - 60.0) < 1.0, f"20% scaling failed: {pct_20}"
    assert abs(pct_30 - 90.0) < 1.0, f"30% scaling failed: {pct_30}"

    # 4. Boundary: INT16_MIN
    s_min = -32768
    out_min, clp_min = scale_linear(s_min, 3.0)
    assert out_min == -127 and clp_min, f"INT16_MIN failed: {out_min}"
    print(f"INT16_MIN (-32768) -> output {out_min} (clipped={clp_min}) [OK]")

    # 5. Attenuation: reference=100%, target=90% -> gain 0.9x
    out_att, clp_att = scale_linear(32767, 0.9)
    assert out_att == 114 and not clp_att, f"Attenuation failed: {out_att}"
    print(f"Attenuation (Ref=100%, Target=90%, Ratio=0.9x): 32767 -> {out_att} ({out_att/127*100:.1f}%) [OK]")

    print("\nALL BASELINE MATH ASSERTIONS PASSED SUCCESSFULLY.")

if __name__ == "__main__":
    main()
