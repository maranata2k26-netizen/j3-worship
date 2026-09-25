#pragma once
#include "j3/Types.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace j3 {
class AudioEngine {
public:
    bool configure(const AudioConfig& cfg, std::string& error);
    bool start(std::string& error);
    void stop() noexcept;
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    AudioConfig config() const noexcept { return config_; }
    std::uint64_t dropouts() const noexcept { return dropouts_.load(); }
    void reportDropout() noexcept { dropouts_.fetch_add(1); }
    void simulateDeviceDisconnect() noexcept { deviceConnected_.store(false); running_.store(false); }
    bool reconnect(std::string& error);
private:
    AudioConfig config_{};
    std::atomic_bool running_{false};
    std::atomic_bool deviceConnected_{true};
    std::atomic_uint64_t dropouts_{0};
};
}
