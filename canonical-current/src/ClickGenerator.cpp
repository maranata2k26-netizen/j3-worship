#include "j3/ClickGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace j3 {
void ClickGenerator::prepare(double sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0, 384000.0);
    reset();
}

void ClickGenerator::reset() noexcept {
    phase_ = 0.0;
    samplesUntilPulse_ = 0.0;
    pulseSamplesRemaining_ = 0;
    pulseCounter_ = 0;
    beatCounter_.store(0, std::memory_order_relaxed);
}

void ClickGenerator::setTempo(double bpm) noexcept {
    bpm_.store(std::clamp(bpm, 20.0, 400.0), std::memory_order_relaxed);
}

void ClickGenerator::setTimeSignature(int numerator, int denominator) noexcept {
    numerator_.store(std::clamp(numerator, 1, 16), std::memory_order_relaxed);
    const int d = denominator == 2 || denominator == 8 || denominator == 16 ? denominator : 4;
    denominator_.store(d, std::memory_order_relaxed);
}

void ClickGenerator::setSubdivision(int pulsesPerBeat) noexcept {
    const int v = pulsesPerBeat >= 4 ? 4 : (pulsesPerBeat >= 2 ? 2 : 1);
    subdivision_.store(v, std::memory_order_relaxed);
}

void ClickGenerator::setLevel(float level) noexcept {
    level_.store(std::clamp(level, 0.0f, 1.0f), std::memory_order_relaxed);
}

int ClickGenerator::process(float* output, int numSamples) noexcept {
    if (numSamples <= 0 || !enabled_.load(std::memory_order_acquire))
        return 0;

    const auto bpm = bpm_.load(std::memory_order_relaxed);
    const auto subdivision = subdivision_.load(std::memory_order_relaxed);
    const auto numerator = numerator_.load(std::memory_order_relaxed);
    const auto level = level_.load(std::memory_order_relaxed);
    const auto accentEnabled = accentEnabled_.load(std::memory_order_relaxed);
    const double samplesPerPulse = sampleRate_ * 60.0 / (bpm * static_cast<double>(subdivision));
    const int pulseLength = std::max(16, static_cast<int>(sampleRate_ * 0.022));
    int beatEvents = 0;

    for (int i = 0; i < numSamples; ++i) {
        if (samplesUntilPulse_ <= 0.0) {
            const bool beatBoundary = (pulseCounter_ % static_cast<std::uint64_t>(subdivision)) == 0;
            if (beatBoundary) {
                beatCounter_.fetch_add(1, std::memory_order_relaxed);
                ++beatEvents;
            }
            pulseSamplesRemaining_ = pulseLength;
            phase_ = 0.0;
            samplesUntilPulse_ += samplesPerPulse;
        }

        if (pulseSamplesRemaining_ > 0) {
            const auto beatIndex = beatCounter_.load(std::memory_order_relaxed);
            const bool downbeat = accentEnabled && beatIndex > 0 && ((beatIndex - 1) % static_cast<std::uint64_t>(numerator) == 0);
            const double frequency = downbeat ? 1760.0 : 1100.0;
            const double env = static_cast<double>(pulseSamplesRemaining_) / static_cast<double>(pulseLength);
            const double shaped = env * env;
            if (output != nullptr)
                output[i] += static_cast<float>(std::sin(phase_) * shaped * level);
            phase_ += 2.0 * std::numbers::pi_v<double> * frequency / sampleRate_;
            --pulseSamplesRemaining_;
        }

        samplesUntilPulse_ -= 1.0;
        if (samplesUntilPulse_ <= 0.0) {
            ++pulseCounter_;
        }
    }
    return beatEvents;
}
}
