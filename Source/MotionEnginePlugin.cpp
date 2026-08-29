#include "MotionEnginePlugin.hpp"
#include "JuceGuiDelegate.hpp"

#include <clap/events.h>
#include <clap/plugin-features.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
std::vector<std::string> strings(auto values)
{
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto value : values)
        result.emplace_back(value);
    return result;
}

std::string zoneModule(int zone)
{
    return "Zone " + std::to_string(zone + 1);
}

std::string outputModule(int output)
{
    return "Motion " + std::to_string(output + 1);
}

constexpr std::uint32_t automatable = CLAP_PARAM_IS_AUTOMATABLE;
} // namespace

const clap_plugin_descriptor_t& MotionEnginePlugin::descriptor() noexcept
{
    static const char* const features[] {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_STEREO,
        CLAP_PLUGIN_FEATURE_UTILITY,
        nullptr,
    };

    static const clap_plugin_descriptor_t descriptor {
        CLAP_VERSION,
        "dev.nullexo.motionengine",
        "Motion Engine",
        "Null Exo",
        "https://github.com/001null100/motion-engine",
        "",
        "",
        "0.2.0",
        "Physics-driven modulation engine",
        features,
    };
    return descriptor;
}

MotionEnginePlugin::MotionEnginePlugin(const clap_host_t* host)
    : Plugin(&descriptor(), host), bridge_(core_)
{
    registerParameters();
    registerPorts();
    registerRemoteControls();
    setGuiDelegate(std::make_unique<JuceGuiDelegate>(*this));
    core_.setParameters(readCoreParameters());
    bridge_.start();
}

double MotionEnginePlugin::parameterValue(clap_id id) const noexcept
{
    return parameters().value(id);
}

int MotionEnginePlugin::parameterInt(clap_id id) const noexcept
{
    return static_cast<int>(std::llround(parameters().value(id)));
}

void MotionEnginePlugin::registerParameters()
{
    parameters().add(nullclap::ParameterSpec::choice(
        motion::ids::model, "Motion Model", "World", strings(motion::MotionEngineCore::modelNames()), 1));
    parameters().add(nullclap::ParameterSpec::choice(
        motion::ids::constraint, "Constraint", "World", strings(motion::MotionEngineCore::constraintNames()), 0));

    auto time = nullclap::ParameterSpec::continuous(motion::ids::timeScale, "Time", "World", 0.1, 3.0, 1.0);
    time.unit = "x";
    parameters().add(std::move(time));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::energy, "Energy", "World", 0.0, 2.0, 1.0));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::globalDamping, "World Drag", "World", 0.0, 2.0, 0.12));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::audioKick, "Audio Kick", "World", 0.0, 2.0, 0.45));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::motionA, "Motion A", "Model", 0.0, 1.0, 0.58));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::motionB, "Motion B", "Model", 0.0, 1.0, 0.34));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::motionC, "Motion C", "Model", 0.0, 1.0, 0.5));
    parameters().add(nullclap::ParameterSpec::continuous(motion::ids::motionD, "Motion D", "Model", 0.0, 1.0, 0.5));

    constexpr std::array<std::array<double, 4>, motion::kNumZones> zoneDefaults {{
        {{ -0.52,  0.52, 0.38, 1.35 }},
        {{  0.52,  0.52, 0.38, 1.35 }},
        {{ -0.52, -0.52, 0.38, 1.35 }},
        {{  0.52, -0.52, 0.38, 1.35 }},
    }};

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(zone)];
        const auto module = zoneModule(zone);
        parameters().add(nullclap::ParameterSpec::continuous(id.x, "X", module, -1.0, 1.0, zoneDefaults[static_cast<std::size_t>(zone)][0]));
        parameters().add(nullclap::ParameterSpec::continuous(id.y, "Y", module, -1.0, 1.0, zoneDefaults[static_cast<std::size_t>(zone)][1]));
        parameters().add(nullclap::ParameterSpec::continuous(id.radius, "Radius", module, 0.08, 1.5, zoneDefaults[static_cast<std::size_t>(zone)][2]));
        parameters().add(nullclap::ParameterSpec::continuous(id.falloff, "Falloff", module, 0.2, 4.0, zoneDefaults[static_cast<std::size_t>(zone)][3]));
    }

    constexpr std::array<int, motion::kNumOutputs> defaultSources { 0, 1, 4, 8, 9, 10, 11, 12 };
    const auto sources = strings(motion::MotionEngineCore::sourceNames());
    const auto curves = strings(motion::MotionEngineCore::curveNames());
    for (int output = 0; output < motion::kNumOutputs; ++output)
    {
        const auto& id = motion::ids::outputs[static_cast<std::size_t>(output)];
        const auto module = outputModule(output);
        parameters().add(nullclap::ParameterSpec::choice(id.source, "Source", module, sources,
            static_cast<std::size_t>(defaultSources[static_cast<std::size_t>(output)])));
        parameters().add(nullclap::ParameterSpec::continuous(id.minimum, "Minimum", module, 0.0, 1.0, 0.0));
        parameters().add(nullclap::ParameterSpec::continuous(id.maximum, "Maximum", module, 0.0, 1.0, 1.0));
        parameters().add(nullclap::ParameterSpec::choice(id.curve, "Curve", module, curves, 1));
        auto smoothing = nullclap::ParameterSpec::continuous(id.smoothing, "Smoothing", module, 0.0, 500.0, 12.0);
        smoothing.unit = "ms";
        parameters().add(std::move(smoothing));
    }
}

