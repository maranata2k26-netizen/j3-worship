#pragma once
#include "j3/Types.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace j3 {
enum class AudioBackend { Asio, WasapiExclusive, WasapiShared, Mock };

struct AudioDeviceDescriptor {
    std::string id;
    std::string name;
    std::string manufacturer;
    AudioBackend backend{AudioBackend::Mock};
    std::uint32_t maxInputs{0};
    std::uint32_t maxOutputs{0};
    std::vector<double> sampleRates;
    std::vector<std::uint32_t> bufferSizes;
    bool isDefault{false};
};

class IAudioDeviceProvider {
public:
    virtual ~IAudioDeviceProvider() = default;
    virtual std::vector<AudioDeviceDescriptor> enumerate() = 0;
};

class AudioDeviceManager {
public:
    explicit AudioDeviceManager(IAudioDeviceProvider& provider) : provider_(provider) {}
    void refresh();
    const std::vector<AudioDeviceDescriptor>& devices() const noexcept { return devices_; }
    std::optional<AudioDeviceDescriptor> findById(const std::string& id) const;
    bool supports(const AudioDeviceDescriptor& d, const AudioConfig& cfg, std::string& error) const;
private:
    IAudioDeviceProvider& provider_;
    std::vector<AudioDeviceDescriptor> devices_;
};

class MockAudioDeviceProvider final : public IAudioDeviceProvider {
public:
    std::vector<AudioDeviceDescriptor> enumerate() override;
};
}
