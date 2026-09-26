#pragma once
#include <array>
#include <cstddef>
#include <cmath>

namespace j3 {
class Biquad {
public:
    void reset() noexcept { z1_=z2_=0.0; }
    void setLowPass(double sr,double hz,double q=0.70710678) noexcept;
    void setHighPass(double sr,double hz,double q=0.70710678) noexcept;
    void setPeak(double sr,double hz,double q,double gainDb) noexcept;
    float process(float x) noexcept;
private:
    double b0_{1},b1_{0},b2_{0},a1_{0},a2_{0},z1_{0},z2_{0};
    void normalize(double b0,double b1,double b2,double a0,double a1,double a2) noexcept;
};

class ParametricEq {
public:
    void prepare(double sampleRate) noexcept;
    void setHpf(double hz) noexcept;
    void setLpf(double hz) noexcept;
    void setBand(std::size_t i,double hz,double q,double gainDb) noexcept;
    float process(float x) noexcept;
private:
    double sr_{48000};
    Biquad hpf_,lpf_;
    std::array<Biquad,4> eq_{};
};

class Gate {
public:
    void configure(double sr,double thresholdDb,double attackMs=2.0,double releaseMs=120.0) noexcept;
    float process(float x) noexcept;
private:
    double threshold_{0.001}; double env_{1.0}; double attack_{0.01}; double release_{0.001};
};

class Compressor {
public:
    void configure(double sr,double thresholdDb,double ratio=3.0,double attackMs=10.0,double releaseMs=100.0,double makeupDb=0.0) noexcept;
    float process(float x) noexcept;
private:
    double thresholdDb_{-18}; double ratio_{3}; double envDb_{0}; double attack_{0.01}; double release_{0.001}; double makeup_{1};
};

class Denoise {
public:
    void configure(double sr,double amount,double thresholdDb) noexcept;
    void learnNoise(float sample) noexcept;
    void finishLearning() noexcept;
    float process(float x) noexcept;
    void bypass(bool b) noexcept { bypass_=b; }
private:
    double threshold_{0.001}; double amount_{0}; double gain_{1}; double noiseSum_{0}; std::size_t noiseN_{0}; bool learning_{false}; bool bypass_{false};
};

class ChannelDsp {
public:
    void prepare(double sampleRate) noexcept;
    void setHpf(double hz) noexcept;
    void setLpf(double hz) noexcept;
    void setEqBand(std::size_t i,double hz,double q,double gainDb) noexcept;
    void setGate(double thresholdDb) noexcept;
    void setCompressor(double thresholdDb,double ratio=3.0) noexcept;
    void setDenoise(double amount,double thresholdDb) noexcept;
    float process(float x) noexcept;
private:
    double sr_{48000}; Biquad hpf_,lpf_; std::array<Biquad,4> eq_{}; Gate gate_; Compressor comp_; Denoise denoise_;
};
}