void MotionEnginePlugin::registerPorts()
{
    auto input = nullclap::AudioPortSpec::stereo(motion::ids::audioInput, "Stereo Input", true);
    auto output = nullclap::AudioPortSpec::stereo(motion::ids::audioOutput, "Stereo Output", true);
    input.inPlacePair = output.id;
    output.inPlacePair = input.id;
    audioPorts().addInput(std::move(input));
    audioPorts().addOutput(std::move(output));

    for (int i = 0; i < motion::kNumOutputs; ++i)
        audioPorts().addOutput(nullclap::AudioPortSpec::mono(
            motion::ids::modulationOutputs[static_cast<std::size_t>(i)], outputModule(i)));

    notePorts().addInput(nullclap::NotePortSpec::midi(motion::ids::midiInput, "Motion MIDI"));
}

void MotionEnginePlugin::registerRemoteControls()
{
    nullclap::RemoteControlPage world;
    world.id = motion::ids::worldRemote;
    world.section = "Motion Engine";
    world.name = "World";
    world.parameters = { motion::ids::model, motion::ids::constraint, motion::ids::timeScale, motion::ids::energy,
                         motion::ids::globalDamping, motion::ids::audioKick, motion::ids::motionA, motion::ids::motionB };
    remoteControls().add(std::move(world));

    nullclap::RemoteControlPage model;
    model.id = motion::ids::modelRemote;
    model.section = "Motion Engine";
    model.name = "Model";
    model.parameters = { motion::ids::motionA, motion::ids::motionB, motion::ids::motionC, motion::ids::motionD,
                         motion::ids::timeScale, motion::ids::energy, motion::ids::globalDamping, motion::ids::audioKick };
    remoteControls().add(std::move(model));

    for (int output = 0; output < motion::kNumOutputs; ++output)
    {
        const auto& id = motion::ids::outputs[static_cast<std::size_t>(output)];
        nullclap::RemoteControlPage page;
        page.id = nullclap::stableId("motion.remote.output." + std::to_string(output + 1));
        page.section = "Motion Outputs";
        page.name = outputModule(output);
        page.parameters = { id.source, id.minimum, id.maximum, id.curve, id.smoothing };
        remoteControls().add(std::move(page));
    }
}

bool MotionEnginePlugin::onActivate(double sampleRate, std::uint32_t, std::uint32_t) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    core_.setParameters(readCoreParameters());
    core_.prepare(sampleRate_);
    return true;
}

