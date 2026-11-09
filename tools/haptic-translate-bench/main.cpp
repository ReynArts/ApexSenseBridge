#include "dualsense/Apex6AudioTranslator.h"

#ifdef ASB_BENCH_VOICE_GATE
#include "voice_gate.h"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using asb::dualsense::Apex6AudioTranslator;
using asb::dualsense::Apex6TranslatorConfig;

struct Wav {
    std::uint32_t rate = 0;
    std::vector<std::vector<float>> channels;
};

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(readU16(bytes, offset)) |
           (static_cast<std::uint32_t>(readU16(bytes, offset + 2)) << 16);
}

bool loadWav(const std::string& path, Wav& wav, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                          std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file: " + path;
        return false;
    }
    std::uint16_t tag = 0;
    std::uint16_t channelCount = 0;
    std::uint16_t bits = 0;
    std::size_t dataOffset = 0;
    std::size_t dataSize = 0;
    for (std::size_t position = 12; position + 8 <= bytes.size();) {
        const auto size = static_cast<std::size_t>(readU32(bytes, position + 4));
        const auto body = position + 8;
        if (std::memcmp(bytes.data() + position, "fmt ", 4) == 0 && body + 16 <= bytes.size()) {
            tag = readU16(bytes, body);
            channelCount = readU16(bytes, body + 2);
            wav.rate = readU32(bytes, body + 4);
            bits = readU16(bytes, body + 14);
            if (tag == 0xFFFE && size >= 26 && body + 26 <= bytes.size()) tag = readU16(bytes, body + 24);
        } else if (std::memcmp(bytes.data() + position, "data", 4) == 0) {
            dataOffset = body;
            dataSize = std::min(size, bytes.size() - body);
            break;
        }
        position = body + size + (size & 1U);
    }
    const bool supported = (tag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (tag == 3 && bits == 32);
    if (!supported || channelCount < 1 || channelCount > 2 || dataOffset == 0) {
        error = "unsupported WAV layout in " + path;
        return false;
    }
    const std::size_t bytesPerSample = bits / 8U;
    const std::size_t frames = dataSize / (bytesPerSample * channelCount);
    wav.channels.assign(channelCount, std::vector<float>(frames));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            const auto at = dataOffset + (frame * channelCount + channel) * bytesPerSample;
            float value = 0.0F;
            if (tag == 3) {
                const auto raw = readU32(bytes, at);
                std::memcpy(&value, &raw, sizeof(value));
            } else if (bits == 16) {
                value = static_cast<float>(static_cast<std::int16_t>(readU16(bytes, at))) / 32768.0F;
            } else if (bits == 24) {
                std::int32_t raw = bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16);
                if (raw & 0x800000) raw |= ~0xFFFFFF;
                value = static_cast<float>(raw) / 8388608.0F;
            } else {
                value = static_cast<float>(static_cast<std::int32_t>(readU32(bytes, at))) / 2147483648.0F;
            }
            wav.channels[channel][frame] = value;
        }
    }
    return true;
}

#ifdef ASB_BENCH_VOICE_GATE
// Settings past the 0.1 config. voice_gate 0.2 exports vg_set_high_band (the 16 kHz
// separation path cannot see above ~7.2 kHz, so sibilants reached the 6-12 kHz band whole);
// it is looked up at run time, so a 0.1 DLL (or header) still works and just skips those rows.
struct GateTuning {
    float strength = 1.0F;
    float highBand = 0.0F;
};

struct GateApi {
    HMODULE module = nullptr;
    VgConfig (*defaultConfig)(std::uint32_t) = nullptr;
    VoiceGate* (*create)(const VgConfig*) = nullptr;
    void (*destroy)(VoiceGate*) = nullptr;
    int (*process)(VoiceGate*, const float*, float*, std::size_t) = nullptr;
    std::uint32_t (*latency)(const VoiceGate*) = nullptr;
    void (*setHighBand)(VoiceGate*, float) = nullptr;

    bool load(const std::string& path) {
        module = LoadLibraryA(path.c_str());  // argv is in the ANSI code page, so is this
        if (!module) return false;
        defaultConfig = reinterpret_cast<decltype(defaultConfig)>(GetProcAddress(module, "vg_default_config"));
        create = reinterpret_cast<decltype(create)>(GetProcAddress(module, "vg_create"));
        destroy = reinterpret_cast<decltype(destroy)>(GetProcAddress(module, "vg_destroy"));
        process = reinterpret_cast<decltype(process)>(GetProcAddress(module, "vg_process"));
        latency = reinterpret_cast<decltype(latency)>(GetProcAddress(module, "vg_latency_samples"));
        setHighBand = reinterpret_cast<decltype(setHighBand)>(GetProcAddress(module, "vg_set_high_band"));
        return defaultConfig && create && destroy && process && latency;
    }

