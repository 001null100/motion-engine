#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <mutex>

namespace motion
{
constexpr int kNumOutputs = 8;
constexpr int kNumZones = 4;

class MotionEngineCore final
{
public:
    struct Snapshot
    {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float speed = 0.0f;
        float energy = 0.0f;
        float radius = 0.0f;
        float angle = 0.5f;
        float impact = 0.0f;
        float audioEnvelope = 0.0f;
        float transient = 0.0f;
        std::array<float, kNumZones> zones {};
        std::array<float, kNumOutputs> outputs {};
    };

    explicit MotionEngineCore(juce::AudioProcessorValueTreeState& parameters);

    void prepare(double sampleRate);
    void processBlock(const juce::AudioBuffer<float>& input);
    void reset();

    Snapshot getSnapshot() const;
    std::array<float, kNumOutputs> getOutputs() const;

    void triggerHit();
    void beginDrag(float x, float y);
    void dragTo(float x, float y);
    void endDrag(float velocityX, float velocityY);

    static juce::StringArray modelNames();
    static juce::StringArray constraintNames();
    static juce::StringArray sourceNames();
    static juce::StringArray curveNames();
    static std::array<juce::String, 4> controlNamesForModel(int model);

private:
    float param(const juce::String& id) const;
    int intParam(const juce::String& id) const;

    void analyseAudio(const juce::AudioBuffer<float>& input);
    void resetForModel(int model);
    void step(double dt);
    void applyGlobalForces(double dt);
    void applyConstraint();
    void containBody(bool bounce, double restitution, double chaos);
    void publishSnapshot(double realDt);
    float sourceValue(int source, const Snapshot& raw) const;
    float zoneValue(int index) const;
    float transformOutput(int index, float source, double dt);
    double randomSigned();

    juce::AudioProcessorValueTreeState& parameters;
    double sampleRate = 48000.0;
    double accumulator = 0.0;
    double elapsed = 0.0;
    int lastModel = -1;

    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double pendulumAngle = 0.65;
    double pendulumVelocity = 0.0;
    double noiseX = 0.0;
    double noiseY = 0.0;
    double driftPhaseA = 0.0;
    double driftPhaseB = 1.7;

    double audioEnvelope = 0.0;
    double audioBalance = 0.0;
    double transientEnvelope = 0.0;
    double previousAudioEnvelope = 0.0;
    double impactEnvelope = 0.0;
    std::array<double, kNumOutputs> smoothedOutputs { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };

    std::atomic<bool> hitPending { false };
    std::atomic<bool> dragging { false };
    std::atomic<float> dragX { 0.0f };
    std::atomic<float> dragY { 0.0f };
    std::atomic<bool> throwPending { false };
    std::atomic<float> throwVX { 0.0f };
    std::atomic<float> throwVY { 0.0f };

    mutable std::mutex snapshotMutex;
    Snapshot snapshot;
    juce::Random random { 0x4d6f7469 };
};
} // namespace motion
