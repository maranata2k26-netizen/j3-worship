#pragma once
#include "j3/SpscRing.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace j3 {
class FloatWavWriter {
public:
    FloatWavWriter() = default;
    ~FloatWavWriter();
    bool open(const std::filesystem::path& path, std::uint32_t sampleRate, std::uint16_t channels, std::string& error);
    bool writeInterleaved(const float* samples, std::size_t frames, std::string& error);
    bool close(std::string& error);
    std::uint64_t framesWritten() const noexcept { return framesWritten_; }
private:
    std::ofstream out_;
    std::uint32_t sampleRate_{0}; std::uint16_t channels_{0}; std::uint64_t framesWritten_{0};
};

constexpr std::size_t kRecordMaxChannels = 48;
constexpr std::size_t kRecordMaxFrames = 512;
struct RecordingBlock {
    std::uint16_t channels{0};
    std::uint16_t frames{0};
    std::array<float, kRecordMaxChannels*kRecordMaxFrames> planar{};
};

class MultiTrackRecorder {
public:
    MultiTrackRecorder();
    ~MultiTrackRecorder();
    bool start(const std::filesystem::path& directory, std::uint32_t sampleRate, const std::vector<std::string>& channelNames, std::string& error);
    bool submit(const float* const* inputs, std::size_t channels, std::size_t frames) noexcept;
    bool stop(std::string& error);
    bool recording() const noexcept { return running_.load(std::memory_order_acquire); }
    std::uint64_t overflowCount() const noexcept { return overflows_.load(std::memory_order_relaxed); }
    std::uint64_t blocksWritten() const noexcept { return blocksWritten_.load(std::memory_order_relaxed); }
    bool hasWorkerError() const noexcept { return workerError_.load(std::memory_order_relaxed); }
private:
    void workerMain();
    static std::string safeName(std::string name);
    std::unique_ptr<SpscRing<RecordingBlock, 64>> queue_;
    std::vector<std::unique_ptr<FloatWavWriter>> writers_;
    std::thread worker_;
    std::atomic_bool running_{false};
    std::atomic_bool stopRequested_{false};
    std::atomic_uint64_t overflows_{0};
    std::atomic_uint64_t blocksWritten_{0};
    std::atomic_bool workerError_{false};
};
}
