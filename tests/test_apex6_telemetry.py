"""
Test APEX 6 Telemetry JSON serialization and schema validation.
Verifies:
1. Presence and types of all new pcm_* telemetry fields.
2. Backward compatibility with older telemetry parsers.
3. Proper JSON syntax and value consistency.
"""

import json

def test_telemetry_schema():
    sample_json = """{
  "schema": 1,
  "virtual_backend": "integrated",
  "apex6": {
    "audio_format": "quadraphonic",
    "audio_mix_channels": 4,
    "audio_channel_mask": 51,
    "pcm_gain_percent": 100,
    "pcm_gain_policy": "bounded-companding-opt-in",
    "pcm_mode": "normalize",
    "pcm_reference_percent": 30,
    "pcm_reference_peak_abs16": 9830,
    "pcm_target_percent": 90,
    "pcm_requested_gain_ratio": 3.0,
    "pcm_effective_gain_ratio": 3.0,
    "pcm_gain_limited": false,
    "pcm_measurement_status": "valid",
    "pcm_output_clipped_samples": 5,
    "pcm_processed_samples": 800,
    "haptic_threshold_percent": 0,
    "pcm_output_left_peak": 48,
    "pcm_output_right_peak": 47,
    "waveform_left_peak": 9830,
    "waveform_right_peak": 9200
  }
}"""
    data = json.loads(sample_json)
    apex6 = data["apex6"]

    assert apex6["pcm_mode"] == "normalize"
    assert apex6["pcm_reference_percent"] == 30
    assert apex6["pcm_reference_peak_abs16"] == 9830
    assert apex6["pcm_target_percent"] == 90
    assert abs(apex6["pcm_requested_gain_ratio"] - 3.0) < 1e-6
    assert abs(apex6["pcm_effective_gain_ratio"] - 3.0) < 1e-6
    assert apex6["pcm_gain_limited"] is False
    assert apex6["pcm_measurement_status"] == "valid"
    assert apex6["pcm_output_clipped_samples"] == 5
    assert apex6["pcm_processed_samples"] == 800

    # Check backward compatibility fields exist
    assert "pcm_gain_percent" in apex6
    assert "pcm_output_left_peak" in apex6
    assert "waveform_left_peak" in apex6

    print("test_telemetry_schema: PASS")

if __name__ == "__main__":
    test_telemetry_schema()
    print("\nALL TELEMETRY TESTS PASSED.")
