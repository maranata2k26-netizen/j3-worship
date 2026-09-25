#include "j3/Multitrack.h"
#include <algorithm>
#include <cctype>

namespace j3 {
StemRole classifyStemName(std::string n) {
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n.find("click") != std::string::npos || n.find("metron") != std::string::npos) return StemRole::Click;
    if (n.find("guide") != std::string::npos || n.find("cue") != std::string::npos) return StemRole::Guide;
    if (n.find("drum") != std::string::npos || n.find("kick") != std::string::npos || n.find("snare") != std::string::npos) return StemRole::Drums;
    if (n.find("bass") != std::string::npos) return StemRole::Bass;
    if (n.find("guitar") != std::string::npos || n.find("gtr") != std::string::npos) return StemRole::Guitar;
    if (n.find("keys") != std::string::npos || n.find("piano") != std::string::npos) return StemRole::Keys;
    if (n.find("pad") != std::string::npos) return StemRole::Pad;
    if (n.find("string") != std::string::npos) return StemRole::Strings;
    if (n.find("synth") != std::string::npos) return StemRole::Synth;
    if (n.find("fx") != std::string::npos) return StemRole::Fx;
    if (n.find("vocal") != std::string::npos || n.find("bgv") != std::string::npos) return StemRole::Vocals;
    return StemRole::Other;
}

const char* stemRoleName(StemRole r) noexcept {
    switch (r) {
        case StemRole::Click: return "Click";
        case StemRole::Guide: return "Guide";
        case StemRole::Drums: return "Drums";
        case StemRole::Bass: return "Bass";
        case StemRole::Guitar: return "Guitar";
        case StemRole::Keys: return "Keys";
        case StemRole::Pad: return "Pad";
        case StemRole::Strings: return "Strings";
        case StemRole::Synth: return "Synth";
        case StemRole::Fx: return "FX";
        case StemRole::Vocals: return "Vocals";
        default: return "Other";
    }
}
}
