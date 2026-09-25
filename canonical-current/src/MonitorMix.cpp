#include "j3/MonitorMix.h"
namespace j3 {
MonitorMixer::MonitorMixer(std::size_t sources,std::size_t count){mixes_.reserve(count);for(std::size_t i=0;i<count;++i){MonitorMix m;m.name="IEM "+std::to_string(i+1);m.sends.resize(sources);mixes_.push_back(std::move(m));}}
bool MonitorMixer::copyMix(std::size_t from,std::size_t to){if(from>=mixes_.size()||to>=mixes_.size()||from==to)return false;const auto name=mixes_[to].name;mixes_[to]=mixes_[from];mixes_[to].name=name;return true;}
void MonitorMixer::setSend(std::size_t m,std::size_t s,double db){mixes_.at(m).sends.at(s).levelDb=std::clamp(db,-80.0,12.0);}
}
