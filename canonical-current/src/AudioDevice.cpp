#include "j3/AudioDevice.h"
#include <algorithm>

namespace j3 {
void AudioDeviceManager::refresh() { devices_ = provider_.enumerate(); }
std::optional<AudioDeviceDescriptor> AudioDeviceManager::findById(const std::string& id) const {
    auto it = std::find_if(devices_.begin(), devices_.end(), [&](const auto& d){ return d.id == id; });
    if (it == devices_.end()) return std::nullopt;
    return *it;
}
bool AudioDeviceManager::supports(const AudioDeviceDescriptor& d, const AudioConfig& cfg, std::string& error) const {
    if (cfg.inputs > d.maxInputs || cfg.outputs > d.maxOutputs) { error = "Requested channel count exceeds device capability"; return false; }
    if (!d.sampleRates.empty() && std::find(d.sampleRates.begin(), d.sampleRates.end(), cfg.sampleRate) == d.sampleRates.end()) { error = "Sample rate not supported by selected device"; return false; }
    if (!d.bufferSizes.empty() && std::find(d.bufferSizes.begin(), d.bufferSizes.end(), cfg.bufferFrames) == d.bufferSizes.end()) { error = "Buffer size not supported by selected device"; return false; }
    return true;
}
std::vector<AudioDeviceDescriptor> MockAudioDeviceProvider::enumerate() {
    return {
        {"mock-focusrite", "Focusrite-compatible ASIO device (simulation)", "Generic", AudioBackend::Mock, 18, 20, {44100,48000,96000}, {64,128,256,512}, true},
        {"mock-xr18", "XR18-compatible ASIO device (simulation)", "Generic", AudioBackend::Mock, 18, 18, {44100,48000}, {64,128,256,512}, false},
        {"mock-stereo", "2x2 ASIO device (simulation)", "Generic", AudioBackend::Mock, 2, 2, {44100,48000,96000}, {64,128,256,512,1024}, false}
    };
}
}
