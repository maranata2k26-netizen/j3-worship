#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
namespace j3 {
struct MonitorSend { double levelDb{-80.0}; bool preFader{true}; bool mute{false}; };
struct MonitorMix { std::string name; std::vector<MonitorSend> sends; double masterDb{0.0}; bool talkbackEnabled{true}; };
class MonitorMixer {
public:
    MonitorMixer(std::size_t sourceChannels,std::size_t mixes);
    std::size_t mixCount() const noexcept{return mixes_.size();}
    MonitorMix& mix(std::size_t i){return mixes_.at(i);} const MonitorMix& mix(std::size_t i)const{return mixes_.at(i);}
    bool copyMix(std::size_t from,std::size_t to);
    void setSend(std::size_t mix,std::size_t source,double levelDb);
private: std::vector<MonitorMix> mixes_;
};
}
