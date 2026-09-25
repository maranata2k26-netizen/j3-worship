#pragma once
#include "j3/Types.h"
#include <optional>
#include <string>

namespace j3 {
struct Section { std::string name; SectionKind kind{SectionKind::Custom}; int bars{4}; };
class LiveEngine {
public:
    void setTempo(double bpm, int beatsPerBar=4);
    void start(Section s);
    void stop();
    void request(Section next, Quantize q);
    void tickBeat();
    void enterFreePad();
    std::string now() const; std::string next() const;
    std::uint64_t beatCounter() const noexcept { return beatCounter_; }
private:
    bool boundaryReached() const;
    double bpm_{120}; int beatsPerBar_{4}; std::uint64_t beatCounter_{0};
    Section current_{"STOPPED",SectionKind::Custom,4};
    std::optional<Section> queued_{}; Quantize quantize_{Quantize::EndOfSection}; bool playing_{false};
};
}
