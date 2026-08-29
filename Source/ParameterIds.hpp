#pragma once

#include <nullclap/Id.hpp>
#include <array>

namespace motion::ids
{
constexpr clap_id model = nullclap::stableId("motion.model");
constexpr clap_id constraint = nullclap::stableId("motion.constraint");
constexpr clap_id timeScale = nullclap::stableId("motion.time-scale");
constexpr clap_id energy = nullclap::stableId("motion.energy");
constexpr clap_id globalDamping = nullclap::stableId("motion.global-damping");
constexpr clap_id audioKick = nullclap::stableId("motion.audio-kick");
constexpr clap_id motionA = nullclap::stableId("motion.model-a");
constexpr clap_id motionB = nullclap::stableId("motion.model-b");
constexpr clap_id motionC = nullclap::stableId("motion.model-c");
constexpr clap_id motionD = nullclap::stableId("motion.model-d");

struct Zone
{
    clap_id x;
    clap_id y;
    clap_id radius;
    clap_id falloff;
};

constexpr std::array<Zone, 4> zones {{
    { nullclap::stableId("motion.zone1.x"), nullclap::stableId("motion.zone1.y"), nullclap::stableId("motion.zone1.radius"), nullclap::stableId("motion.zone1.falloff") },
    { nullclap::stableId("motion.zone2.x"), nullclap::stableId("motion.zone2.y"), nullclap::stableId("motion.zone2.radius"), nullclap::stableId("motion.zone2.falloff") },
    { nullclap::stableId("motion.zone3.x"), nullclap::stableId("motion.zone3.y"), nullclap::stableId("motion.zone3.radius"), nullclap::stableId("motion.zone3.falloff") },
    { nullclap::stableId("motion.zone4.x"), nullclap::stableId("motion.zone4.y"), nullclap::stableId("motion.zone4.radius"), nullclap::stableId("motion.zone4.falloff") },
}};

struct Output
{
    clap_id source;
    clap_id minimum;
    clap_id maximum;
    clap_id curve;
    clap_id smoothing;
};

constexpr std::array<Output, 8> outputs {{
    { nullclap::stableId("motion.out1.source"), nullclap::stableId("motion.out1.min"), nullclap::stableId("motion.out1.max"), nullclap::stableId("motion.out1.curve"), nullclap::stableId("motion.out1.smoothing") },
    { nullclap::stableId("motion.out2.source"), nullclap::stableId("motion.out2.min"), nullclap::stableId("motion.out2.max"), nullclap::stableId("motion.out2.curve"), nullclap::stableId("motion.out2.smoothing") },
    { nullclap::stableId("motion.out3.source"), nullclap::stableId("motion.out3.min"), nullclap::stableId("motion.out3.max"), nullclap::stableId("motion.out3.curve"), nullclap::stableId("motion.out3.smoothing") },
    { nullclap::stableId("motion.out4.source"), nullclap::stableId("motion.out4.min"), nullclap::stableId("motion.out4.max"), nullclap::stableId("motion.out4.curve"), nullclap::stableId("motion.out4.smoothing") },
    { nullclap::stableId("motion.out5.source"), nullclap::stableId("motion.out5.min"), nullclap::stableId("motion.out5.max"), nullclap::stableId("motion.out5.curve"), nullclap::stableId("motion.out5.smoothing") },
    { nullclap::stableId("motion.out6.source"), nullclap::stableId("motion.out6.min"), nullclap::stableId("motion.out6.max"), nullclap::stableId("motion.out6.curve"), nullclap::stableId("motion.out6.smoothing") },
    { nullclap::stableId("motion.out7.source"), nullclap::stableId("motion.out7.min"), nullclap::stableId("motion.out7.max"), nullclap::stableId("motion.out7.curve"), nullclap::stableId("motion.out7.smoothing") },
    { nullclap::stableId("motion.out8.source"), nullclap::stableId("motion.out8.min"), nullclap::stableId("motion.out8.max"), nullclap::stableId("motion.out8.curve"), nullclap::stableId("motion.out8.smoothing") },
}};

constexpr clap_id audioInput = nullclap::stableId("motion.audio.input");
constexpr clap_id audioOutput = nullclap::stableId("motion.audio.output");
constexpr std::array<clap_id, 8> modulationOutputs {
    nullclap::stableId("motion.audio.mod1"), nullclap::stableId("motion.audio.mod2"),
    nullclap::stableId("motion.audio.mod3"), nullclap::stableId("motion.audio.mod4"),
    nullclap::stableId("motion.audio.mod5"), nullclap::stableId("motion.audio.mod6"),
    nullclap::stableId("motion.audio.mod7"), nullclap::stableId("motion.audio.mod8")
};
constexpr clap_id midiInput = nullclap::stableId("motion.midi.input");
constexpr clap_id worldRemote = nullclap::stableId("motion.remote.world");
constexpr clap_id modelRemote = nullclap::stableId("motion.remote.model");
constexpr clap_id outputRemote = nullclap::stableId("motion.remote.outputs");
} // namespace motion::ids
