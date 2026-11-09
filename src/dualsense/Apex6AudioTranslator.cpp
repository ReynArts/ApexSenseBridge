#include "dualsense/Apex6AudioTranslator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace asb::dualsense {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kButterworth4Q1 = 0.54119610014619698;
constexpr double kButterworth4Q2 = 1.30656296487637653;
constexpr std::array<double, 4> kButterworth8Q{
    0.50979557910415917, 0.60134488693504528, 0.89997622313641570, 2.56291544774150617};
constexpr double kOutputRate = 1000.0;
constexpr double kPunchPhase = 0.25;

double decibelsToGain(double decibels) noexcept {
    return std::pow(10.0, decibels / 20.0);
}

double followerCoefficient(double milliseconds, double sampleRate) noexcept {
    if (milliseconds <= 0.0) return 1.0;
    return 1.0 - std::exp(-1.0 / (milliseconds * 0.001 * sampleRate));
}

} // namespace

Apex6AudioTranslator::Biquad Apex6AudioTranslator::makeLowPass(
    double sampleRate, double frequency, double q) noexcept {
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad result;
    result.b0 = (1.0 - cosine) / 2.0 / a0;
    result.b1 = (1.0 - cosine) / a0;
    result.b2 = result.b0;
    result.a1 = -2.0 * cosine / a0;
    result.a2 = (1.0 - alpha) / a0;
    return result;
}

Apex6AudioTranslator::Biquad Apex6AudioTranslator::makeHighPass(
    double sampleRate, double frequency, double q) noexcept {
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad result;
    result.b0 = (1.0 + cosine) / 2.0 / a0;
    result.b1 = -(1.0 + cosine) / a0;
    result.b2 = result.b0;
    result.a1 = -2.0 * cosine / a0;
    result.a2 = (1.0 - alpha) / a0;
    return result;
}

Apex6AudioTranslator::Apex6AudioTranslator(const Apex6TranslatorConfig& config)
    : config_(config) {
    const double rate = config_.sampleRate;
    const double ratio = rate / kOutputRate;
    if (!(rate > 0.0) || ratio < 1.0 || std::fabs(ratio - std::round(ratio)) > 1e-9) return;
    decimation_ = static_cast<std::size_t>(std::llround(ratio));
    for (std::size_t stage = 0; stage < low_.size(); ++stage) {
        low_[stage] = makeLowPass(rate, config_.lowPassHz, kButterworth8Q[stage]);
    }
    lowGain_ = decibelsToGain(config_.lowGainDb);
    attackCoefficient_ = followerCoefficient(config_.attackMs, rate);
    for (std::size_t index = 0; index < bands_.size(); ++index) {
        const auto& spec = config_.bands[index];
        auto& band = bands_[index];
        band.highPass[0] = makeHighPass(rate, spec.lowHz, kButterworth4Q1);
        band.highPass[1] = makeHighPass(rate, spec.lowHz, kButterworth4Q2);
        band.useLowPass = spec.highHz < rate * 0.45;
        if (band.useLowPass) {
            band.lowPass[0] = makeLowPass(rate, spec.highHz, kButterworth4Q1);
            band.lowPass[1] = makeLowPass(rate, spec.highHz, kButterworth4Q2);
        }
        band.releaseCoefficient = followerCoefficient(spec.releaseMs, rate);
        band.gain = decibelsToGain(spec.gainDb);
        band.carrierStep = spec.carrierHz / kOutputRate;
    }
}

void Apex6AudioTranslator::reset() noexcept {
    counter_ = 0;
    for (auto& biquad : low_) biquad.z1 = biquad.z2 = 0.0;
    for (auto& band : bands_) {
        for (auto& biquad : band.highPass) biquad.z1 = biquad.z2 = 0.0;
        for (auto& biquad : band.lowPass) biquad.z1 = biquad.z2 = 0.0;
        band.envelope = 0.0;
        band.phase = 0.0;
        band.active = false;
    }
}

double Apex6AudioTranslator::emit() noexcept {
    double sum = 0.0;
    for (auto& band : bands_) {
        const double level = band.envelope * config_.envelopeScale;
        if (config_.punchPhaseReset) {
            if (!band.active && level >= config_.punchThreshold) {
                band.phase = kPunchPhase;
                band.active = true;
            } else if (band.active && level < config_.punchThreshold * 0.5) {
                band.active = false;
            }
        }
        sum += level * band.gain * std::sin(2.0 * kPi * band.phase);
        band.phase += band.carrierStep;
        band.phase -= std::floor(band.phase);
    }
    return sum;
}

void Apex6AudioTranslator::render(
    const float* input, std::size_t count, std::vector<double>& output) {
    if (!valid()) return;
    output.reserve(output.size() + count / decimation_ + 1);
    double lowValue = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(input[index]);
        lowValue = x;
        for (auto& biquad : low_) lowValue = biquad.step(lowValue);
        for (auto& band : bands_) {
            double filtered = band.highPass[1].step(band.highPass[0].step(x));
            if (band.useLowPass) filtered = band.lowPass[1].step(band.lowPass[0].step(filtered));
            const double rectified = std::fabs(filtered);
            const double coefficient =
                rectified > band.envelope ? attackCoefficient_ : band.releaseCoefficient;
            band.envelope += coefficient * (rectified - band.envelope);
        }
        if (++counter_ == decimation_) {
            counter_ = 0;
            output.push_back(lowValue * lowGain_ + emit());
        }
    }
}

std::int8_t Apex6AudioTranslator::quantize(double value) const noexcept {
    const double limited = std::tanh(value * config_.drive) * config_.outputPeak;
    const double rounded = std::round(limited);
    return static_cast<std::int8_t>(std::clamp(rounded, -127.0, 127.0));
}

void Apex6AudioTranslator::process(
    const float* input, std::size_t count, std::vector<std::int8_t>& output) {
    std::vector<double> raw;
    render(input, count, raw);
    output.reserve(output.size() + raw.size());
    for (const double value : raw) output.push_back(quantize(value));
}

} // namespace asb::dualsense
