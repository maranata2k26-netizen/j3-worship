#include "j3/LiveEngine.h"
#include <algorithm>
#include <stdexcept>
namespace j3 {
void LiveEngine::setTempo(double bpm,int bpb){ if(bpm<20||bpm>400||bpb<1||bpb>16) throw std::invalid_argument("Invalid tempo/signature"); bpm_=bpm; beatsPerBar_=bpb; }
void LiveEngine::start(Section s){ current_=std::move(s); queued_.reset(); beatCounter_=0; playing_=true; }
void LiveEngine::request(Section n,Quantize q){ queued_=std::move(n); quantize_=q; }
bool LiveEngine::boundaryReached() const {
    if(!queued_) return false;
    const auto beatInBar = beatCounter_%static_cast<std::uint64_t>(beatsPerBar_);
    const auto bar = beatCounter_/static_cast<std::uint64_t>(beatsPerBar_);
    switch(quantize_){
        case Quantize::Beat:return true;
        case Quantize::Bar:return beatInBar==0;
        case Quantize::TwoBars:return beatInBar==0 && bar%2==0;
        case Quantize::EndOfSection:return beatCounter_>0 && beatCounter_%static_cast<std::uint64_t>(std::max(1,current_.bars)*beatsPerBar_)==0;
    } return false;
}
void LiveEngine::tickBeat(){ if(!playing_) return; ++beatCounter_; if(boundaryReached()){ current_=*queued_; queued_.reset(); beatCounter_=0; } }
void LiveEngine::enterFreePad(){ request({"FREE / PAD",SectionKind::FreePad,1},Quantize::Bar); }
std::string LiveEngine::now() const{return current_.name;} std::string LiveEngine::next() const{return queued_?queued_->name:"—";}
}
