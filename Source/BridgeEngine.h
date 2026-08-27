#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <mutex>

class BridgeEngine final : private juce::Thread
{
public:
    struct Status
    {
        bool bridgeSeen = false;
        bool mapped = false;
        bool armed = false;
        juce::String targetName { "None" };
        double receivedHz = 0.0;
        double appliedHz = 0.0;
        double requestedHz = 0.0;
        double worstGapMs = 0.0;
        double sentHz = 0.0;
    };

    explicit BridgeEngine(juce::AudioProcessorValueTreeState& state);
    ~BridgeEngine() override;

    void start();
    void triggerImpulse();
    void requestMap();
    void requestUnmap();
    Status getStatus() const;
    float getCurrentValue() const noexcept { return currentValue.load(std::memory_order_relaxed); }

private:
    void run() override;
    float generateValue(double dt, double timeSeconds, int source, double frequency,
                        double stiffness, double damping);
    void sendCommand(const juce::String& command);
    void sendValue(uint64_t sequence, float value, int requestedRate);
    void receiveTelemetry();
    static int rateFromChoice(int choiceIndex);

    juce::AudioProcessorValueTreeState& parameters;
    juce::DatagramSocket socket { false };

    std::atomic<bool> pendingImpulse { false };
    std::atomic<bool> pendingMap { false };
    std::atomic<bool> pendingUnmap { false };
    std::atomic<float> currentValue { 0.5f };

    mutable std::mutex statusMutex;
    Status status;

    double springPosition = 0.0;
    double springVelocity = 0.0;
    uint64_t sequence = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeEngine)
};
