#include "MotionEngineCore.h"

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
        case 4: return { "Drift Speed", "Strength", "Inertia", "Curl" };
        case 5: return { "Travel Speed", "Restitution", "Gravity", "Impact Chaos" };
        case 6: return { "Strength", "Falloff", "Orbit Bias", "Polarity" };
        case 7: return { "Blast Force", "Drag", "Return", "Spin" };
        case 8: return { "Initial Energy", "Decay", "Return", "Wobble" };
        case 9: return { "Attraction", "Damping", "Audio Gain", "Transient Pull" };
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
        return;

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
        audioBalance_ *= 0.9;
    }

    const double rise = std::max(0.0, audioEnvelope_ - previousAudioEnvelope_);
    transientEnvelope_ = std::max(transientEnvelope_ * 0.72, clamp(rise * 18.0, 0.0, 1.0));
    previousAudioEnvelope_ = audioEnvelope_;
}

void MotionEngineCore::resetForModel(int model) noexcept
{
    lastModel_ = clamp(model, 0, 9);
    vx_ = vy_ = 0.0;
    pendulumVelocity_ = 0.0;
    noiseX_ = noiseY_ = 0.0;
    driftPhaseA_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;
    driftPhaseB_ = (randomSigned() * 0.5 + 0.5) * kTwoPi;

    switch (lastModel_)
    {
        case 0:
        {
            const double radius = 0.22 + clampUnit(parameters_.motion[0]) * 0.68;
            const double rotation = clampUnit(parameters_.motion[2]) * kPi;
            const double speed = 0.18 + clampUnit(parameters_.motion[3]) * 1.7;
            x_ = std::cos(rotation) * radius;
            y_ = std::sin(rotation) * radius;
            vx_ = -std::sin(rotation) * speed;
            vy_ = std::cos(rotation) * speed;
            break;
        }
        case 1: x_ = 0.72; y_ = -0.18; break;
        case 2: pendulumAngle_ = 0.72; x_ = 0.5; y_ = -0.55; break;
        case 3: x_ = 0.0; y_ = 0.0; break;
        case 4: x_ = -0.42; y_ = 0.18; break;
        case 5: x_ = -0.55; y_ = -0.25; vx_ = 1.15; vy_ = 0.78; break;
        case 6: x_ = 0.78; y_ = 0.32; vy_ = 0.4; break;
        case 7: x_ = 0.08; y_ = 0.04; hitPending_.store(true, std::memory_order_relaxed); break;
        case 8: x_ = -0.08; y_ = 0.05; hitPending_.store(true, std::memory_order_relaxed); break;
        case 9: x_ = 0.0; y_ = -0.65; break;
        default: break;
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
        if (model == 2)
            pendulumAngle_ = std::atan2(x_, -y_);
        applyConstraint();
        return;
    }

    if (throwPending_.exchange(false, std::memory_order_acq_rel))
    {
        vx_ = throwVX_.load(std::memory_order_relaxed);
        vy_ = throwVY_.load(std::memory_order_relaxed);
        if (model == 2)
            pendulumVelocity_ += vx_ * 0.7;
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
        const double direction = std::atan2(y_, x_) + 0.62;
        switch (model)
        {
            case 0:
            {
                const double force = 0.7 + 1.7 * energy;
                const double hitDirection = std::atan2(y_, x_) + 0.48;
                vx_ += std::cos(hitDirection) * force;
                vy_ += std::sin(hitDirection) * force;
                break;
            }
            case 2: pendulumVelocity_ += (1.4 + 5.0 * a) * energy; break;
            case 7:
            {
                const double radial = std::hypot(x_, y_);
                const double nx = radial > 1.0e-5 ? x_ / radial : std::cos(direction);
                const double ny = radial > 1.0e-5 ? y_ / radial : std::sin(direction);
                const double force = (1.0 + 7.0 * a) * energy;
                vx_ += nx * force - ny * (d - 0.5) * 3.0;
                vy_ += ny * force + nx * (d - 0.5) * 3.0;
                break;
            }
            case 8:
            {
                const double force = (0.8 + 6.0 * a) * energy;
                vx_ += std::cos(direction) * force;
                vy_ += std::sin(direction) * force;
                break;
            }
            default:
            {
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
            const double targetRadius = 0.22 + a * 0.68;
            const double aspect = 1.0 - b * 0.65;
            const double orientation = c * kPi;
            const double co = std::cos(orientation);
            const double so = std::sin(orientation);
            const double lx = co * x_ + so * y_;
            const double ly = -so * x_ + co * y_;
            const double lvx = co * vx_ + so * vy_;
            const double lvy = -so * vx_ + co * vy_;
            const double qx = lx;
            const double qy = ly / aspect;
            const double ellipseRadius = std::max(1.0e-5, std::hypot(qx, qy));
            double rx = qx / ellipseRadius;
            double ry = qy / (ellipseRadius * aspect);
            const double normalLength = std::max(1.0e-5, std::hypot(rx, ry));
            rx /= normalLength;
            ry /= normalLength;
            const double tx = -ry;
            const double ty = rx;
            const double radialVelocity = lvx * rx + lvy * ry;
            const double tangentialVelocity = lvx * tx + lvy * ty;
            const double radialError = ellipseRadius - targetRadius;
            constexpr double pull = 8.0;
            constexpr double radialDamping = 3.0;
            constexpr double tangentDrive = 2.0;
            const double targetSpeed = 0.18 + d * 1.7;
            const double radialAcceleration = -pull * radialError - radialDamping * radialVelocity;
            const double tangentAcceleration = (targetSpeed - tangentialVelocity) * tangentDrive;
            const double lax = rx * radialAcceleration + tx * tangentAcceleration;
            const double lay = ry * radialAcceleration + ty * tangentAcceleration;
            vx_ += (co * lax - so * lay) * dt;
            vy_ += (so * lax + co * lay) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.52, 0.0);
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
            const double length = 0.28 + a * 0.67;
            const double gravity = 1.0 + b * 16.0;
            const double damping = 0.08 + c * 5.2;
            const double drive = (d - 0.5) * 2.2;
            const double angularAcceleration = -(gravity / length) * std::sin(pendulumAngle_)
                                             - damping * pendulumVelocity_
                                             + drive * std::sin(elapsed_ * 1.37);
            pendulumVelocity_ += angularAcceleration * dt;
            pendulumAngle_ += pendulumVelocity_ * dt;
            const double oldX = x_;
            const double oldY = y_;
            x_ = std::sin(pendulumAngle_) * length;
            y_ = -std::cos(pendulumAngle_) * length + 0.12;
            vx_ = (x_ - oldX) / std::max(1.0e-6, dt);
            vy_ = (y_ - oldY) / std::max(1.0e-6, dt);
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
            const double speed = 0.05 + a * 0.5;
            const double strength = 0.15 + b * 2.6;
            const double damping = 0.35 + (1.0 - c) * 3.0;
            const double curl = (d - 0.5) * 2.6;
            driftPhaseA_ += speed * dt;
            driftPhaseB_ += speed * 0.713 * dt;
            const double fieldX = std::sin(driftPhaseA_ + y_ * 2.1) + std::cos(driftPhaseB_ * 0.7);
            const double fieldY = std::cos(driftPhaseB_ + x_ * 1.8) - std::sin(driftPhaseA_ * 0.63);
            vx_ += (fieldX * strength - damping * vx_ - curl * y_) * dt;
            vy_ += (fieldY * strength - damping * vy_ + curl * x_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.78, 0.0);
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
            vy_ += gravity * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, restitution, d);
            break;
        }
        case 6:
        {
            const double distance = std::max(0.06, std::hypot(x_, y_));
            const double strength = 0.4 + a * 12.0;
            const double falloff = 0.5 + b * 2.8;
            const double orbitBias = (c - 0.5) * 8.0;
            const double polarity = d * 2.0 - 1.0;
            const double magnitude = strength / std::pow(distance + 0.18, falloff);
            const double nx = -x_ / distance;
            const double ny = -y_ / distance;
            vx_ += (nx * magnitude * polarity - ny * orbitBias - vx_ * 0.8) * dt;
            vy_ += (ny * magnitude * polarity + nx * orbitBias - vy_ * 0.8) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.8, 0.0);
            break;
        }
        case 7:
        {
            const double drag = 0.15 + b * 6.0;
            const double returnForce = c * 5.5;
            const double spin = (d - 0.5) * 4.0;
            vx_ += (-returnForce * x_ - drag * vx_ - spin * y_) * dt;
            vy_ += (-returnForce * y_ - drag * vy_ + spin * x_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(true, 0.42, 0.18);
            break;
        }
        case 8:
        {
            const double decay = 0.25 + b * 7.5;
            const double returnForce = c * 4.0;
            const double wobble = (d - 0.5) * 5.0;
            vx_ += (-returnForce * x_ - decay * vx_ - wobble * y_) * dt;
            vy_ += (-returnForce * y_ - decay * vy_ + wobble * x_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(false, 0.0, 0.0);
            break;
        }
        case 9:
        {
            const double attraction = 0.8 + a * 28.0;
            const double damping = 0.2 + b * 8.0;
            const double gain = 0.5 + c * 5.0;
            const double transientPull = d * 1.6;
            const double targetX = clamp(audioBalance_ + transientEnvelope_ * transientPull - transientPull * 0.3, -1.0, 1.0);
            const double targetY = clamp(audioEnvelope_ * gain * 2.0 - 1.0, -1.0, 1.0);
            vx_ += ((targetX - x_) * attraction - damping * vx_) * dt;
            vy_ += ((targetY - y_) * attraction - damping * vy_) * dt;
            x_ += vx_ * dt;
            y_ += vy_ * dt;
            containBody(false, 0.0, 0.0);
            break;
        }
        default: break;
    }

    applyGlobalForces(dt);
    applyConstraint();
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
    switch (clamp(parameters_.constraint, 0, 4))
    {
        case 1: y_ = 0.0; vy_ = 0.0; break;
        case 2: x_ = 0.0; vx_ = 0.0; break;
        case 3:
        {
            const double projectedPosition = (x_ + y_) * 0.5;
            const double projectedVelocity = (vx_ + vy_) * 0.5;
            x_ = y_ = projectedPosition;
            vx_ = vy_ = projectedVelocity;
            break;
        }
        case 4:
        {
            constexpr double radius = 0.72;
            const double currentRadius = std::hypot(x_, y_);
            if (currentRadius < 1.0e-6)
            {
                x_ = radius;
                y_ = 0.0;
            }
            else
            {
                const double nx = x_ / currentRadius;
                const double ny = y_ / currentRadius;
                x_ = nx * radius;
                y_ = ny * radius;
                const double tangentVelocity = vx_ * (-ny) + vy_ * nx;
                vx_ = -ny * tangentVelocity;
                vy_ = nx * tangentVelocity;
            }
            break;
        }
        default: break;
    }
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
    Snapshot next;
    next.x = static_cast<float>(clampWorld(x_));
    next.y = static_cast<float>(clampWorld(y_));
    next.vx = static_cast<float>(vx_);
    next.vy = static_cast<float>(vy_);
    const double speed = std::hypot(vx_, vy_);
    next.speed = static_cast<float>(clampUnit(1.0 - std::exp(-speed * 0.6)));
    next.energy = static_cast<float>(clampUnit(1.0 - std::exp(-(speed * speed) * 0.22)));
    next.radius = static_cast<float>(clampUnit(std::hypot(x_, y_) / kSqrtTwo));
    next.angle = static_cast<float>(std::fmod(std::atan2(y_, x_) / kTwoPi + 1.0, 1.0));
    next.impact = static_cast<float>(clampUnit(impactEnvelope_));
    next.audioEnvelope = static_cast<float>(clampUnit(audioEnvelope_ * 2.5));
    next.transient = static_cast<float>(clampUnit(transientEnvelope_));

    for (int zone = 0; zone < kNumZones; ++zone)
        next.zones[static_cast<std::size_t>(zone)] = zoneValue(zone);

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
    const auto& zone = parameters_.zones[static_cast<std::size_t>(clamp(index, 0, kNumZones - 1))];
    const double radius = std::max(0.03, zone.radius);
    const double falloff = clamp(zone.falloff, 0.1, 5.0);
    const double distance = std::hypot(x_ - zone.x, y_ - zone.y);
    const double normalized = clampUnit(1.0 - distance / radius);
    return static_cast<float>(std::pow(normalized, falloff));
}

