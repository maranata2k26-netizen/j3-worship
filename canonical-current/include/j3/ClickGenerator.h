#pragma once

#include <atomic>
#include <cstdint>

namespace j3 {
class ClickGenerator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_release); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setTempo(double bpm) noexcept;
    void setTimeSignature(int numerator, int denominator) noexcept;
    void setSubdivision(int pulsesPerBeat) noexcept;
    void setAccentEnabled(bool enabled) noexcept { accentEnabled_.store(enabled, std::memory_order_release); }
    void setLevel(float level) noexcept;
    double tempo() const noexcept { return bpm_.load(std::memory_order_relaxed); }
    int numerator() const noexcept { return numerator_.load(std::memory_order_relaxed); }
    int denominator() const noexcept { return denominator_.load(std::memory_order_relaxed); }
    int subdivision() const noexcept { return subdivision_.load(std::memory_order_relaxed); }
    std::uint64_t beatCounter() const noexcept { return beatCounter_.load(std::memory_order_relaxed); }

    // Advances the transport clock, optionally adds click signal to output, and returns quarter-note beat boundaries crossed.
    // Passing output == nullptr advances timing silently.
    int process(float* output, int numSamples) noexcept;

private:
    double sampleRate_ { 48000.0 };
    double phase_ { 0.0 };
    double samplesUntilPulse_ { 0.0 };
    int pulseSamplesRemaining_ { 0 };
    std::uint64_t pulseCounter_ { 0 };
    std::atomic<std::uint64_t> beatCounter_ { 0 };
    std::atomic<double> bpm_ { 120.0 };
    std::atomic<int> numerator_ { 4 };
    std::atomic<int> denominator_ { 4 };
    std::atomic<int> subdivision_ { 1 };
    std::atomic<float> level_ { 0.35f };
    std::atomic<bool> enabled_ { false };
    std::atomic<bool> accentEnabled_ { true };
};
}