void MotionEnginePlugin::onReset() noexcept
{
    core_.requestReset();
}

bool MotionEnginePlugin::loadExtraState(std::span<const std::byte>)
{
    core_.setParameters(readCoreParameters());
    core_.requestReset();
    return true;
}

motion::Parameters MotionEnginePlugin::readCoreParameters() const noexcept
{
    motion::Parameters result;
    result.model = static_cast<int>(std::llround(parameters().effectiveValue(motion::ids::model)));
    result.constraint = static_cast<int>(std::llround(parameters().effectiveValue(motion::ids::constraint)));
    result.timeScale = parameters().effectiveValue(motion::ids::timeScale);
    result.energy = parameters().effectiveValue(motion::ids::energy);
    result.globalDamping = parameters().effectiveValue(motion::ids::globalDamping);
    result.audioKick = parameters().effectiveValue(motion::ids::audioKick);
    result.motion = {
        parameters().effectiveValue(motion::ids::motionA),
        parameters().effectiveValue(motion::ids::motionB),
        parameters().effectiveValue(motion::ids::motionC),
        parameters().effectiveValue(motion::ids::motionD),
    };

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto& id = motion::ids::zones[static_cast<std::size_t>(zone)];
        auto& target = result.zones[static_cast<std::size_t>(zone)];
        target.x = parameters().effectiveValue(id.x);
        target.y = parameters().effectiveValue(id.y);
        target.radius = parameters().effectiveValue(id.radius);
        target.falloff = parameters().effectiveValue(id.falloff);
    }

    for (int output = 0; output < motion::kNumOutputs; ++output)
    {
        const auto& id = motion::ids::outputs[static_cast<std::size_t>(output)];
        auto& target = result.outputs[static_cast<std::size_t>(output)];
        target.source = static_cast<int>(std::llround(parameters().effectiveValue(id.source)));
        target.minimum = parameters().effectiveValue(id.minimum);
        target.maximum = parameters().effectiveValue(id.maximum);
        target.curve = static_cast<int>(std::llround(parameters().effectiveValue(id.curve)));
        target.smoothingMs = parameters().effectiveValue(id.smoothing);
    }
    return result;
}

motion::AudioAnalysis MotionEnginePlugin::analyseSpan(const clap_process_t& process,
                                                       std::uint32_t startFrame,
                                                       std::uint32_t endFrame) const noexcept
{
    motion::AudioAnalysis result;
    if (startFrame >= endFrame || process.audio_inputs_count == 0)
        return result;

    const auto& input = process.audio_inputs[0];
    const auto frames = endFrame - startFrame;
    result.channels = static_cast<int>(input.channel_count);
    if (input.channel_count == 0)
        return result;

    double totalSquares = 0.0;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    for (std::uint32_t channel = 0; channel < input.channel_count; ++channel)
    {
        double channelSquares = 0.0;
        if (input.data32 != nullptr && input.data32[channel] != nullptr)
        {
            const float* data = input.data32[channel];
            for (std::uint32_t frame = startFrame; frame < endFrame; ++frame)
                channelSquares += static_cast<double>(data[frame]) * static_cast<double>(data[frame]);
        }
        else if (input.data64 != nullptr && input.data64[channel] != nullptr)
        {
            const double* data = input.data64[channel];
            for (std::uint32_t frame = startFrame; frame < endFrame; ++frame)
                channelSquares += data[frame] * data[frame];
        }
        totalSquares += channelSquares;
        if (channel == 0) leftSquares = channelSquares;
        if (channel == 1) rightSquares = channelSquares;
    }

    result.rms = std::sqrt(totalSquares / std::max(1.0, static_cast<double>(frames) * input.channel_count));
    result.leftRms = std::sqrt(leftSquares / std::max(1.0, static_cast<double>(frames)));
    result.rightRms = input.channel_count > 1
        ? std::sqrt(rightSquares / std::max(1.0, static_cast<double>(frames)))
        : result.leftRms;
    return result;
}