float MotionEngineCore::transformOutput(int index, float source, double dt) noexcept
{
    source = clamp(source, 0.0f, 1.0f);
    const auto& output = parameters_.outputs[static_cast<std::size_t>(index)];
    switch (clamp(output.curve, 0, 4))
    {
        case 1: source = source * source * (3.0f - 2.0f * source); break;
        case 2: source = source * source; break;
        case 3: source = std::sqrt(source); break;
        case 4:
            source = source < 0.5f ? 2.0f * source * source
                                   : 1.0f - 2.0f * (1.0f - source) * (1.0f - source);
            break;
        default: break;
    }

    const double minimum = clampUnit(output.minimum);
    const double maximum = clampUnit(output.maximum);
    const double target = minimum + (maximum - minimum) * source;
    const double smoothingMs = clamp(output.smoothingMs, 0.0, 1000.0);
    double& smoothed = smoothedOutputs_[static_cast<std::size_t>(index)];
    if (smoothingMs <= 0.01 || dt <= 0.0)
        smoothed = target;
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
    // Small deterministic xorshift generator. It lives exclusively on the audio
    // thread, so no locks or library RNG state are needed in the hot path.
    std::uint32_t x = randomState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState_ = x;
    return static_cast<double>(x) / static_cast<double>(UINT32_MAX) * 2.0 - 1.0;
}
} // namespace motion
