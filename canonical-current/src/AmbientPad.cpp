#include "j3/AmbientPad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace j3 {

void AmbientPad::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(8000.0, sampleRate);
    phase_.fill(0.0);
    lfoPhase_ = 0.0;
    smoothedGain_ = 0.0f;
}

void AmbientPad::setRootMidi(int note) noexcept
{
    rootMidi_.store(std::clamp(note, 36, 84), std::memory_order_release);
}

void AmbientPad::setVolume(float volume) noexcept
{
    volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_release);
}

void AmbientPad::process(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || (left == nullptr && right == nullptr))
        return;

    const bool minor = minor_.load(std::memory_order_acquire);
    const int root = rootMidi_.load(std::memory_order_acquire);
    const float target = enabled_.load(std::memory_order_acquire)
        ? volume_.load(std::memory_order_relaxed) : 0.0f;

    const std::array<int, 6> semitones { 0, minor ? 3 : 4, 7, 12, 19, 24 };
    std::array<double, 6> inc {};
    for (std::size_t v = 0; v < inc.size(); ++v)
    {
        const double midi = static_cast<double>(root + semitones[v]);
        const double hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        const double detune = (v % 2 == 0 ? -0.0015 : 0.0015);
        inc[v] = 2.0 * std::numbers::pi_v<double> * hz * (1.0 + detune) / sampleRate_;
    }

    constexpr std::array<float, 6> leftWeight  { 0.85f, 0.25f, 0.72f, 0.35f, 0.64f, 0.42f };
    constexpr std::array<float, 6> rightWeight { 0.25f, 0.85f, 0.38f, 0.76f, 0.42f, 0.66f };
    const double lfoInc = 2.0 * std::numbers::pi_v<double> * 0.065 / sampleRate_;

    for (int i = 0; i < numSamples; ++i)
    {
        const float ramp = target > smoothedGain_ ? 0.0007f : 0.0012f;
        smoothedGain_ += (target - smoothedGain_) * ramp;
        const float breathe = 0.86f + 0.14f * static_cast<float>(std::sin(lfoPhase_));

        float l = 0.0f;
        float r = 0.0f;
        for (std::size_t v = 0; v < phase_.size(); ++v)
        {
            const float voice = static_cast<float>(std::sin(phase_[v])) * 0.18f;
            l += voice * leftWeight[v];
            r += voice * rightWeight[v];
            phase_[v] += inc[v];
            if (phase_[v] >= 2.0 * std::numbers::pi_v<double>)
                phase_[v] -= 2.0 * std::numbers::pi_v<double>;
        }

        const float gain = smoothedGain_ * breathe;
        if (left != nullptr) left[i] += l * gain;
        if (right != nullptr) right[i] += r * gain;

        lfoPhase_ += lfoInc;
        if (lfoPhase_ >= 2.0 * std::numbers::pi_v<double>)
            lfoPhase_ -= 2.0 * std::numbers::pi_v<double>;
    }
}

}
