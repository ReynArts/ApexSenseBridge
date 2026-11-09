#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace asb::dualsense {

struct Apex6TranslatorBand {
    double lowHz;
    double highHz;
    double carrierHz;
    double gainDb;
    double releaseMs;
};

struct Apex6TranslatorConfig {
    double sampleRate = 48000.0;
    double lowPassHz = 220.0;
    double lowGainDb = 0.0;
    double attackMs = 1.0;
    double envelopeScale = 1.0;
    std::array<Apex6TranslatorBand, 3> bands{{
        {1000.0, 3000.0, 60.0, 6.0, 20.0},
        {3000.0, 6000.0, 90.0, 8.0, 10.0},
        {6000.0, 12000.0, 170.0, 10.0, 5.0},
    }};
    bool punchPhaseReset = true;
    double punchThreshold = 0.02;
    double drive = 1.0;
    double outputPeak = 112.0;
};

class Apex6AudioTranslator {
public:
    explicit Apex6AudioTranslator(const Apex6TranslatorConfig& config = {});

    [[nodiscard]] bool valid() const noexcept { return decimation_ != 0; }
    [[nodiscard]] std::size_t decimation() const noexcept { return decimation_; }
    [[nodiscard]] const Apex6TranslatorConfig& config() const noexcept { return config_; }

    void render(const float* input, std::size_t count, std::vector<double>& output);
    void process(const float* input, std::size_t count, std::vector<std::int8_t>& output);
    [[nodiscard]] std::int8_t quantize(double value) const noexcept;
    void reset() noexcept;

private:
    struct Biquad {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double z1 = 0.0;
        double z2 = 0.0;

        double step(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Band {
        std::array<Biquad, 2> highPass{};
        std::array<Biquad, 2> lowPass{};
        bool useLowPass = true;
        double releaseCoefficient = 0.0;
        double gain = 1.0;
        double carrierStep = 0.0;
        double envelope = 0.0;
        double phase = 0.0;
        bool active = false;
    };

    static Biquad makeLowPass(double sampleRate, double frequency, double q) noexcept;
    static Biquad makeHighPass(double sampleRate, double frequency, double q) noexcept;
    [[nodiscard]] double emit() noexcept;

    Apex6TranslatorConfig config_;
    std::size_t decimation_ = 0;
    std::size_t counter_ = 0;
    std::array<Biquad, 4> low_{};
    double lowGain_ = 1.0;
    double attackCoefficient_ = 0.0;
    std::array<Band, 3> bands_{};
};

} // namespace asb::dualsense
