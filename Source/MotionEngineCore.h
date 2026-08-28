#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace motion
{
constexpr int kNumOutputs = 8;
constexpr int kNumZones = 4;

struct ZoneParameters
{
    double x = 0.0;
    double y = 0.0;
    double radius = 0.38;
    double falloff = 1.35;
};

struct OutputParameters
{
    int source = 0;
    double minimum = 0.0;
    double maximum = 1.0;
    int curve = 1;
    double smoothingMs = 12.0;
};

struct Parameters
{
    int model = 1;
    int constraint = 0;
    double timeScale = 1.0;
    double energy = 1.0;
    double globalDamping = 0.12;
    double audioKick = 0.45;
    std::array<double, 4> motion { 0.58, 0.34, 0.5, 0.5 };
    std::array<ZoneParameters, kNumZones> zones {{
        { -0.52,  0.52, 0.38, 1.35 },
        {  0.52,  0.52, 0.38, 1.35 },
        { -0.52, -0.52, 0.38, 1.35 },
        {  0.52, -0.52, 0.38, 1.35 },
    }};
    std::array<OutputParameters, kNumOutputs> outputs {{
        { 0,  0.0, 1.0, 1, 12.0 },
        { 1,  0.0, 1.0, 1, 12.0 },
        { 4,  0.0, 1.0, 1, 12.0 },
        { 8,  0.0, 1.0, 1, 12.0 },
        { 9,  0.0, 1.0, 1, 12.0 },
        { 10, 0.0, 1.0, 1, 12.0 },
        { 11, 0.0, 1.0, 1, 12.0 },
        { 12, 0.0, 1.0, 1, 12.0 },
    }};
};

struct AudioAnalysis
{
    double rms = 0.0;
    double leftRms = 0.0;
    double rightRms = 0.0;
    int channels = 0;
};

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

    MotionEngineCore();

    void prepare(double sampleRate) noexcept;
    void setParameters(const Parameters& parameters) noexcept { parameters_ = parameters; }
    void process(double blockSeconds, const AudioAnalysis& audio) noexcept;
    void requestReset() noexcept { resetPending_.store(true, std::memory_order_release); }

    Snapshot getSnapshot() const noexcept;
    std::array<float, kNumOutputs> getOutputs() const noexcept;

    void triggerHit() noexcept;
    void beginDrag(float x, float y) noexcept;
    void dragTo(float x, float y) noexcept;
    void endDrag(float velocityX, float velocityY) noexcept;

    static constexpr std::array<std::string_view, 10> modelNames() noexcept
    {
        return { "Orbit", "Spring", "Pendulum", "Brownian", "Drift", "Bounce", "Magnet", "Explosion", "Decay", "Follower" };
    }

    static constexpr std::array<std::string_view, 5> constraintNames() noexcept
    {
        return { "Free 2D", "Horizontal", "Vertical", "Diagonal", "Circle" };
    }

    static constexpr std::array<std::string_view, 15> sourceNames() noexcept
    {
        return { "X Position", "Y Position", "X Velocity", "Y Velocity", "Speed", "Energy", "Radius", "Angle",
                 "Impact", "Zone 1", "Zone 2", "Zone 3", "Zone 4", "Audio Envelope", "Transient" };
    }

    static constexpr std::array<std::string_view, 5> curveNames() noexcept
    {
        return { "Linear", "Smooth", "Exponential", "Logarithmic", "S Curve" };
    }

    static std::array<std::string_view, 4> controlNamesForModel(int model) noexcept;

private:
    struct AtomicSnapshot
    {
        std::atomic<float> x { 0.0f };
        std::atomic<float> y { 0.0f };
        std::atomic<float> vx { 0.0f };
        std::atomic<float> vy { 0.0f };
        std::atomic<float> speed { 0.0f };
        std::atomic<float> energy { 0.0f };
        std::atomic<float> radius { 0.0f };
        std::atomic<float> angle { 0.5f };
        std::atomic<float> impact { 0.0f };
        std::atomic<float> audioEnvelope { 0.0f };
        std::atomic<float> transient { 0.0f };
        std::array<std::atomic<float>, kNumZones> zones {};
        std::array<std::atomic<float>, kNumOutputs> outputs {};

        void store(const Snapshot& value) noexcept;
        Snapshot load() const noexcept;
    };

    void resetNow() noexcept;
    void analyseAudio(const AudioAnalysis& input) noexcept;
    void resetForModel(int model) noexcept;
    void step(double dt) noexcept;
    void applyGlobalForces(double dt) noexcept;
    void applyConstraint() noexcept;
    void containBody(bool bounce, double restitution, double chaos) noexcept;
    void publishSnapshot(double realDt) noexcept;
    float sourceValue(int source, const Snapshot& raw) const noexcept;
    float zoneValue(int index) const noexcept;
    float transformOutput(int index, float source, double dt) noexcept;
    double randomSigned() noexcept;

    Parameters parameters_;
    double sampleRate_ = 48000.0;
    double accumulator_ = 0.0;
    double elapsed_ = 0.0;
    int lastModel_ = -1;

    double x_ = 0.0;
    double y_ = 0.0;
    double vx_ = 0.0;
    double vy_ = 0.0;
    double pendulumAngle_ = 0.65;
    double pendulumVelocity_ = 0.0;
    double noiseX_ = 0.0;
    double noiseY_ = 0.0;
    double driftPhaseA_ = 0.0;
    double driftPhaseB_ = 1.7;

    double audioEnvelope_ = 0.0;
    double audioBalance_ = 0.0;
    double transientEnvelope_ = 0.0;
    double previousAudioEnvelope_ = 0.0;
    double impactEnvelope_ = 0.0;
    std::array<double, kNumOutputs> smoothedOutputs_ { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };

    std::atomic<bool> resetPending_ { false };
    std::atomic<bool> hitPending_ { false };
    std::atomic<bool> dragging_ { false };
    std::atomic<float> dragX_ { 0.0f };
    std::atomic<float> dragY_ { 0.0f };
    std::atomic<bool> throwPending_ { false };
    std::atomic<float> throwVX_ { 0.0f };
    std::atomic<float> throwVY_ { 0.0f };

    AtomicSnapshot snapshot_;
    std::uint32_t randomState_ = 0x4d6f7469u;
};
} // namespace motion