void MotionEnginePlugin::copyMainAudio(const clap_process_t& process,
                                       std::uint32_t startFrame,
                                       std::uint32_t endFrame) noexcept
{
    if (process.audio_outputs_count == 0 || startFrame >= endFrame)
        return;
    auto& output = process.audio_outputs[0];
    const clap_audio_buffer_t* input = process.audio_inputs_count > 0 ? &process.audio_inputs[0] : nullptr;

    for (std::uint32_t channel = 0; channel < output.channel_count; ++channel)
    {
        if (output.data32 != nullptr && output.data32[channel] != nullptr)
        {
            float* destination = output.data32[channel];
            const float* source = input != nullptr && input->data32 != nullptr && channel < input->channel_count
                ? input->data32[channel] : nullptr;
            if (source == destination)
                continue;
            for (std::uint32_t frame = startFrame; frame < endFrame; ++frame)
                destination[frame] = source != nullptr ? source[frame] : 0.0f;
        }
        else if (output.data64 != nullptr && output.data64[channel] != nullptr)
        {
            double* destination = output.data64[channel];
            const double* source = input != nullptr && input->data64 != nullptr && channel < input->channel_count
                ? input->data64[channel] : nullptr;
            if (source == destination)
                continue;
            for (std::uint32_t frame = startFrame; frame < endFrame; ++frame)
                destination[frame] = source != nullptr ? source[frame] : 0.0;
        }
    }
}

void MotionEnginePlugin::writeModulationOutputs(const clap_process_t& process,
                                                 std::uint32_t startFrame,
                                                 std::uint32_t endFrame,
                                                 const std::array<float, motion::kNumOutputs>& start,
                                                 const std::array<float, motion::kNumOutputs>& end) noexcept
{
    if (startFrame >= endFrame)
        return;
    const auto frames = endFrame - startFrame;
    for (int index = 0; index < motion::kNumOutputs; ++index)
    {
        const auto port = static_cast<std::uint32_t>(index + 1);
        if (port >= process.audio_outputs_count)
            break;
        auto& output = process.audio_outputs[port];
        if (output.channel_count == 0)
            continue;

        for (std::uint32_t frame = startFrame; frame < endFrame; ++frame)
        {
            const float t = frames > 1
                ? static_cast<float>(frame - startFrame) / static_cast<float>(frames - 1) : 1.0f;
            const float normalized = start[static_cast<std::size_t>(index)]
                                   + (end[static_cast<std::size_t>(index)] - start[static_cast<std::size_t>(index)]) * t;
            const float bipolar = normalized * 2.0f - 1.0f;
            if (output.data32 != nullptr && output.data32[0] != nullptr)
                output.data32[0][frame] = bipolar;
            else if (output.data64 != nullptr && output.data64[0] != nullptr)
                output.data64[0][frame] = static_cast<double>(bipolar);
        }
    }
}

void MotionEnginePlugin::processAudio(const clap_process_t& process,
                                      std::uint32_t startFrame,
                                      std::uint32_t endFrame) noexcept
{
    if (startFrame >= endFrame)
        return;

    core_.setParameters(readCoreParameters());
    const auto previousOutputs = core_.getOutputs();
    const auto audio = analyseSpan(process, startFrame, endFrame);
    core_.process(static_cast<double>(endFrame - startFrame) / sampleRate_, audio);
    const auto currentOutputs = core_.getOutputs();

    copyMainAudio(process, startFrame, endFrame);
    writeModulationOutputs(process, startFrame, endFrame, previousOutputs, currentOutputs);
}

void MotionEnginePlugin::onEvent(const clap_event_header_t& event) noexcept
{
    if (event.space_id != CLAP_CORE_EVENT_SPACE_ID || event.type != CLAP_EVENT_MIDI
        || event.size < sizeof(clap_event_midi_t))
        return;

    const auto& midi = reinterpret_cast<const clap_event_midi_t&>(event);
    const std::uint8_t status = midi.data[0] & 0xf0u;
    if (status == 0x90u && midi.data[2] != 0)
        core_.triggerHit();
}
