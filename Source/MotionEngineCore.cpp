#include "MotionEngineCore.h"
#include "MotionPaths.h"

#include <algorithm>
#include <cmath>

namespace motion
{
namespace
{
constexpr double kSimulationHz = 240.0;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = kPi * 2.0;
constexpr double kSqrtTwo = 1.4142135623730951;

template <typename T>
constexpr T clamp(T value, T minimum, T maximum) noexcept
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

double clampUnit(double value) noexcept { return clamp(value, 0.0, 1.0); }
double clampWorld(double value) noexcept { return clamp(value, -1.0, 1.0); }

struct ProjectedState
{
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
};

ProjectedState projectConstraint(int constraint, double x, double y, double vx, double vy) noexcept
{
    ProjectedState result { x, y, vx, vy };

    switch (clamp(constraint, 0, 4))
    {
        case 1:
            result.y = 0.0;
            result.vy = 0.0;
            break;

        case 2:
            result.x = 0.0;
            result.vx = 0.0;
            break;

        case 3:
        {
            const double position = (x + y) * 0.5;
            const double velocity = (vx + vy) * 0.5;
            result.x = result.y = position;
            result.vx = result.vy = velocity;
            break;
        }

        case 4:
        {
            constexpr double radius = 0.72;
            double nx = 1.0;
            double ny = 0.0;
            const double positionRadius = std::hypot(x, y);

            if (positionRadius > 1.0e-7)
            {
                nx = x / positionRadius;
                ny = y / positionRadius;
            }
            else
            {
                const double velocityRadius = std::hypot(vx, vy);
                if (velocityRadius > 1.0e-7)
                {
                    nx = vx / velocityRadius;
                    ny = vy / velocityRadius;
                }
            }

            result.x = nx * radius;
            result.y = ny * radius;

            const double tx = -ny;
            const double ty = nx;
            const double tangentSpeed = vx * tx + vy * ty;
            result.vx = tx * tangentSpeed;
            result.vy = ty * tangentSpeed;
            break;
        }

        default:
            break;
    }

    return result;
}
} // namespace

void MotionEngineCore::AtomicSnapshot::store(const Snapshot& value) noexcept
{
    x.store(value.x, std::memory_order_relaxed);
    y.store(value.y, std::memory_order_relaxed);
    vx.store(value.vx, std::memory_order_relaxed);
    vy.store(value.vy, std::memory_order_relaxed);
    speed.store(value.speed, std::memory_order_relaxed);
    energy.store(value.energy, std::memory_order_relaxed);
    radius.store(value.radius, std::memory_order_relaxed);
    angle.store(value.angle, std::memory_order_relaxed);
    impact.store(value.impact, std::memory_order_relaxed);
    audioEnvelope.store(value.audioEnvelope, std::memory_order_relaxed);
    transient.store(value.transient, std::memory_order_relaxed);
    trajectoryPhase.store(value.trajectoryPhase, std::memory_order_relaxed);
    for (int i = 0; i < kNumZones; ++i)
        zones[static_cast<std::size_t>(i)].store(value.zones[static_cast<std::size_t>(i)], std::memory_order_relaxed);
    for (int i = 0; i < kNumOutputs; ++i)
        outputs[static_cast<std::size_t>(i)].store(value.outputs[static_cast<std::size_t>(i)], std::memory_order_relaxed);
}

MotionEngineCore::Snapshot MotionEngineCore::AtomicSnapshot::load() const noexcept
{
    Snapshot value;
    value.x = x.load(std::memory_order_relaxed);
    value.y = y.load(std::memory_order_relaxed);
    value.vx = vx.load(std::memory_order_relaxed);
    value.vy = vy.load(std::memory_order_relaxed);
    value.speed = speed.load(std::memory_order_relaxed);
    value.energy = energy.load(std::memory_order_relaxed);
    value.radius = radius.load(std::memory_order_relaxed);
    value.angle = angle.load(std::memory_order_relaxed);
    value.impact = impact.load(std::memory_order_relaxed);
    value.audioEnvelope = audioEnvelope.load(std::memory_order_relaxed);
    value.transient = transient.load(std::memory_order_relaxed);
    value.trajectoryPhase = trajectoryPhase.load(std::memory_order_relaxed);
    for (int i = 0; i < kNumZones; ++i)
        value.zones[static_cast<std::size_t>(i)] = zones[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
    for (int i = 0; i < kNumOutputs; ++i)
        value.outputs[static_cast<std::size_t>(i)] = outputs[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
    return value;
}

MotionEngineCore::MotionEngineCore()
{
    resetNow();
}

void MotionEngineCore::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    resetNow();
}

void MotionEngineCore::resetNow() noexcept
{
    lastModel_ = -1;
    accumulator_ = 0.0;
    elapsed_ = 0.0;
    trajectoryPhase_ = 0.0;
    audioEnvelope_ = 0.0;
    audioBalance_ = 0.0;
    transientEnvelope_ = 0.0;
    previousAudioEnvelope_ = 0.0;
    impactEnvelope_ = 0.0;
    smoothedOutputs_.fill(0.5);
    throwPending_.store(false, std::memory_order_relaxed);
    dragging_.store(false, std::memory_order_relaxed);
    hitPending_.store(false, std::memory_order_relaxed);
    resetPending_.store(false, std::memory_order_relaxed);
    resetForModel(parameters_.model);
    publishSnapshot(1.0 / kSimulationHz);
}

MotionEngineCore::Snapshot MotionEngineCore::getSnapshot() const noexcept
{
    return snapshot_.load();
}

std::array<float, kNumOutputs> MotionEngineCore::getOutputs() const noexcept
{
    return getSnapshot().outputs;
}

void MotionEngineCore::triggerHit() noexcept
{
    hitPending_.store(true, std::memory_order_release);
}

void MotionEngineCore::beginDrag(float x, float y) noexcept
{
    dragX_.store(clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
    dragY_.store(clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
    dragging_.store(true, std::memory_order_release);
}

void MotionEngineCore::dragTo(float x, float y) noexcept
{
    dragX_.store(clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
    dragY_.store(clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
}

void MotionEngineCore::endDrag(float velocityX, float velocityY) noexcept
{
    throwVX_.store(clamp(velocityX, -8.0f, 8.0f), std::memory_order_relaxed);
    throwVY_.store(clamp(velocityY, -8.0f, 8.0f), std::memory_order_relaxed);
    throwPending_.store(true, std::memory_order_release);
    dragging_.store(false, std::memory_order_release);
}

std::array<std::string_view, 4> MotionEngineCore::controlNamesForModel(int model) noexcept
{
    switch (model)
    {
        case 0: return { "Radius", "Ellipticity", "Rotation", "Orbit Speed" };
        case 1: return { "Tension", "Damping", "Swirl", "Anchor Offset" };
        case 2: return { "Length", "Gravity", "Damping", "Drive" };
        case 3: return { "Activity", "Inertia", "Correlation", "Bias" };
        case 4: return { "Speed", "Wander", "Inertia", "Curl" };
        case 5: return { "Travel Speed", "Restitution", "Gravity", "Impact Chaos" };
        case 6: return { "Rate", "Y Ratio", "Phase", "Rotation" };
        case 7: return { "Force", "Rebound", "Decay", "Direction" };
        case 8: return { "Initial Energy", "Decay", "Rate", "Wobble" };
        case 9: return { "Response", "Damping", "Audio Gain", "Transient Lift" };
        default: return { "A", "B", "C", "D" };
    }
}

void MotionEngineCore::process(double blockSeconds, const AudioAnalysis& audio) noexcept
{
    if (resetPending_.exchange(false, std::memory_order_acq_rel))
        resetNow();

    analyseAudio(audio);
    accumulator_ += std::max(0.0, blockSeconds);
    constexpr double fixedStep = 1.0 / kSimulationHz;
    int safety = 0;

    while (accumulator_ >= fixedStep && safety++ < 128)
    {
        const double timeScale = clamp(parameters_.timeScale, 0.05, 4.0);
        step(fixedStep * timeScale);
        accumulator_ -= fixedStep;
        elapsed_ += fixedStep * timeScale;
    }

    publishSnapshot(blockSeconds);
}

void MotionEngineCore::analyseAudio(const AudioAnalysis& input) noexcept
{
    if (input.channels <= 0)
    {
        audioEnvelope_ *= 0.92;
        audioBalance_ *= 0.9;
        transientEnvelope_ *= 0.78;
        previousAudioEnvelope_ = audioEnvelope_;
        return;
    }

    const double rms = std::max(0.0, input.rms);
    const double attack = rms > audioEnvelope_ ? 0.55 : 0.08;
    audioEnvelope_ += (rms - audioEnvelope_) * attack;

    if (input.channels > 1)
    {
        const double total = input.leftRms + input.rightRms + 1.0e-9;
        const double balance = (input.rightRms - input.leftRms) / total;
        audioBalance_ += (balance - audioBalance_) * 0.18;
    }
    else
    {
        audioBalance_ *= 0.82;
    }

    const double rise = std::max(0.0, audioEnvelope_ - previousAudioEnvelope_);
    transientEnvelope_ = std::max(transientEnvelope_ * 0.72, clamp(rise * 18.0, 0.0, 1.0));
    previousAudioEnvelope_ = audioEnvelope_;
}

void MotionEngineCore::resetForModel(int model) noexcept
{
    lastModel_ = clamp(model, 0, 9);
    trajectoryPhase_ = 0.0;
    vx_ = vy_ = 0.0;
    pendulumVelocity_ = 0.0;
    noiseX_ = noiseY_ = 0.0;
    driftPhaseA_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;
    driftPhaseB_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;

    switch (lastModel_)
    {
        case 0:
        {
            const auto point = paths::orbitPoint(parameters_.motion[0], parameters_.motion[1], parameters_.motion[2], trajectoryPhase_);
            const auto velocity = paths::orbitVelocity(parameters_.motion[0], parameters_.motion[1], parameters_.motion[2], parameters_.motion[3], trajectoryPhase_);
            x_ = point.x;
            y_ = point.y;
            vx_ = velocity.x;
            vy_ = velocity.y;
            break;
        }

        case 1:
            x_ = 0.72;
            y_ = -0.18;
            break;

        case 2:
        {
            constexpr double anchorY = 0.12;
            const double length = 0.28 + clampUnit(parameters_.motion[0]) * 0.67;
            pendulumAngle_ = 0.72;
            x_ = std::sin(pendulumAngle_) * length;
            y_ = anchorY - std::cos(pendulumAngle_) * length;
            break;
        }

        case 3:
            x_ = 0.0;
            y_ = 0.0;
            break;

        case 4:
            x_ = -0.42;
            y_ = 0.18;
            pendulumAngle_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;
            break;

        case 5:
            x_ = -0.55;
            y_ = -0.25;
            vx_ = 1.15;
            vy_ = 0.78;
            break;

        case 6:
        {
            const auto point = paths::lissajousPoint(parameters_.motion[1], parameters_.motion[2], parameters_.motion[3], trajectoryPhase_);
            const auto velocity = paths::lissajousVelocity(parameters_.motion[0], parameters_.motion[1], parameters_.motion[2], parameters_.motion[3], trajectoryPhase_);
            x_ = point.x;
            y_ = point.y;
            vx_ = velocity.x;
            vy_ = velocity.y;
            break;
        }

        case 7:
            x_ = 0.0;
            y_ = 0.0;
            hitPending_.store(true, std::memory_order_relaxed);
            break;

        case 8:
            x_ = 0.0;
            y_ = 0.0;
            noiseX_ = 0.0;
            noiseY_ = 0.0;
            hitPending_.store(true, std::memory_order_relaxed);
            break;

        case 9:
            x_ = 0.0;
            y_ = -0.65;
            break;

        default:
            break;
    }
}

void MotionEngineCore::step(double dt) noexcept
{
    const int model = clamp(parameters_.model, 0, 9);
    if (model != lastModel_)
        resetForModel(model);

    if (dragging_.load(std::memory_order_acquire))
    {
        x_ = dragX_.load(std::memory_order_relaxed);
        y_ = dragY_.load(std::memory_order_relaxed);
        vx_ = vy_ = 0.0;

        if (model == 8)
        {
            noiseX_ = clamp(std::hypot(x_, y_), 0.0, 1.0);
            noiseY_ = std::atan2(y_, x_);
        }
        return;
    }

    if (throwPending_.exchange(false, std::memory_order_acq_rel))
    {
        vx_ = throwVX_.load(std::memory_order_relaxed);
        vy_ = throwVY_.load(std::memory_order_relaxed);

        if (model == 0)
        {
            const double radius = paths::orbitRadius(parameters_.motion[0]);
            const double aspect = paths::orbitAspect(parameters_.motion[1]);
            const double rotation = clampUnit(parameters_.motion[2]) * kPi;
            const double co = std::cos(rotation);
            const double so = std::sin(rotation);
            const double localX = co * x_ + so * y_;
            const double localY = -so * x_ + co * y_;
            trajectoryPhase_ = paths::wrapPhase(std::atan2(localY / std::max(1.0e-6, radius * aspect),
                                                           localX / std::max(1.0e-6, radius)));
        }
        else if (model == 8)
        {
            const double throwSpeed = std::hypot(vx_, vy_);
            noiseX_ = clamp(std::max(noiseX_, std::hypot(x_, y_) + throwSpeed * 0.035), 0.0, 1.0);
            noiseY_ = std::atan2(y_, x_);
            const double cross = x_ * vy_ - y_ * vx_;
            pendulumVelocity_ = cross < 0.0 ? -1.0 : 1.0;
        }
    }

    const double a = clampUnit(parameters_.motion[0]);
    const double b = clampUnit(parameters_.motion[1]);
    const double c = clampUnit(parameters_.motion[2]);
    const double d = clampUnit(parameters_.motion[3]);
    const double energy = clamp(parameters_.energy, 0.0, 2.5);

    const bool hit = hitPending_.exchange(false, std::memory_order_acq_rel);
    if (hit)
    {
        impactEnvelope_ = 1.0;

        switch (model)
        {
            case 0:
            case 6:
                // Deterministic trajectory modes do not leave their displayed path.
                break;

            case 2:
            {
                constexpr double anchorY = 0.12;
                const double dx = x_;
                const double dy = y_ - anchorY;
                const double radius = std::max(1.0e-6, std::hypot(dx, dy));
                const double tx = -dy / radius;
                const double ty = dx / radius;
                const double force = (1.0 + 4.5 * energy) * (0.55 + 0.45 * a);
                vx_ += tx * force;
                vy_ += ty * force;
                break;
            }

            case 7:
            {
                const double angle = d * kTwoPi;
                const double force = (1.5 + 6.8 * a) * (0.6 + 0.4 * energy);
                x_ = 0.0;
                y_ = 0.0;
                vx_ = std::cos(angle) * force;
                vy_ = std::sin(angle) * force;
                break;
            }

            case 8:
            {
                noiseX_ = clamp((0.24 + 0.72 * a) * (0.65 + 0.35 * energy), 0.08, 0.98);
                noiseY_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;
                pendulumVelocity_ = randomSigned() < 0.0 ? -1.0 : 1.0;
                vx_ = vy_ = 0.0;
                break;
            }

            default:
            {
                const double direction = std::atan2(y_, x_) + 0.62;
                const double force = 0.7 + 2.8 * energy;
                vx_ += std::cos(direction) * force;
                vy_ += std::sin(direction) * force;
                break;
            }
        }
    }

    switch (model)
    {
        case 0:
        {
            trajectoryPhase_ = paths::wrapPhase(trajectoryPhase_ + paths::kTwoPi * paths::orbitRateHz(d) * dt);
            const auto point = paths::orbitPoint(a, b, c, trajectoryPhase_);
            const auto velocity = paths::orbitVelocity(a, b, c, d, trajectoryPhase_);
            x_ = point.x;
            y_ = point.y;
            vx_ = velocity.x;
            vy_ = velocity.y;
            break;
        }

        case 1:
        {
            const double stiffness = 1.0 + a * 42.0;
            const double damping = 0.15 + b * 8.5;
            const double swirl = (c - 0.5) * 7.0;
            const double anchorX = (d - 0.5) * 0.9;
            const double dx = x_ - anchorX;
            const double dy = y_;
            vx_ += (-stiffness * dx - damping * vx_ - swirl * dy) * dt;
            vy_ += (-stiffness * dy - damping * vy_ + swirl * dx) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        case 2:
        {
            constexpr double anchorY = 0.12;
            const double length = 0.28 + a * 0.67;
            const double gravity = 1.0 + b * 16.0;
            const double damping = 0.08 + c * 3.0;
            const double drive = (d - 0.5) * 5.0;

            const double dx = x_;
            const double dy = y_ - anchorY;
            const double radius = std::max(1.0e-5, std::hypot(dx, dy));
            const double nx = dx / radius;
            const double ny = dy / radius;
            const double tx = -ny;
            const double ty = nx;

            double ax = tx * drive * std::sin(elapsed_ * 1.37);
            double ay = -gravity + ty * drive * std::sin(elapsed_ * 1.37);

            const double stretch = std::max(0.0, radius - length);
            if (stretch > 0.0)
            {
                const double radialVelocity = vx_ * nx + vy_ * ny;
                const double tension = stretch * (34.0 + gravity * 2.4)
                                     + std::max(0.0, radialVelocity) * (1.3 + c * 7.0);
                ax -= nx * tension;
                ay -= ny * tension;
            }

            const double dampFactor = std::exp(-damping * dt);
            vx_ = (vx_ + ax * dt) * dampFactor;
            vy_ = (vy_ + ay * dt) * dampFactor;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        case 3:
        {
            const double activity = 0.3 + a * 11.0;
            const double correlation = 0.82 + c * 0.175;
            noiseX_ = noiseX_ * correlation + randomSigned() * (1.0 - correlation);
            noiseY_ = noiseY_ * correlation + randomSigned() * (1.0 - correlation);
            const double damping = 0.25 + (1.0 - b) * 5.5;
            const double biasAngle = d * kTwoPi;
            vx_ += (noiseX_ * activity + std::cos(biasAngle) * 0.35 - damping * vx_) * dt;
            vy_ += (noiseY_ * activity + std::sin(biasAngle) * 0.35 - damping * vy_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.62 + 0.3 * b, 0.05);
            break;
        }

        case 4:
        {
            const double speed = 0.12 + a * 1.05;
            const double wander = 0.15 + b * 1.85;
            const double response = 0.7 + (1.0 - c) * 4.5;
            const double curl = (d - 0.5) * 2.2;

            driftPhaseA_ += (0.22 + b * 0.72) * dt;
            driftPhaseB_ += (0.17 + b * 0.49) * dt;
            const double headingNoise = std::sin(driftPhaseA_ * 1.31)
                                      + 0.58 * std::sin(driftPhaseB_ * 0.73 + 1.4);
            pendulumAngle_ += (headingNoise * wander + curl) * dt;

            double desiredX = std::cos(pendulumAngle_) * speed;
            double desiredY = std::sin(pendulumAngle_) * speed;

            constexpr double wallStart = 0.72;
            constexpr double wallGain = 7.0;
            if (std::abs(x_) > wallStart)
                desiredX += -std::copysign((std::abs(x_) - wallStart) * wallGain * speed, x_);
            if (std::abs(y_) > wallStart)
                desiredY += -std::copysign((std::abs(y_) - wallStart) * wallGain * speed, y_);

            vx_ += (desiredX - vx_) * response * dt;
            vy_ += (desiredY - vy_) * response * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.48, 0.0);
            break;
        }

        case 5:
        {
            const double targetSpeed = 0.25 + a * 3.8;
            const double restitution = 0.25 + b * 0.74;
            const double gravity = c * 4.5;
            const double currentSpeed = std::hypot(vx_, vy_);

            if (currentSpeed < 0.06)
            {
                vx_ = targetSpeed * 0.84;
                vy_ = targetSpeed * 0.52;
            }
            else
            {
                const double correction = (targetSpeed - currentSpeed) * 0.45;
                vx_ += vx_ / currentSpeed * correction * dt;
                vy_ += vy_ / currentSpeed * correction * dt;
            }

            vy_ -= gravity * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, restitution, d);
            break;
        }

        case 6:
        {
            trajectoryPhase_ = paths::wrapPhase(trajectoryPhase_ + paths::kTwoPi * paths::lissajousRateHz(a) * dt);
            const auto point = paths::lissajousPoint(b, c, d, trajectoryPhase_);
            const auto velocity = paths::lissajousVelocity(a, b, c, d, trajectoryPhase_);
            x_ = point.x;
            y_ = point.y;
            vx_ = velocity.x;
            vy_ = velocity.y;
            break;
        }

        case 7:
        {
            const double rebound = 2.0 + b * 28.0;
            const double decay = 0.35 + c * 6.0;
            vx_ += -rebound * x_ * dt;
            vy_ += -rebound * y_ * dt;
            const double dampFactor = std::exp(-decay * dt);
            vx_ *= dampFactor;
            vy_ *= dampFactor;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.24 + b * 0.28, 0.0);
            break;
        }

        case 8:
        {
            const double decay = 0.18 + b * 4.2;
            const double rateHz = 0.12 + c * 1.45;
            const double wobble = d * 0.38;
            const double direction = pendulumVelocity_ == 0.0 ? 1.0 : pendulumVelocity_;
            const double oldX = x_;
            const double oldY = y_;

            noiseX_ *= std::exp(-decay * dt);
            noiseY_ += direction * kTwoPi * rateHz * dt;

            const double phase = noiseY_;
            const double amplitude = noiseX_;
            x_ = amplitude * (std::cos(phase) + wobble * 0.18 * std::cos(phase * 3.0));
            y_ = amplitude * ((1.0 - wobble * 0.45) * std::sin(phase)
                            + wobble * 0.16 * std::sin(phase * 2.0));
            x_ = clampWorld(x_);
            y_ = clampWorld(y_);
            vx_ = (x_ - oldX) / std::max(1.0e-6, dt);
            vy_ = (y_ - oldY) / std::max(1.0e-6, dt);
            break;
        }

        case 9:
        {
            const double response = 0.8 + a * 28.0;
            const double damping = 0.2 + b * 8.0;
            const double gain = 0.5 + c * 5.0;
            const double transientLift = d * 0.9;
            const double targetX = clamp(audioBalance_ * 1.35, -1.0, 1.0);
            const double targetY = clamp(audioEnvelope_ * gain * 2.0 - 1.0
                                       + transientEnvelope_ * transientLift, -1.0, 1.0);
            vx_ += ((targetX - x_) * response - damping * vx_) * dt;
            vy_ += ((targetY - y_) * response - damping * vy_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        default:
            break;
    }

    // Orbit and Lissajous are deterministic path generators. Applying global
    // velocity forces to them would make the body leave the route shown in the UI.
    if (model != 0 && model != 6 && model != 8)
        applyGlobalForces(dt);

    impactEnvelope_ *= std::exp(-dt * 10.0);
    transientEnvelope_ *= std::exp(-dt * 13.0);
}

void MotionEngineCore::applyGlobalForces(double dt) noexcept
{
    const double damping = clamp(parameters_.globalDamping, 0.0, 4.0);
    const double dampFactor = std::exp(-damping * dt);
    vx_ *= dampFactor;
    vy_ *= dampFactor;

    const double audioKick = clamp(parameters_.audioKick, 0.0, 3.0);
    if (audioKick > 0.0 && transientEnvelope_ > 0.001)
    {
        const double angle = elapsed_ * 2.173 + audioBalance_ * 1.7;
        const double force = transientEnvelope_ * audioKick * 2.5;
        vx_ += std::cos(angle) * force * dt;
        vy_ += std::sin(angle) * force * dt;
    }
}

void MotionEngineCore::applyConstraint() noexcept
{
    // Constraints are intentionally a presentation/output projection. Mutating
    // model state here causes path generators such as Orbit and Lissajous to fight
    // the constraint instead of simply exposing their motion through it.
}

void MotionEngineCore::containBody(bool bounce, double restitution, double chaos) noexcept
{
    auto collide = [this, bounce, restitution, chaos](double& position, double& velocity, double normalSign)
    {
        if (position >= -1.0 && position <= 1.0)
            return;

        const double incoming = std::abs(velocity);
        position = clampWorld(position);
        impactEnvelope_ = std::max(impactEnvelope_, clampUnit(incoming * 0.35));

        if (bounce)
        {
            velocity = -velocity * restitution;
            velocity += randomSigned() * chaos * incoming * 0.55 * normalSign;
        }
        else
        {
            velocity *= -0.15;
        }
    };

    collide(x_, vx_, 1.0);
    collide(y_, vy_, -1.0);
}

void MotionEngineCore::publishSnapshot(double realDt) noexcept
{
    const auto projected = projectConstraint(parameters_.constraint, x_, y_, vx_, vy_);

    Snapshot next;
    next.x = static_cast<float>(clampWorld(projected.x));
    next.y = static_cast<float>(clampWorld(projected.y));
    next.vx = static_cast<float>(projected.vx);
    next.vy = static_cast<float>(projected.vy);

    const double speed = std::hypot(projected.vx, projected.vy);
    next.speed = static_cast<float>(clampUnit(1.0 - std::exp(-speed * 0.6)));
    next.energy = static_cast<float>(clampUnit(1.0 - std::exp(-(speed * speed) * 0.22)));
    next.radius = static_cast<float>(clampUnit(std::hypot(projected.x, projected.y) / kSqrtTwo));
    next.angle = static_cast<float>(std::fmod(std::atan2(projected.y, projected.x) / kTwoPi + 1.0, 1.0));
    next.impact = static_cast<float>(clampUnit(impactEnvelope_));
    next.audioEnvelope = static_cast<float>(clampUnit(audioEnvelope_ * 2.5));
    next.transient = static_cast<float>(clampUnit(transientEnvelope_));
    next.trajectoryPhase = static_cast<float>(paths::wrapPhase(trajectoryPhase_));

    for (int zone = 0; zone < kNumZones; ++zone)
    {
        const auto& zoneParameters = parameters_.zones[static_cast<std::size_t>(zone)];
        const double radius = std::max(0.03, zoneParameters.radius);
        const double falloff = clamp(zoneParameters.falloff, 0.1, 5.0);
        const double distance = std::hypot(projected.x - zoneParameters.x, projected.y - zoneParameters.y);
        const double normalized = clampUnit(1.0 - distance / radius);
        next.zones[static_cast<std::size_t>(zone)] = static_cast<float>(std::pow(normalized, falloff));
    }

    for (int output = 0; output < kNumOutputs; ++output)
    {
        const int source = parameters_.outputs[static_cast<std::size_t>(output)].source;
        next.outputs[static_cast<std::size_t>(output)] = transformOutput(output, sourceValue(source, next), realDt);
    }

    snapshot_.store(next);
}

float MotionEngineCore::sourceValue(int source, const Snapshot& raw) const noexcept
{
    switch (source)
    {
        case 0: return raw.x * 0.5f + 0.5f;
        case 1: return raw.y * 0.5f + 0.5f;
        case 2: return static_cast<float>(0.5 + 0.5 * std::tanh(raw.vx * 0.45));
        case 3: return static_cast<float>(0.5 + 0.5 * std::tanh(raw.vy * 0.45));
        case 4: return raw.speed;
        case 5: return raw.energy;
        case 6: return raw.radius;
        case 7: return raw.angle;
        case 8: return raw.impact;
        case 9: return raw.zones[0];
        case 10: return raw.zones[1];
        case 11: return raw.zones[2];
        case 12: return raw.zones[3];
        case 13: return raw.audioEnvelope;
        case 14: return raw.transient;
        default: return 0.5f;
    }
}

float MotionEngineCore::zoneValue(int index) const noexcept
{
    const auto projected = projectConstraint(parameters_.constraint, x_, y_, vx_, vy_);
    const auto& zone = parameters_.zones[static_cast<std::size_t>(clamp(index, 0, kNumZones - 1))];
    const double radius = std::max(0.03, zone.radius);
    const double falloff = clamp(zone.falloff, 0.1, 5.0);
    const double distance = std::hypot(projected.x - zone.x, projected.y - zone.y);
    const double normalized = clampUnit(1.0 - distance / radius);
    return static_cast<float>(std::pow(normalized, falloff));
}

float MotionEngineCore::transformOutput(int index, float source, double dt) noexcept
{
    source = clamp(source, 0.0f, 1.0f);
    const auto& output = parameters_.outputs[static_cast<std::size_t>(index)];

    switch (clamp(output.curve, 0, 4))
    {
        case 1:
            source = source * source * (3.0f - 2.0f * source);
            break;
        case 2:
            source = source * source;
            break;
        case 3:
            source = std::sqrt(source);
            break;
        case 4:
            source = source < 0.5f ? 2.0f * source * source
                                   : 1.0f - 2.0f * (1.0f - source) * (1.0f - source);
            break;
        default:
            break;
    }

    const double minimum = clampUnit(output.minimum);
    const double maximum = clampUnit(output.maximum);
    const double target = minimum + (maximum - minimum) * source;
    const double smoothingMs = clamp(output.smoothingMs, 0.0, 1000.0);
    double& smoothed = smoothedOutputs_[static_cast<std::size_t>(index)];

    if (smoothingMs <= 0.01 || dt <= 0.0)
    {
        smoothed = target;
    }
    else
    {
        const double tau = smoothingMs * 0.001;
        const double alpha = 1.0 - std::exp(-dt / tau);
        smoothed += (target - smoothed) * alpha;
    }

    return static_cast<float>(clampUnit(smoothed));
}

double MotionEngineCore::randomSigned() noexcept
{
    std::uint32_t x = randomState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState_ = x;
    return static_cast<double>(x) / static_cast<double>(UINT32_MAX) * 2.0 - 1.0;
}
} // namespace motion
