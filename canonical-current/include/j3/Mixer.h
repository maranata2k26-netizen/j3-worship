#pragma once
#include "j3/Types.h"
#include <string>
#include <vector>

namespace j3 {
struct EqBand { double freq{1000.0}; double gainDb{0.0}; double q{1.0}; bool enabled{true}; };
struct ChannelStrip {
    std::string name{"Channel"}; ChannelRole role{ChannelRole::Input};
    double trimDb{0.0}; bool phaseInvert{false}; double hpfHz{20.0}; double lpfHz{20000.0};
    EqBand eq[4]{}; double gateThresholdDb{-60.0}; double compressorThresholdDb{-18.0};
    double denoiseAmount{0.0}; double pan{0.0}; bool mute{false}; bool solo{false}; double faderDb{0.0};
};
class Mixer {
public:
    std::size_t addChannel(ChannelStrip c);
    ChannelStrip& channel(std::size_t i) { return channels_.at(i); }
    const ChannelStrip& channel(std::size_t i) const { return channels_.at(i); }
    std::size_t size() const noexcept { return channels_.size(); }
    void createDrumPreset(bool audiencePerspective=false);
    void createVocalPreset(int leads, int bgv);
private: std::vector<ChannelStrip> channels_;
};
}
