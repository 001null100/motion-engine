#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <mutex>
#include "MotionEngineCore.h"

class BridgeEngine final : private juce::Thread
{
public:
    struct SlotStatus
    {
        bool mapped = false;
        bool armed = false;
        juce::String targetName { "None" };
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

    explicit BridgeEngine(motion::MotionEngineCore& core);
    ~BridgeEngine() override;

    void start();
    void requestMap(int slot);
    void requestUnmap(int slot);
    Status getStatus() const;

private:
    void run() override;
    void sendCommand(const juce::String& command, int slot);
    void sendValues(uint64_t sequence, const std::array<float, motion::kNumOutputs>& values);
    void receiveTelemetry();

    motion::MotionEngineCore& core;
    juce::DatagramSocket socket { false };

    std::array<std::atomic<bool>, motion::kNumOutputs> pendingMap {};
    std::array<std::atomic<bool>, motion::kNumOutputs> pendingUnmap {};

    mutable std::mutex statusMutex;
    Status status;
    uint64_t sequence = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeEngine)
};
