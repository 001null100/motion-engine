#pragma once

#include "MotionEngineCore.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class BridgeEngine final
{
public:
    struct SlotStatus
    {
        bool mapped = false;
        bool armed = false;
        std::string targetName { "None" };
    };

    struct Status
    {
        bool bridgeSeen = false;
        double receivedHz = 0.0;
        double appliedHz = 0.0;
        double worstGapMs = 0.0;
        double sentHz = 0.0;
        std::array<SlotStatus, motion::kNumOutputs> slots {};
    };

    explicit BridgeEngine(motion::MotionEngineCore& core) noexcept;
    ~BridgeEngine();

    void start();
    void requestMap(int slot) noexcept;
    void requestUnmap(int slot) noexcept;
    Status getStatus() const;

private:
    void run() noexcept;
    void sendCommand(const char* command, int slot) noexcept;
    void sendValues(std::uint64_t sequence, const std::array<float, motion::kNumOutputs>& values) noexcept;
    void sendPacket(const std::string& message) noexcept;
    void receiveTelemetry() noexcept;

    motion::MotionEngineCore& core_;
    std::atomic<bool> running_ { false };
    std::thread thread_;
    std::intptr_t socket_ = -1;
    bool socketRuntimeReady_ = false;

    std::array<std::atomic<bool>, motion::kNumOutputs> pendingMap_ {};
    std::array<std::atomic<bool>, motion::kNumOutputs> pendingUnmap_ {};

    mutable std::mutex statusMutex_;
    Status status_;
    std::uint64_t sequence_ = 0;
};
