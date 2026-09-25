#pragma once
#include <map>
#include <optional>
#include <string>
#include <tuple>
namespace j3 {
struct MidiMessage {
    int channel{1}; int number{0}; int value{0}; bool cc{true};
    friend bool operator<(const MidiMessage&a,const MidiMessage&b) noexcept { return std::tie(a.channel,a.number,a.cc)<std::tie(b.channel,b.number,b.cc); }
};
class MidiMap {
public:
    void assign(MidiMessage m,std::string action){map_[m]=std::move(action);}
    std::optional<std::string> actionFor(const MidiMessage&m)const{auto it=map_.find(m);return it==map_.end()?std::nullopt:std::optional<std::string>(it->second);}
private: std::map<MidiMessage,std::string> map_;
};
}