    bool run(int mode, std::uint32_t rate, const std::vector<float>& input, std::vector<float>& output,
             std::uint32_t& delay, const GateTuning& tuning = {}) const {
        auto config = defaultConfig(rate);
        config.mode = mode;
        config.strength = tuning.strength;
        VoiceGate* gate = create(&config);
        if (!gate) return false;
        if (setHighBand) setHighBand(gate, tuning.highBand);
        delay = latency(gate);
        output = input;
        const std::size_t block = rate / 100U;
        bool ok = true;
        for (std::size_t position = 0; ok && position < output.size(); position += block) {
            const auto count = std::min(block, output.size() - position);
            ok = process(gate, output.data() + position, output.data() + position, count) == 0;
        }
        destroy(gate);
        return ok;
    }
};
#endif

std::vector<std::int8_t> translate(const std::vector<float>& input, const Apex6TranslatorConfig& config) {
    Apex6AudioTranslator translator(config);
    std::vector<std::int8_t> output;
    translator.process(input.data(), input.size(), output);
    return output;
}

std::vector<float> delayed(const std::vector<float>& input, std::size_t delay) {
    std::vector<float> result(input.size(), 0.0F);
    for (std::size_t index = delay; index < input.size(); ++index) result[index] = input[index - delay];
    return result;
}

// Moving RMS over 20 output samples (20 ms), centred: how strong the pad is driven,
// without the 60/90/170 Hz carriers' phase. Carrier phase depends on when a band last
// woke up, so two outputs that feel the same can still differ sample by sample.
std::vector<double> envelope(const std::vector<std::int8_t>& values) {
    constexpr std::size_t kWidth = 20;
    std::vector<double> result(values.size(), 0.0);
    double sum = 0.0;
    for (std::size_t index = 0; index < values.size() + kWidth / 2; ++index) {
        if (index < values.size()) sum += static_cast<double>(values[index]) * values[index];
        if (index >= kWidth) {
            const auto old = values[index - kWidth];
            sum -= static_cast<double>(old) * old;
        }
        if (index >= kWidth / 2) result[index - kWidth / 2] = std::sqrt(std::max(sum, 0.0) / kWidth);
    }
    return result;
}

// 100 ms windows of the voice: speech, clear (no voice and not right after it), or neither.
enum class Window : std::int8_t { Clear, Voice, Skip };

struct Residual {
    double energy = 0.0;
    double reference = 0.0;

    void add(double difference, double expected) {
        energy += difference * difference;
        reference += expected * expected;
    }

    [[nodiscard]] double decibels() const {
        if (reference <= 0.0 || energy <= 0.0) return -120.0;
        return 10.0 * std::log10(energy / reference);
    }

    // "n/a" when no window fell in this class (e.g. no clear windows at all)
    [[nodiscard]] std::string text() const {
        if (reference <= 0.0) return "   n/a";
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%6.1f", decibels());
        return buffer;
    }
};

struct Row {
    std::string name;
    std::uint32_t delay = 0;
    std::vector<std::vector<std::int8_t>> output;
};

