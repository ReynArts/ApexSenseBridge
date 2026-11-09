#ifdef NDEBUG
#undef NDEBUG
#endif

#include "dualsense/Apex6AudioTranslator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace asb::dualsense;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> tone(double frequency, double amplitude, double seconds, double startSeconds = 0.0,
                        double totalSeconds = 0.0) {
    const auto total = static_cast<std::size_t>((totalSeconds > 0.0 ? totalSeconds : seconds) * kRate);
    const auto start = static_cast<std::size_t>(startSeconds * kRate);
    const auto length = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> result(total, 0.0F);
    for (std::size_t index = 0; index < length && start + index < total; ++index) {
        result[start + index] = static_cast<float>(
            amplitude * std::sin(2.0 * kPi * frequency * static_cast<double>(index) / kRate));
    }
    return result;
}

std::vector<double> renderAll(const std::vector<float>& input, const Apex6TranslatorConfig& config = {}) {
    Apex6AudioTranslator translator(config);
    std::vector<double> output;
    translator.render(input.data(), input.size(), output);
    return output;
}

double goertzelPower(const std::vector<double>& samples, std::size_t from, double frequency) {
    const double w = 2.0 * kPi * frequency / 1000.0;
    const double coefficient = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t index = from; index < samples.size(); ++index) {
        const double s = samples[index] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double count = static_cast<double>(samples.size() - from);
    return (s1 * s1 + s2 * s2 - coefficient * s1 * s2) / (count * count);
}

double peakAfter(const std::vector<double>& samples, std::size_t from) {
    double peak = 0.0;
    for (std::size_t index = from; index < samples.size(); ++index) {
        peak = std::max(peak, std::fabs(samples[index]));
    }
    return peak;
}

void testRateValidation() {
    Apex6TranslatorConfig config;
    config.sampleRate = 44100.0;
    assert(!Apex6AudioTranslator(config).valid());
    config.sampleRate = 48000.0;
    assert(Apex6AudioTranslator(config).valid());
    assert(Apex6AudioTranslator(config).decimation() == 48);
    config.sampleRate = 96000.0;
    assert(Apex6AudioTranslator(config).decimation() == 96);
    config.sampleRate = 500.0;
    assert(!Apex6AudioTranslator(config).valid());
}

void testSilence() {
    const std::vector<float> silence(48000, 0.0F);
    const auto output = renderAll(silence);
    assert(output.size() == 1000);
    assert(std::all_of(output.begin(), output.end(), [](double value) { return value == 0.0; }));
}

void testStreamingMatchesOneShot() {
    auto input = tone(100.0, 0.4, 0.5);
    const auto burst = tone(2000.0, 0.3, 0.3, 0.1, 0.5);
    for (std::size_t index = 0; index < input.size(); ++index) input[index] += burst[index];

    const auto reference = renderAll(input);
    Apex6AudioTranslator translator;
    std::vector<double> chunked;
    const std::size_t sizes[] = {1, 47, 480, 777, 3, 4096, 13};
    std::size_t position = 0;
    std::size_t sizeIndex = 0;
    while (position < input.size()) {
        const auto size = std::min(sizes[sizeIndex++ % 7], input.size() - position);
        translator.render(input.data() + position, size, chunked);
        position += size;
    }
    assert(chunked.size() == reference.size());
    for (std::size_t index = 0; index < reference.size(); ++index) {
        assert(chunked[index] == reference[index]);
    }
}

void testLowBandPassesThrough() {
    const auto output = renderAll(tone(100.0, 0.5, 1.0));
    assert(output.size() == 1000);
    const double at100 = goertzelPower(output, 200, 100.0);
    const double at60 = goertzelPower(output, 200, 60.0);
    const double at170 = goertzelPower(output, 200, 170.0);
    const double peak = peakAfter(output, 200);
    std::fprintf(stderr, "low100: peak %.3f p100 %.4g p60 %.4g p170 %.4g\n", peak, at100, at60, at170);
    assert(at100 > 100.0 * at60);
    assert(at100 > 100.0 * at170);
    assert(peak > 0.4 && peak < 0.6);
}

struct BandCase {
    double frequency;
    double carrier;
    double gainDb;
};

void testHighBandsMoveToTheirCarrier() {
    const BandCase cases[] = {{1732.0, 60.0, 6.0}, {4243.0, 90.0, 8.0}, {8485.0, 170.0, 10.0}};
    const double carriers[] = {60.0, 90.0, 170.0};
    for (const auto& testCase : cases) {
        const auto output = renderAll(tone(testCase.frequency, 0.2, 1.0));
        const double expected = 0.2 * std::pow(10.0, testCase.gainDb / 20.0);
        const double peak = peakAfter(output, 200);
        std::fprintf(stderr, "band %.0f Hz: peak %.3f expected %.3f ratio %.3f\n", testCase.frequency,
                     peak, expected, peak / expected);
        double wanted = 0.0;
        double other = 0.0;
        for (const double carrier : carriers) {
            const double power = goertzelPower(output, 200, carrier);
            if (carrier == testCase.carrier) wanted = power;
            else other = std::max(other, power);
        }
        std::fprintf(stderr, "  carrier isolation %.1f dB\n", 10.0 * std::log10(wanted / other));
        assert(wanted > 6.0 * other);
        assert(goertzelPower(output, 200, 100.0) < wanted);
        assert(peak > expected * 0.8 && peak < expected * 1.2);
    }
}

