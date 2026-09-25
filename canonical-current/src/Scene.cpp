#include "j3/Scene.h"
#include <algorithm>
namespace j3 {
Scene SceneManager::capture(const std::string& name,const Mixer& m) const { Scene s; s.name=name; s.channels.reserve(m.size()); for(std::size_t i=0;i<m.size();++i){const auto& c=m.channel(i);s.channels.push_back({c.faderDb,c.mute,c.pan});} return s; }
void SceneManager::recall(const Scene& s,Mixer& m,const std::vector<bool>& safe) const { const auto n=std::min(s.channels.size(),m.size()); for(std::size_t i=0;i<n;++i){if(i<safe.size()&&safe[i])continue; auto& c=m.channel(i);c.faderDb=s.channels[i].faderDb;c.mute=s.channels[i].mute;c.pan=s.channels[i].pan;} }
}
