#include "MotionEngineCore.h"
#include <cmath>

namespace motion
{
namespace
{
constexpr double kSimulationHz = 240.0;
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;
constexpr double kSqrtTwo = 1.4142135623730951;

double clampUnit(const double value)
{
    return juce::jlimit(0.0, 1.0, value);
}

double clampWorld(const double value)
{
    return juce::jlimit(-1.0, 1.0, value);
}
}

MotionEngineCore::MotionEngineCore(juce::AudioProcessorValueTreeState& state)
    : parameters(state)
{
    reset();
}

void MotionEngineCore::prepare(const double newSampleRate)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    accumulator = 0.0;
}

void MotionEngineCore::reset()
{
    lastModel = -1;
    accumulator = 0.0;
    elapsed = 0.0;
    audioEnvelope = 0.0;
    audioBalance = 0.0;
    transientEnvelope = 0.0;
    previousAudioEnvelope = 0.0;
    impactEnvelope = 0.0;
    smoothedOutputs.fill(0.5);
    resetForModel(intParam("model"));
    publishSnapshot(1.0 / kSimulationHz);
}

MotionEngineCore::Snapshot MotionEngineCore::getSnapshot() const
{
    const std::scoped_lock lock(snapshotMutex);
    return snapshot;
}

std::array<float, kNumOutputs> MotionEngineCore::getOutputs() const
{
    const std::scoped_lock lock(snapshotMutex);
    return snapshot.outputs;
}

void MotionEngineCore::triggerHit()
{
    hitPending.store(true, std::memory_order_release);
}

void MotionEngineCore::beginDrag(const float newX, const float newY)
{
    dragX.store(juce::jlimit(-1.0f, 1.0f, newX), std::memory_order_relaxed);
    dragY.store(juce::jlimit(-1.0f, 1.0f, newY), std::memory_order_relaxed);
    dragging.store(true, std::memory_order_release);
}

void MotionEngineCore::dragTo(const float newX, const float newY)
{
    dragX.store(juce::jlimit(-1.0f, 1.0f, newX), std::memory_order_relaxed);
    dragY.store(juce::jlimit(-1.0f, 1.0f, newY), std::memory_order_relaxed);
}

void MotionEngineCore::endDrag(const float velocityX, const float velocityY)
{
    dragX.store(juce::jlimit(-1.0f, 1.0f, dragX.load()), std::memory_order_relaxed);
    dragY.store(juce::jlimit(-1.0f, 1.0f, dragY.load()), std::memory_order_relaxed);
    throwVX.store(juce::jlimit(-8.0f, 8.0f, velocityX), std::memory_order_relaxed);
    throwVY.store(juce::jlimit(-8.0f, 8.0f, velocityY), std::memory_order_relaxed);
    throwPending.store(true, std::memory_order_release);
    dragging.store(false, std::memory_order_release);
}

juce::StringArray MotionEngineCore::modelNames()
{
    return { "Orbit", "Spring", "Pendulum", "Brownian", "Drift", "Bounce", "Magnet", "Explosion", "Decay", "Follower" };
}

juce::StringArray MotionEngineCore::constraintNames()
{
    return { "Free 2D", "Horizontal", "Vertical", "Diagonal", "Circle" };
}

juce::StringArray MotionEngineCore::sourceNames()
{
    return { "X Position", "Y Position", "X Velocity", "Y Velocity", "Speed", "Energy", "Radius", "Angle",
             "Impact", "Zone 1", "Zone 2", "Zone 3", "Zone 4", "Audio Envelope", "Transient" };
}

juce::StringArray MotionEngineCore::curveNames()
{
    return { "Linear", "Smooth", "Exponential", "Logarithmic", "S Curve" };
}

