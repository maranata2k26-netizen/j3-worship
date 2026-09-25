#pragma once
#include "j3/Mixer.h"
#include <string>
#include <vector>
namespace j3 {
struct ChannelSceneState { double faderDb{0}; bool mute{false}; double pan{0}; };
struct Scene { std::string name; std::vector<ChannelSceneState> channels; };
class SceneManager {
public:
    Scene capture(const std::string& name,const Mixer& mixer) const;
    void recall(const Scene& scene,Mixer& mixer,const std::vector<bool>& safeChannels) const;
};
}