void usage() {
    std::puts("usage: ApexSenseBridgeHapticTranslateBench <input.wav> [--reference sfx_only.wav]\n"
              "         [--voice-gate <voice_gate.dll>] [--drive <linear>] [--csv <out.csv>]\n"
              "         [--delay <host samples>] [--voice-source <mix.wav>]\n"
              "With voice_gate 0.2 the gate also runs as separate+hb (high band on) and separate+hb+s1.5\n"
              "(and strength 1.5). CSV columns are aligned to the reference (gate delay removed).");
}

} // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string referencePath;
    std::string gatePath;
    std::string csvPath;
    std::string voicePath;
    std::uint32_t inputDelay = 0;
    Apex6TranslatorConfig config;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--reference" && hasValue) referencePath = argv[++index];
        else if (argument == "--voice-gate" && hasValue) gatePath = argv[++index];
        else if (argument == "--csv" && hasValue) csvPath = argv[++index];
        else if (argument == "--voice-source" && hasValue) voicePath = argv[++index];
        else if (argument == "--delay" && hasValue) inputDelay = static_cast<std::uint32_t>(std::atoi(argv[++index]));
        else if (argument == "--drive" && hasValue) config.drive = std::atof(argv[++index]);
        else if (inputPath.empty() && argument.rfind("--", 0) != 0) inputPath = argument;
        else {
            usage();
            return 2;
        }
    }
    if (inputPath.empty()) {
        usage();
        return 2;
    }

    Wav input;
    std::string error;
    if (!loadWav(inputPath, input, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    config.sampleRate = static_cast<double>(input.rate);
    if (!Apex6AudioTranslator(config).valid()) {
        std::fprintf(stderr, "sample rate %u Hz is not a multiple of 1000\n", input.rate);
        return 1;
    }
    Wav reference;
    if (!referencePath.empty()) {
        if (!loadWav(referencePath, reference, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (reference.rate != input.rate || reference.channels.size() != input.channels.size() ||
            reference.channels[0].size() != input.channels[0].size()) {
            std::fprintf(stderr, "reference must match the input rate, channels and length\n");
            return 1;
        }
    }

    Wav voiceWav;
    if (!voicePath.empty()) {
        if (!loadWav(voicePath, voiceWav, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (voiceWav.rate != input.rate || voiceWav.channels.size() != input.channels.size() ||
            voiceWav.channels[0].size() != input.channels[0].size()) {
            std::fprintf(stderr, "voice source must match the input rate, channels and length\n");
            return 1;
        }
    }
    const Wav& voiceSource = voicePath.empty() ? input : voiceWav;

    std::vector<Row> rows;
    Row off;
    off.name = inputDelay != 0 ? "as-given" : "off";
    off.delay = inputDelay;
    for (const auto& channel : input.channels) off.output.push_back(translate(channel, config));
    rows.push_back(std::move(off));

    if (!gatePath.empty()) {
#ifdef ASB_BENCH_VOICE_GATE
        GateApi gate;
        if (!gate.load(gatePath)) {
            std::fprintf(stderr, "cannot load voice gate from %s\n", gatePath.c_str());
            return 1;
        }
        struct GateRow {
            const char* name;
            int mode;
            GateTuning tuning;
        };
        std::vector<GateRow> modes = {{"duck", VG_MODE_DUCK, {}},
                                      {"separate", VG_MODE_SEPARATE, {}},
                                      {"separate+always", VG_MODE_SEPARATE_ALWAYS, {}}};
        if (gate.setHighBand) {
            modes.push_back({"separate+hb", VG_MODE_SEPARATE, {1.0F, 1.0F}});
            modes.push_back({"separate+hb+s1.5", VG_MODE_SEPARATE, {1.5F, 1.0F}});
        } else {
            std::printf("voice gate has no vg_set_high_band (0.1): no separate+hb rows\n");
        }
        for (const auto& mode : modes) {
            Row row;
            row.name = mode.name;
            // one gate per channel: game speech is often panned, and a mid-only or shared
            // decision measured worse for panned lines
            for (const auto& channel : input.channels) {
                std::vector<float> filtered;
                if (!gate.run(mode.mode, input.rate, channel, filtered, row.delay, mode.tuning)) {
                    std::fprintf(stderr, "voice gate failed in mode %s\n", mode.name);
                    return 1;
                }
                row.output.push_back(translate(filtered, config));
            }
            rows.push_back(std::move(row));
        }
#else
        std::fprintf(stderr, "built without voice gate support (set ASB_VOICE_GATE_DIR)\n");
        return 1;
#endif
    }

    std::printf("input %s: %u Hz, %zu channel(s), %.1f s -> %zu samples at 1 kHz\n", inputPath.c_str(),
                input.rate, input.channels.size(),
                static_cast<double>(input.channels[0].size()) / input.rate, rows[0].output[0].size());

    const bool haveReference = !referencePath.empty();
    std::vector<std::vector<std::vector<std::int8_t>>> aligned;
    std::vector<std::vector<std::vector<Window>>> voiceWindows;
    if (haveReference) {
        for (const auto& row : rows) {
            std::vector<std::vector<std::int8_t>> perChannel;
            std::vector<std::vector<Window>> perWindows;
            for (std::size_t channel = 0; channel < input.channels.size(); ++channel) {
                const auto shifted = delayed(reference.channels[channel], row.delay);
                perChannel.push_back(translate(shifted, config));
                std::vector<float> voice(input.channels[channel].size());
                for (std::size_t sample = 0; sample < voice.size(); ++sample) {
                    voice[sample] = voiceSource.channels[channel][sample] - reference.channels[channel][sample];
                }
                const auto voiceShifted = delayed(voice, row.delay);
                // voice: RMS > 0.01. clear: RMS < 0.001 and at least 300 ms after the last voice
                // window (quiet speech tails, the gate's hangover and the translator's release
                // otherwise land in "no voice"). Windows in between are left out of both.
                const std::size_t window = input.rate / 10U;
                constexpr std::size_t kSettleWindows = 3;
                std::vector<Window> flags;
                std::size_t sinceVoice = kSettleWindows + 1;
                for (std::size_t start = 0; start + window <= voiceShifted.size(); start += window) {
                    double energy = 0.0;
                    for (std::size_t sample = start; sample < start + window; ++sample) {
                        energy += static_cast<double>(voiceShifted[sample]) * voiceShifted[sample];
                    }
                    const double level = std::sqrt(energy / static_cast<double>(window));
                    sinceVoice = level > 0.01 ? 0 : sinceVoice + 1;
                    if (level > 0.01) flags.push_back(Window::Voice);
                    else if (level < 0.001 && sinceVoice > kSettleWindows) flags.push_back(Window::Clear);
                    else flags.push_back(Window::Skip);
                }
                perWindows.push_back(std::move(flags));
            }
            aligned.push_back(std::move(perChannel));
            voiceWindows.push_back(std::move(perWindows));
        }
        std::printf("residual = energy of (translated output - translated sfx-only reference) relative to the reference, in dB; lower is better\n"
                    "  sample: sample by sample; 20 ms env: on 20 ms RMS envelopes (what the pad's strength follows, carrier phase ignored)\n"
                    "  voice: 100 ms windows with voice RMS > 0.01; clear: voice RMS < 0.001 and 300 ms past the last voice window\n");
        std::printf("%-17s %7s %7s %6s | %-26s | %-26s\n", "mode", "delay", "rms", "peak", "sample: all / voice / clear",
                    "20 ms env: all / voice / clear");
    } else {
        std::printf("%-17s %7s %7s %6s\n", "mode", "delay", "rms", "peak");
    }

    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        double sumSquares = 0.0;
        std::size_t count = 0;
        int peak = 0;
        Residual all, voice, clear;           // sample by sample
        Residual allEnv, voiceEnv, clearEnv;  // 20 ms envelopes
        for (std::size_t channel = 0; channel < row.output.size(); ++channel) {
            const auto& out = row.output[channel];
            for (const auto value : out) {
                sumSquares += static_cast<double>(value) * value;
                peak = std::max(peak, std::abs(static_cast<int>(value)));
            }
            count += out.size();
            if (!haveReference) continue;
            const auto& expected = aligned[rowIndex][channel];
            const auto& flags = voiceWindows[rowIndex][channel];
            const auto outEnv = envelope(out);
            const auto expectedEnv = envelope(expected);
            for (std::size_t sample = 0; sample < out.size() && sample < expected.size(); ++sample) {
                const double difference = static_cast<double>(out[sample]) - expected[sample];
                const double envDifference = outEnv[sample] - expectedEnv[sample];
                all.add(difference, expected[sample]);
                allEnv.add(envDifference, expectedEnv[sample]);
                const auto window = sample / 100U;
                if (window >= flags.size() || flags[window] == Window::Skip) continue;
                const bool isVoice = flags[window] == Window::Voice;
                (isVoice ? voice : clear).add(difference, expected[sample]);
                (isVoice ? voiceEnv : clearEnv).add(envDifference, expectedEnv[sample]);
            }
        }
        const double rmsValue = count ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0;
        if (haveReference) {
            std::printf("%-17s %5u ms %7.2f %6d | %s %s %s dB      | %s %s %s dB\n", row.name.c_str(),
                        row.delay * 1000U / input.rate, rmsValue, peak, all.text().c_str(), voice.text().c_str(),
                        clear.text().c_str(), allEnv.text().c_str(), voiceEnv.text().c_str(),
                        clearEnv.text().c_str());
        } else {
            std::printf("%-17s %5u ms %7.2f %6d\n", row.name.c_str(), row.delay * 1000U / input.rate, rmsValue, peak);
        }
    }

    if (!csvPath.empty()) {
        std::ofstream csv(csvPath);
        csv << "ms";
        for (const auto& row : rows) csv << ',' << row.name;
        if (haveReference) csv << ",reference";
        csv << '\n';
        // every column on the reference's time base: a row whose path is longer (the gate's
        // latency) is read that many milliseconds later
        for (std::size_t sample = 0; sample < rows[0].output[0].size(); ++sample) {
            csv << sample;
            for (const auto& row : rows) {
                const auto shift = (static_cast<long long>(row.delay) - static_cast<long long>(rows[0].delay)) *
                                   1000LL / static_cast<long long>(input.rate);
                const auto at = static_cast<long long>(sample) + shift;
                const bool inside = at >= 0 && at < static_cast<long long>(row.output[0].size());
                csv << ',' << (inside ? static_cast<int>(row.output[0][static_cast<std::size_t>(at)]) : 0);
            }
            if (haveReference) {
                csv << ',' << (sample < aligned[0][0].size() ? static_cast<int>(aligned[0][0][sample]) : 0);
            }
            csv << '\n';
        }
    }
    return 0;
}