std::array<juce::String, 4> MotionEngineCore::controlNamesForModel(const int model)
{
    switch (model)
    {
        case 0: return { "Orbit Speed", "Orbit Pull", "Ellipticity", "Precession" };
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

float MotionEngineCore::param(const juce::String& id) const
{
    if (const auto* value = parameters.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    return 0.0f;
}

int MotionEngineCore::intParam(const juce::String& id) const
{
    return static_cast<int>(std::lround(param(id)));
}

void MotionEngineCore::processBlock(const juce::AudioBuffer<float>& input)
{
    analyseAudio(input);

    const double blockSeconds = static_cast<double>(input.getNumSamples()) / sampleRate;
    accumulator += blockSeconds;
    const double fixedStep = 1.0 / kSimulationHz;
    int safety = 0;

    while (accumulator >= fixedStep && safety++ < 128)
    {
        const double timeScale = juce::jlimit(0.05, 4.0, static_cast<double>(param("timeScale")));
        step(fixedStep * timeScale);
        accumulator -= fixedStep;
        elapsed += fixedStep * timeScale;
    }

    publishSnapshot(blockSeconds);
}

void MotionEngineCore::analyseAudio(const juce::AudioBuffer<float>& input)
{
    if (input.getNumSamples() <= 0 || input.getNumChannels() <= 0)
        return;

    double sumSquares = 0.0;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    const int channels = input.getNumChannels();
    const int samples = input.getNumSamples();

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = input.getReadPointer(channel);
        double channelSquares = 0.0;
        for (int i = 0; i < samples; ++i)
            channelSquares += static_cast<double>(data[i]) * static_cast<double>(data[i]);

        sumSquares += channelSquares;
        if (channel == 0) leftSquares = channelSquares;
        if (channel == 1) rightSquares = channelSquares;
    }

    const double rms = std::sqrt(sumSquares / static_cast<double>(samples * channels));
    const double attack = rms > audioEnvelope ? 0.55 : 0.08;
    audioEnvelope += (rms - audioEnvelope) * attack;

    if (channels > 1)
    {
        const double left = std::sqrt(leftSquares / samples);
        const double right = std::sqrt(rightSquares / samples);
        const double total = left + right + 1.0e-9;
        const double balance = (right - left) / total;
        audioBalance += (balance - audioBalance) * 0.18;
    }
    else
    {
        audioBalance *= 0.9;
    }

    const double rise = juce::jmax(0.0, audioEnvelope - previousAudioEnvelope);
    transientEnvelope = juce::jmax(transientEnvelope * 0.72, juce::jlimit(0.0, 1.0, rise * 18.0));
    previousAudioEnvelope = audioEnvelope;
}

void MotionEngineCore::resetForModel(const int model)
{
    lastModel = juce::jlimit(0, 9, model);
    vx = vy = 0.0;
    pendulumVelocity = 0.0;
    noiseX = noiseY = 0.0;
    driftPhaseA = random.nextDouble() * kTwoPi;
    driftPhaseB = random.nextDouble() * kTwoPi;

    switch (lastModel)
    {
        case 0: x = 0.62; y = 0.0; vx = 0.0; vy = 0.9; break;
        case 1: x = 0.72; y = -0.18; break;
        case 2: pendulumAngle = 0.72; x = 0.5; y = -0.55; break;
        case 3: x = 0.0; y = 0.0; break;
        case 4: x = -0.42; y = 0.18; break;
        case 5: x = -0.55; y = -0.25; vx = 1.15; vy = 0.78; break;
        case 6: x = 0.78; y = 0.32; vy = 0.4; break;
        case 7: x = 0.08; y = 0.04; hitPending.store(true); break;
        case 8: x = -0.08; y = 0.05; hitPending.store(true); break;
        case 9: x = 0.0; y = -0.65; break;
        default: break;
    }
}

void MotionEngineCore::step(const double dt)
{
    const int model = juce::jlimit(0, 9, intParam("model"));
    if (model != lastModel)
        resetForModel(model);

    if (dragging.load(std::memory_order_acquire))
    {
        x = dragX.load(std::memory_order_relaxed);
        y = dragY.load(std::memory_order_relaxed);
        vx = vy = 0.0;
        if (model == 2)
            pendulumAngle = std::atan2(x, -y);
        applyConstraint();
        return;
    }

    if (throwPending.exchange(false, std::memory_order_acq_rel))
    {
        vx = throwVX.load(std::memory_order_relaxed);
        vy = throwVY.load(std::memory_order_relaxed);
        if (model == 2)
            pendulumVelocity += vx * 0.7;
    }

    const double a = clampUnit(param("motionA"));
    const double b = clampUnit(param("motionB"));
    const double c = clampUnit(param("motionC"));
    const double d = clampUnit(param("motionD"));
    const double energy = juce::jlimit(0.0, 2.5, static_cast<double>(param("energy")));

    const bool hit = hitPending.exchange(false, std::memory_order_acq_rel);
    if (hit)
    {
        impactEnvelope = 1.0;
        const double direction = std::atan2(y, x) + 0.62;
        switch (model)
        {
            case 0:
            {
                const double radius = juce::jmax(0.08, std::hypot(x, y));
                const double nx = x / radius;
                const double ny = y / radius;
                const double tx = -ny;
                const double ty = nx;
                const double force = 0.22 + 0.65 * energy;
                vx += nx * force + tx * force * 0.18;
                vy += ny * force + ty * force * 0.18;
                break;
            }
            case 2: pendulumVelocity += (1.4 + 5.0 * a) * energy; break;
            case 7:
            {
                const double radial = std::hypot(x, y);
                const double nx = radial > 1.0e-5 ? x / radial : std::cos(direction);
                const double ny = radial > 1.0e-5 ? y / radial : std::sin(direction);
                const double force = (1.0 + 7.0 * a) * energy;
                vx += nx * force - ny * (d - 0.5) * 3.0;
                vy += ny * force + nx * (d - 0.5) * 3.0;
                break;
            }
            case 8:
            {
                const double force = (0.8 + 6.0 * a) * energy;
                vx += std::cos(direction) * force;
                vy += std::sin(direction) * force;
                break;
            }
            default:
            {
                const double force = (0.7 + 2.8 * energy);
                vx += std::cos(direction) * force;
                vy += std::sin(direction) * force;
                break;
            }
        }
    }

    switch (model)
    {
        case 0: // Orbit
        {
            // Orbit is a smooth path-following physical system rather than a
            // generic central-force hack. Ellipticity defines the orbit shape
            // explicitly, while velocity and disturbances are still real state.
            constexpr double targetRadius = 0.62;
            const double aspect = 1.0 - c * 0.55; // 1.0 = circle, 0.45 = strong ellipse
            const double precessionRate = d * 0.14; // 0 = fixed axis
            const double orientation = elapsed * precessionRate;
            const double co = std::cos(orientation);
            const double so = std::sin(orientation);

            // Transform into the slowly rotating ellipse frame.
            const double lx = co * x + so * y;
            const double ly = -so * x + co * y;
            const double lvx = co * vx + so * vy;
            const double lvy = -so * vx + co * vy;

            // Ellipse-space radius. Scaling Y by aspect makes the target ellipse
            // become a circle in this metric, so radial correction stays smooth.
            const double qx = lx;
            const double qy = ly / aspect;
            const double ellipseRadius = juce::jmax(1.0e-5, std::hypot(qx, qy));

            // Gradient of the ellipse metric gives the local outward normal.
            double rx = qx / ellipseRadius;
            double ry = qy / (ellipseRadius * aspect);
            const double normalLength = juce::jmax(1.0e-5, std::hypot(rx, ry));
            rx /= normalLength;
            ry /= normalLength;
            const double tx = -ry;
            const double ty = rx;

            const double radialVelocity = lvx * rx + lvy * ry;
            const double tangentialVelocity = lvx * tx + lvy * ty;
            const double radialError = ellipseRadius - targetRadius;

            const double pull = 1.4 + b * 13.0;
            const double radialDamping = 1.2 + b * 4.2;
            const double targetSpeed = 0.22 + a * 1.65;
            const double tangentDrive = 1.25 + b * 1.8;

            const double radialAcceleration = -pull * radialError - radialDamping * radialVelocity;
            const double tangentAcceleration = (targetSpeed - tangentialVelocity) * tangentDrive;
            const double lax = rx * radialAcceleration + tx * tangentAcceleration;
            const double lay = ry * radialAcceleration + ty * tangentAcceleration;

            // Rotate acceleration back to world coordinates and integrate normally,
            // preserving mouse throws, HIT impulses, and other disturbances.
            vx += (co * lax - so * lay) * dt;
            vy += (so * lax + co * lay) * dt;
            x += vx * dt;
            y += vy * dt;

            // The nominal orbit lives well inside the world. This only catches
            // deliberate hard throws instead of shaping the orbit itself.
            containBody(true, 0.52, 0.0);
            break;
        }

        case 1: // Spring
        {
            const double stiffness = 1.0 + a * 42.0;
            const double damping = 0.15 + b * 8.5;
            const double swirl = (c - 0.5) * 7.0;
            const double anchorX = (d - 0.5) * 0.9;
            const double dx = x - anchorX;
            const double dy = y;
            vx += (-stiffness * dx - damping * vx - swirl * dy) * dt;
            vy += (-stiffness * dy - damping * vy + swirl * dx) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        case 2: // Pendulum
        {
            const double length = 0.28 + a * 0.67;
            const double gravity = 1.0 + b * 16.0;
            const double damping = 0.08 + c * 5.2;
            const double drive = (d - 0.5) * 2.2;
            const double angularAcceleration = -(gravity / length) * std::sin(pendulumAngle)
                                             - damping * pendulumVelocity
                                             + drive * std::sin(elapsed * 1.37);
            pendulumVelocity += angularAcceleration * dt;
            pendulumAngle += pendulumVelocity * dt;
            const double oldX = x;
            const double oldY = y;
            x = std::sin(pendulumAngle) * length;
            y = -std::cos(pendulumAngle) * length + 0.12;
            vx = (x - oldX) / juce::jmax(1.0e-6, dt);
            vy = (y - oldY) / juce::jmax(1.0e-6, dt);
            break;
        }

        case 3: // Brownian
        {
            const double activity = 0.3 + a * 11.0;
            const double correlation = 0.82 + c * 0.175;
            noiseX = noiseX * correlation + randomSigned() * (1.0 - correlation);
            noiseY = noiseY * correlation + randomSigned() * (1.0 - correlation);
            const double damping = 0.25 + (1.0 - b) * 5.5;
            const double biasAngle = d * kTwoPi;
            vx += (noiseX * activity + std::cos(biasAngle) * 0.35 - damping * vx) * dt;
            vy += (noiseY * activity + std::sin(biasAngle) * 0.35 - damping * vy) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(true, 0.62 + 0.3 * b, 0.05);
            break;
        }

        case 4: // Drift
        {
            const double speed = 0.05 + a * 0.5;
            const double strength = 0.15 + b * 2.6;
            const double damping = 0.35 + (1.0 - c) * 3.0;
            const double curl = (d - 0.5) * 2.6;
            driftPhaseA += speed * dt;
            driftPhaseB += speed * 0.713 * dt;
            const double fieldX = std::sin(driftPhaseA + y * 2.1) + std::cos(driftPhaseB * 0.7);
            const double fieldY = std::cos(driftPhaseB + x * 1.8) - std::sin(driftPhaseA * 0.63);
            vx += (fieldX * strength - damping * vx - curl * y) * dt;
            vy += (fieldY * strength - damping * vy + curl * x) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(true, 0.78, 0.0);
            break;
        }

        case 5: // Bounce
        {
            const double targetSpeed = 0.25 + a * 3.8;
            const double restitution = 0.25 + b * 0.74;
            const double gravity = c * 4.5;
            const double currentSpeed = std::hypot(vx, vy);
            if (currentSpeed < 0.06)
            {
                vx = targetSpeed * 0.84;
                vy = targetSpeed * 0.52;
            }
            else
            {
                const double correction = (targetSpeed - currentSpeed) * 0.45;
                vx += vx / currentSpeed * correction * dt;
                vy += vy / currentSpeed * correction * dt;
            }
            vy += gravity * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(true, restitution, d);
            break;
        }

        case 6: // Magnet
        {
            const double distance = juce::jmax(0.06, std::hypot(x, y));
            const double strength = 0.4 + a * 12.0;
            const double falloff = 0.5 + b * 2.8;
            const double orbitBias = (c - 0.5) * 8.0;
            const double polarity = (d * 2.0 - 1.0);
            const double magnitude = strength / std::pow(distance + 0.18, falloff);
            const double nx = -x / distance;
            const double ny = -y / distance;
            vx += (nx * magnitude * polarity - ny * orbitBias - vx * 0.8) * dt;
            vy += (ny * magnitude * polarity + nx * orbitBias - vy * 0.8) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(true, 0.8, 0.0);
            break;
        }

        case 7: // Explosion
        {
            const double drag = 0.15 + b * 6.0;
            const double returnForce = c * 5.5;
            const double spin = (d - 0.5) * 4.0;
            vx += (-returnForce * x - drag * vx - spin * y) * dt;
            vy += (-returnForce * y - drag * vy + spin * x) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(true, 0.42, 0.18);
            break;
        }

        case 8: // Decay
        {
            const double decay = 0.25 + b * 7.5;
            const double returnForce = c * 4.0;
            const double wobble = (d - 0.5) * 5.0;
            vx += (-returnForce * x - decay * vx - wobble * y) * dt;
            vy += (-returnForce * y - decay * vy + wobble * x) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        case 9: // Follower
        {
            const double attraction = 0.8 + a * 28.0;
            const double damping = 0.2 + b * 8.0;
            const double gain = 0.5 + c * 5.0;
            const double transientPull = d * 1.6;
            const double targetX = juce::jlimit(-1.0, 1.0, audioBalance + transientEnvelope * transientPull - transientPull * 0.3);
            const double targetY = juce::jlimit(-1.0, 1.0, audioEnvelope * gain * 2.0 - 1.0);
            vx += ((targetX - x) * attraction - damping * vx) * dt;
            vy += ((targetY - y) * attraction - damping * vy) * dt;
            x += vx * dt;
            y += vy * dt;
            containBody(false, 0.0, 0.0);
            break;
        }

        default: break;
    }

    applyGlobalForces(dt);
    applyConstraint();
    impactEnvelope *= std::exp(-dt * 10.0);
    transientEnvelope *= std::exp(-dt * 13.0);
}

void MotionEngineCore::applyGlobalForces(const double dt)
{
    const double damping = juce::jlimit(0.0, 4.0, static_cast<double>(param("globalDamping")));
    const double dampFactor = std::exp(-damping * dt);
    vx *= dampFactor;
    vy *= dampFactor;

    const double audioKick = juce::jlimit(0.0, 3.0, static_cast<double>(param("audioKick")));
    if (audioKick > 0.0 && transientEnvelope > 0.001)
    {
        const double angle = elapsed * 2.173 + audioBalance * 1.7;
        const double force = transientEnvelope * audioKick * 2.5;
        vx += std::cos(angle) * force * dt;
        vy += std::sin(angle) * force * dt;
    }
}

void MotionEngineCore::applyConstraint()
{
    switch (juce::jlimit(0, 4, intParam("constraint")))
    {
        case 1: y = 0.0; vy = 0.0; break;
        case 2: x = 0.0; vx = 0.0; break;
        case 3:
        {
            const double projectedPosition = (x + y) * 0.5;
            const double projectedVelocity = (vx + vy) * 0.5;
            x = y = projectedPosition;
            vx = vy = projectedVelocity;
            break;
        }
        case 4:
        {
            constexpr double radius = 0.72;
            const double currentRadius = std::hypot(x, y);
            if (currentRadius < 1.0e-6)
            {
                x = radius;
                y = 0.0;
            }
            else
            {
                const double nx = x / currentRadius;
                const double ny = y / currentRadius;
                x = nx * radius;
                y = ny * radius;
                const double tangentVelocity = vx * (-ny) + vy * nx;
                vx = -ny * tangentVelocity;
                vy = nx * tangentVelocity;
            }
            break;
        }
        default: break;
    }
}

void MotionEngineCore::containBody(const bool bounce, const double restitution, const double chaos)
{
    auto collide = [this, bounce, restitution, chaos](double& position, double& velocity, const double normalSign)
    {
        if (position >= -1.0 && position <= 1.0)
            return;

        const double incoming = std::abs(velocity);
        position = clampWorld(position);
        impactEnvelope = juce::jmax(impactEnvelope, clampUnit(incoming * 0.35));

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

    collide(x, vx, 1.0);
    collide(y, vy, -1.0);
}

void MotionEngineCore::publishSnapshot(const double realDt)
{
    Snapshot next;
    next.x = static_cast<float>(clampWorld(x));
    next.y = static_cast<float>(clampWorld(y));
    next.vx = static_cast<float>(vx);
    next.vy = static_cast<float>(vy);
    const double speed = std::hypot(vx, vy);
    next.speed = static_cast<float>(clampUnit(1.0 - std::exp(-speed * 0.6)));
    next.energy = static_cast<float>(clampUnit(1.0 - std::exp(-(speed * speed) * 0.22)));
    next.radius = static_cast<float>(clampUnit(std::hypot(x, y) / kSqrtTwo));
    next.angle = static_cast<float>(std::fmod(std::atan2(y, x) / kTwoPi + 1.0, 1.0));
    next.impact = static_cast<float>(clampUnit(impactEnvelope));
    next.audioEnvelope = static_cast<float>(clampUnit(audioEnvelope * 2.5));
    next.transient = static_cast<float>(clampUnit(transientEnvelope));

    for (int zone = 0; zone < kNumZones; ++zone)
        next.zones[static_cast<size_t>(zone)] = zoneValue(zone);

    for (int output = 0; output < kNumOutputs; ++output)
    {
        const auto sourceId = "out" + juce::String(output + 1) + "Source";
        const int source = intParam(sourceId);
        next.outputs[static_cast<size_t>(output)] = transformOutput(output, sourceValue(source, next), realDt);
    }

    const std::scoped_lock lock(snapshotMutex);
    snapshot = next;
}

float MotionEngineCore::sourceValue(const int source, const Snapshot& raw) const
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

float MotionEngineCore::zoneValue(const int index) const
{
    const auto prefix = "zone" + juce::String(index + 1);
    const double zx = param(prefix + "X");
    const double zy = param(prefix + "Y");
    const double radius = juce::jmax(0.03, static_cast<double>(param(prefix + "Radius")));
    const double falloff = juce::jlimit(0.1, 5.0, static_cast<double>(param(prefix + "Falloff")));
    const double distance = std::hypot(x - zx, y - zy);
    const double normalized = clampUnit(1.0 - distance / radius);
    return static_cast<float>(std::pow(normalized, falloff));
}

float MotionEngineCore::transformOutput(const int index, float source, const double dt)
{
    source = juce::jlimit(0.0f, 1.0f, source);
    const auto prefix = "out" + juce::String(index + 1);
    switch (juce::jlimit(0, 4, intParam(prefix + "Curve")))
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

    const double minimum = clampUnit(param(prefix + "Min"));
    const double maximum = clampUnit(param(prefix + "Max"));
    const double target = minimum + (maximum - minimum) * source;
    const double smoothingMs = juce::jlimit(0.0, 1000.0, static_cast<double>(param(prefix + "Smooth")));

    double& smoothed = smoothedOutputs[static_cast<size_t>(index)];
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

double MotionEngineCore::randomSigned()
{
    return random.nextDouble() * 2.0 - 1.0;
}
} // namespace motion
