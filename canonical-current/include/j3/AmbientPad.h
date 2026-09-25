#pragma once

#include <array>
#include <atomic>

namespace j3 {

class AmbientPad
{
public:
    void prepare(double sampleRate) noexcept;
    void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_release); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setRootMidi(int note) noexcept;
    int rootMidi() const noexcept { return rootMidi_.load(std::memory_order_relaxed); }
    void setMinor(bool minor) noexcept { minor_.store(minor, std::memory_order_release); }
    bool minor() const noexcept { return minor_.load(std::memory_order_acquire); }
    void setVolume(float volume) noexcept;
    float volume() const noexcept { return volume_.load(std::memory_order_relaxed); }

    // Adds a click-free, continuously sustained stereo pad to the supplied buffers.
    // Buffers may be null independently.
    void process(float* left, float* right, int numSamples) noexcept;

private:
    double sampleRate_ { 48000.0 };
    std::array<double, 6> phase_ {};
    double lfoPhase_ { 0.0 };
    float smoothedGain_ { 0.0f };
    std::atomic<bool> enabled_ { false };
    std::atomic<int> rootMidi_ { 60 };
    std::atomic<bool> minor_ { false };
    std::atomic<float> volume_ { 0.18f };
};

}
