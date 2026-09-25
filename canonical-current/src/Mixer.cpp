#include "j3/Mixer.h"
#include <algorithm>
#include <stdexcept>

namespace j3 {
std::size_t Mixer::addChannel(ChannelStrip c) { channels_.push_back(std::move(c)); return channels_.size()-1; }
void Mixer::createDrumPreset(bool audiencePerspective) {
    const char* names[] = {"Kick","Snare","Hi-Hat","Tom 1","Tom 2","Floor Tom","Overhead L","Overhead R"};
    const double pans[] = {0,0,-0.25,-0.20,0.20,0.35,-0.65,0.65};
    for (int i=0;i<8;++i) {
        ChannelStrip c; c.name=names[i]; c.role=ChannelRole::Input; c.pan = audiencePerspective ? -pans[i] : pans[i];
        c.hpfHz = (i==0 ? 25.0 : (i==1 ? 70.0 : 90.0)); addChannel(c);
    }
    ChannelStrip bus; bus.name="DRUM BUS"; bus.role=ChannelRole::Bus; addChannel(bus);
}
void Mixer::createVocalPreset(int leads, int bgv) {
    if (leads<0 || bgv<0) throw std::invalid_argument("Negative vocal count");
    for(int i=0;i<leads;++i){ ChannelStrip c; c.name = leads==1?"Lead Vocal":"Lead Vocal "+std::to_string(i+1); c.pan=0; c.hpfHz=80; addChannel(c); }
    for(int i=0;i<bgv;++i){ ChannelStrip c; c.name="BGV "+std::to_string(i+1); c.hpfHz=90; c.pan = bgv==1?0.0:(-0.35 + 0.70*static_cast<double>(i)/static_cast<double>(bgv-1)); addChannel(c); }
    ChannelStrip bus; bus.name="VOCAL BUS"; bus.role=ChannelRole::Bus; addChannel(bus);
}
}
