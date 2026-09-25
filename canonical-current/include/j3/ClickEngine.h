#pragma once
#include <cstdint>
#include <stdexcept>
namespace j3 {
class ClickEngine {
public:
    void setTempo(double bpm); void setTimeSignature(int numerator, int denominator);
    double bpm() const noexcept{return bpm_;} int numerator() const noexcept{return numerator_;}
    double secondsPerBeat() const noexcept { return 60.0/bpm_; }
    bool isAccent(std::uint64_t beatIndex) const noexcept { return beatIndex%static_cast<std::uint64_t>(numerator_)==0; }
private: double bpm_{120}; int numerator_{4}; int denominator_{4};
};
}
