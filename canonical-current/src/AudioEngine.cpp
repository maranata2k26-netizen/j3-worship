#include "j3/AudioEngine.h"
#include <array>

namespace j3 {
bool AudioEngine::configure(const AudioConfig& cfg, std::string& error) {
    static constexpr std::array<double,3> allowed{44100.0,48000.0,96000.0};
    bool srOk=false; for (auto s:allowed) if (s==cfg.sampleRate) srOk=true;
    if (!srOk) { error="Unsupported sample rate"; return false; }
    if (cfg.bufferFrames < 32 || cfg.bufferFrames > 4096) { error="Buffer outside safe range"; return false; }
    if (cfg.inputs==0 || cfg.outputs==0) { error="At least one input and output required"; return false; }
    if (running()) { error="Stop engine before reconfiguration"; return false; }
    config_=cfg; return true;
}

bool AudioEngine::start(std::string& error) {
    if (!deviceConnected_.load()) { error="Audio device disconnected"; return false; }
    running_.store(true, std::memory_order_release); return true;
}
void AudioEngine::stop() noexcept { running_.store(false, std::memory_order_release); }
bool AudioEngine::reconnect(std::string& error) {
    deviceConnected_.store(true);
    return start(error);
}
}