void testGapBetweenPathsIsAttenuated() {
    const auto output = renderAll(tone(500.0, 0.5, 1.0));
    const double peak = peakAfter(output, 200);
    std::fprintf(stderr, "500 Hz gap tone: peak %.3f\n", peak);
    assert(peak < 0.15);
}

void testLowPathDoesNotAliasAboveNyquist() {
    // 1 kHz sampling folds a 550 Hz tone onto 450 Hz; the low path must stop it before sampling.
    const auto output = renderAll(tone(550.0, 0.5, 1.0));
    const double alias = std::sqrt(goertzelPower(output, 200, 450.0)) * 2.0;
    std::fprintf(stderr, "550 Hz tone: 450 Hz alias amplitude %.5f (%.1f dB re input)\n", alias,
                 20.0 * std::log10(alias / 0.5));
    assert(alias < 0.5 * std::pow(10.0, -55.0 / 20.0));
}

void testPunchPhaseReset() {
    const auto burst = tone(2000.0, 0.3, 0.3, 0.1, 0.4);
    Apex6TranslatorConfig on;
    Apex6TranslatorConfig off;
    off.punchPhaseReset = false;
    const auto withReset = renderAll(burst, on);
    const auto withoutReset = renderAll(burst, off);
    double firstOn = 0.0;
    double firstOff = 0.0;
    for (std::size_t index = 100; index < 130; ++index) {
        if (firstOn == 0.0 && std::fabs(withReset[index]) > 0.01) firstOn = withReset[index];
        if (firstOff == 0.0 && std::fabs(withoutReset[index]) > 0.01) firstOff = withoutReset[index];
    }
    std::fprintf(stderr, "punch first sample: reset %.3f without %.3f\n", firstOn, firstOff);
    assert(firstOn > 0.0);
    assert(std::fabs(firstOn) > std::fabs(firstOff));
}

void testReleaseFollowsBandSpeed() {
    const auto burst = tone(8000.0, 0.3, 0.2, 0.1, 0.5);
    const auto output = renderAll(burst);
    std::size_t last = 0;
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (std::fabs(output[index]) > 0.02) last = index;
    }
    std::fprintf(stderr, "8 kHz burst ends at 300 ms, output below 0.02 after %zu ms\n", last);
    assert(last >= 300 && last < 360);
}

void testQuantizerIsBoundedAndLinearWhenSmall() {
    Apex6AudioTranslator translator;
    assert(translator.quantize(0.0) == 0);
    assert(translator.quantize(0.01) == 1);
    assert(translator.quantize(-0.01) == -1);
    assert(translator.quantize(1000.0) == 112);
    assert(translator.quantize(-1000.0) == -112);
    assert(translator.quantize(0.5) > 0 && translator.quantize(0.5) < 112);

    std::vector<float> noise(48000 * 3);
    std::uint32_t state = 0x1234ABCDU;
    for (auto& sample : noise) {
        state = state * 1664525U + 1013904223U;
        sample = static_cast<float>(static_cast<std::int32_t>(state)) / 2147483648.0F;
    }
    std::vector<std::int8_t> quantized;
    translator.process(noise.data(), noise.size(), quantized);
    assert(quantized.size() == 3000);
    assert(std::all_of(quantized.begin(), quantized.end(),
                       [](std::int8_t value) { return value >= -112 && value <= 112; }));
}

void testDeterminism() {
    const auto input = tone(3000.0, 0.4, 0.4);
    assert(renderAll(input) == renderAll(input));
}

void testResetClearsState() {
    Apex6AudioTranslator translator;
    const auto input = tone(2000.0, 0.5, 0.2);
    std::vector<double> first;
    translator.render(input.data(), input.size(), first);
    translator.reset();
    std::vector<double> second;
    translator.render(input.data(), input.size(), second);
    assert(first == second);
}

} // namespace

int main() {
    testRateValidation();
    testSilence();
    testStreamingMatchesOneShot();
    testLowBandPassesThrough();
    testHighBandsMoveToTheirCarrier();
    testGapBetweenPathsIsAttenuated();
    testLowPathDoesNotAliasAboveNyquist();
    testPunchPhaseReset();
    testReleaseFollowsBandSpeed();
    testQuantizerIsBoundedAndLinearWhenSmall();
    testDeterminism();
    testResetClearsState();
    std::puts("apex6 audio translator tests passed");
    return 0;
}
