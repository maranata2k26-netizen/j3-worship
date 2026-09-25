#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
namespace j3 {
struct PadSample { std::string key; std::vector<float> mono; std::size_t crossfadeSamples{2048}; };
class PadEngine {
public:
    bool load(PadSample sample,std::string& error);
    void play() noexcept { playing_=!sample_.mono.empty(); }
    void stop() noexcept { playing_=false; }
    void setVolume(float v) noexcept { volume_=std::clamp(v,0.0f,2.0f); }
    float next() noexcept;
    const std::string& key() const noexcept { return sample_.key; }
private:
    PadSample sample_{}; std::size_t pos_{0}; bool playing_{false}; float volume_{1.0f};
};
}
