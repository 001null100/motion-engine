#include "MotionEngineCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
bool finite(float value) noexcept
{
    return std::isfinite(static_cast<double>(value));
}

bool normalized(float value) noexcept
{
    return finite(value) && value >= -1.0e-4f && value <= 1.0001f;
}

bool validate(const motion::MotionEngineCore::Snapshot& snapshot, int model, int step)
{
    auto fail = [model, step](const std::string& message)
    {
        std::cerr << "model " << model << ", step " << step << ": " << message << '\n';
        return false;
    };

    if (!finite(snapshot.x) || !finite(snapshot.y) || !finite(snapshot.vx) || !finite(snapshot.vy))
        return fail("non-finite position or velocity");
    if (snapshot.x < -1.001f || snapshot.x > 1.001f || snapshot.y < -1.001f || snapshot.y > 1.001f)
        return fail("body escaped the world bounds");

    if (!normalized(snapshot.speed) || !normalized(snapshot.energy) || !normalized(snapshot.radius)
        || !normalized(snapshot.angle) || !normalized(snapshot.impact)
        || !normalized(snapshot.audioEnvelope) || !normalized(snapshot.transient))
        return fail("normalized snapshot field left 0..1");

    for (const float zone : snapshot.zones)
        if (!normalized(zone))
            return fail("zone response left 0..1");
    for (const float output : snapshot.outputs)
        if (!normalized(output))
            return fail("motion output left 0..1");
    return true;
}
} // namespace

int main()
{
    motion::MotionEngineCore core;
    core.prepare(48000.0);

    for (int model = 0; model < 10; ++model)
    {
        motion::Parameters parameters;
        parameters.model = model;
        parameters.constraint = 0;
        parameters.timeScale = 1.35;
        parameters.energy = 1.4;
        parameters.globalDamping = 0.08;
        parameters.audioKick = 0.7;
        parameters.motion = {
            0.17 + 0.071 * model,
            0.83 - 0.053 * model,
            0.21 + 0.061 * model,
            0.74 - 0.041 * model,
        };
        for (auto& value : parameters.motion)
            value = std::clamp(value, 0.0, 1.0);

        parameters.outputs[0].minimum = 0.85;
        parameters.outputs[0].maximum = 0.15; // Exercise inverted ranges.
        parameters.outputs[1].smoothingMs = 0.0;
        parameters.outputs[2].smoothingMs = 250.0;

        core.setParameters(parameters);
        core.requestReset();

        constexpr double tickSeconds = 1.0 / 240.0;
        for (int step = 0; step < 2400; ++step)
        {
            motion::AudioAnalysis audio;
            audio.channels = 2;
            const double phase = static_cast<double>(step) * 0.031;
            audio.leftRms = 0.12 + 0.09 * (0.5 + 0.5 * std::sin(phase));
            audio.rightRms = 0.12 + 0.09 * (0.5 + 0.5 * std::cos(phase * 0.73));
            audio.rms = std::sqrt((audio.leftRms * audio.leftRms + audio.rightRms * audio.rightRms) * 0.5);

            if (step % 311 == 0)
                core.triggerHit();
            if (step == 700)
                core.beginDrag(-0.62f, 0.38f);
            if (step > 700 && step < 720)
                core.dragTo(-0.62f + static_cast<float>(step - 700) * 0.025f,
                            0.38f - static_cast<float>(step - 700) * 0.018f);
            if (step == 720)
                core.endDrag(2.4f, -1.7f);

            core.process(tickSeconds, audio);
            if (!validate(core.getSnapshot(), model, step))
                return 1;
        }
    }

    // Exercise every geometric constraint independently of model switching.
    for (int constraint = 1; constraint <= 4; ++constraint)
    {
        motion::Parameters parameters;
        parameters.model = 5;
        parameters.constraint = constraint;
        parameters.motion = { 0.7, 0.8, 0.25, 0.4 };
        core.setParameters(parameters);
        core.requestReset();
        for (int step = 0; step < 480; ++step)
        {
            core.process(1.0 / 240.0, {});
            if (!validate(core.getSnapshot(), 100 + constraint, step))
                return 1;
        }
    }

    std::cout << "Motion Engine core smoke tests passed\n";
    return 0;
}
