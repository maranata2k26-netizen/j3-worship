#pragma once
#include <cstdint>
#include <string>

namespace j3 {
enum class ChannelRole { Input, Bus, Aux, Master, Click, Guide, Pad, Track };
enum class Quantize { Beat, Bar, TwoBars, EndOfSection };
enum class SectionKind { Intro, Verse, PreChorus, Chorus, Bridge, Instrumental, FreePad, Ending, Custom };

enum class Health { Ok, Review, Error };

struct AudioConfig {
    double sampleRate {48000.0};
    std::uint32_t bufferFrames {256};
    std::uint32_t inputs {18};
    std::uint32_t outputs {18};
};

inline double estimatedRoundTripMs(const AudioConfig& c) {
    return (2.0 * static_cast<double>(c.bufferFrames) / c.sampleRate) * 1000.0;
}

struct SemVer {
    int major{}; int minor{}; int patch{};
    friend auto operator<=>(const SemVer&, const SemVer&) = default;
};
}
