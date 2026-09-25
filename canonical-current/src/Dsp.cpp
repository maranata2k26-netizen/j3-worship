#include "j3/Dsp.h"
#include <algorithm>
#include <numbers>

namespace j3 {
static double dbToGain(double db) noexcept { return std::pow(10.0, db/20.0); }
static double gainToDb(double g) noexcept { return 20.0*std::log10(std::max(g,1e-12)); }
void Biquad::normalize(double b0,double b1,double b2,double a0,double a1,double a2) noexcept { b0_=b0/a0; b1_=b1/a0; b2_=b2/a0; a1_=a1/a0; a2_=a2/a0; }
void Biquad::setLowPass(double sr,double hz,double q) noexcept { hz=std::clamp(hz,5.0,sr*0.49); const double w=2.0*std::numbers::pi_v<double>*hz/sr,c=std::cos(w),s=std::sin(w),a=s/(2*q); normalize((1-c)/2,1-c,(1-c)/2,1+a,-2*c,1-a); }
void Biquad::setHighPass(double sr,double hz,double q) noexcept { hz=std::clamp(hz,5.0,sr*0.49); const double w=2.0*std::numbers::pi_v<double>*hz/sr,c=std::cos(w),s=std::sin(w),a=s/(2*q); normalize((1+c)/2,-(1+c),(1+c)/2,1+a,-2*c,1-a); }
void Biquad::setPeak(double sr,double hz,double q,double gainDb) noexcept { hz=std::clamp(hz,5.0,sr*0.49); q=std::clamp(q,0.1,18.0); const double A=std::pow(10.0,gainDb/40.0),w=2*std::numbers::pi_v<double>*hz/sr,c=std::cos(w),s=std::sin(w),a=s/(2*q); normalize(1+a*A,-2*c,1-a*A,1+a/A,-2*c,1-a/A); }
float Biquad::process(float x) noexcept { const double y=b0_*x+z1_; z1_=b1_*x-a1_*y+z2_; z2_=b2_*x-a2_*y; return static_cast<float>(y); }
void Gate::configure(double sr,double thresholdDb,double attackMs,double releaseMs) noexcept { threshold_=dbToGain(thresholdDb); attack_=1-std::exp(-1.0/(sr*attackMs/1000.0)); release_=1-std::exp(-1.0/(sr*releaseMs/1000.0)); }
float Gate::process(float x) noexcept { const double target=std::abs(x)>=threshold_?1.0:0.0; env_ += (target-env_)*(target>env_?attack_:release_); return static_cast<float>(x*env_); }
void Compressor::configure(double sr,double thresholdDb,double ratio,double attackMs,double releaseMs,double makeupDb) noexcept { thresholdDb_=thresholdDb; ratio_=std::max(1.0,ratio); attack_=1-std::exp(-1.0/(sr*attackMs/1000.0)); release_=1-std::exp(-1.0/(sr*releaseMs/1000.0)); makeup_=dbToGain(makeupDb); }
float Compressor::process(float x) noexcept { const double inDb=gainToDb(std::abs(x)); double reduction=0; if(inDb>thresholdDb_) reduction=(thresholdDb_ + (inDb-thresholdDb_)/ratio_) - inDb; const double target=reduction; envDb_ += (target-envDb_)*(target<envDb_?attack_:release_); return static_cast<float>(x*dbToGain(envDb_)*makeup_); }
void Denoise::configure(double,double amount,double thresholdDb) noexcept { amount_=std::clamp(amount,0.0,1.0); threshold_=dbToGain(thresholdDb); }
void Denoise::learnNoise(float sample) noexcept { learning_=true; noiseSum_ += std::abs(sample); ++noiseN_; }
void Denoise::finishLearning() noexcept { if(noiseN_) threshold_=std::max(threshold_,(noiseSum_/static_cast<double>(noiseN_))*1.5); noiseSum_=0; noiseN_=0; learning_=false; }
float Denoise::process(float x) noexcept { if(bypass_||learning_) return x; const double a=std::abs(x); double target=1.0; if(a<threshold_) { const double floor=1.0-0.85*amount_; target=floor+(1.0-floor)*(a/std::max(threshold_,1e-9)); } gain_ += (target-gain_)*(target<gain_?0.03:0.006); return static_cast<float>(x*gain_); }
void ChannelDsp::prepare(double s) noexcept { sr_=s; setHpf(20); setLpf(std::min(20000.0,sr_*0.45)); for(std::size_t i=0;i<4;++i)setEqBand(i,200.0*std::pow(3.0,static_cast<double>(i)),1.0,0); setGate(-60); setCompressor(-18,3); setDenoise(0,-60); }
void ChannelDsp::setHpf(double hz) noexcept { hpf_.setHighPass(sr_,hz); }
void ChannelDsp::setLpf(double hz) noexcept { lpf_.setLowPass(sr_,hz); }
void ChannelDsp::setEqBand(std::size_t i,double hz,double q,double gainDb) noexcept { if(i<eq_.size()) eq_[i].setPeak(sr_,hz,q,gainDb); }
void ChannelDsp::setGate(double t) noexcept { gate_.configure(sr_,t); }
void ChannelDsp::setCompressor(double t,double r) noexcept { comp_.configure(sr_,t,r); }
void ChannelDsp::setDenoise(double a,double t) noexcept { denoise_.configure(sr_,a,t); }
float ChannelDsp::process(float x) noexcept { x=hpf_.process(x); for(auto& e:eq_)x=e.process(x); x=gate_.process(x); x=comp_.process(x); x=denoise_.process(x); return lpf_.process(x); }
}
