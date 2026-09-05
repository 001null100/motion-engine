#include "MotionEnginePlugin.hpp"
#ifndef MOTION_ENGINE_HEADLESS_TEST
#include "JuceGuiDelegate.hpp"
#endif

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
        "0.2.1",
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
#ifndef MOTION_ENGINE_HEADLESS_TEST
    setGuiDelegate(std::make_unique<JuceGuiDelegate>(*this));
#endif
    core_.setParameters(readCoreParameters());
#ifndef MOTION_ENGINE_HEADLESS_TEST
    bridge_.start();
#endif
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

    notePorts().addInput(nullclap::NotePortSpec::dialects(motion::ids::midiInput, "Motion MIDI",
        CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_CLAP, CLAP_NOTE_DIALECT_MIDI));
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
    if (!std::isfinite(sampleRate) || sampleRate < 1000.0 || sampleRate > 768000.0) return false;
    sampleRate_=sampleRate;
    resetRequested_.store(false, std::memory_order_release);
    resetControlState();
    return true;
}

void MotionEnginePlugin::onReset() noexcept { resetRequested_.store(true, std::memory_order_release); }

bool MotionEnginePlugin::loadExtraState(std::span<const std::byte> bytes)
{
    if (!bytes.empty()) return false; // Existing NCLP v1 states have no opaque payload.
    // Only the audio thread owns core parameters/history. CLAP state load runs on
    // the main thread and may overlap processing, so publish a request instead.
    resetMotionFromUi();
    return true;
}

void MotionEnginePlugin::resetControlState() noexcept
{
    core_.setParameters(readCoreParameters());
    core_.prepare(sampleRate_);
    cvStart_=cvTarget_=core_.getOutputs();
    controlPhase_=leftSquares_=rightSquares_=0.0;
    analysisFrames_=0; analysisChannels_=0;
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

namespace
{
double inputSample(const clap_audio_buffer_t* input, std::uint32_t channel, std::uint32_t frame) noexcept
{
    if (input==nullptr || channel>=input->channel_count) return 0.0;
    if (input->data32!=nullptr && input->data32[channel]!=nullptr) return input->data32[channel][frame];
    if (input->data64!=nullptr && input->data64[channel]!=nullptr) return input->data64[channel][frame];
    return 0.0;
}
}

void MotionEnginePlugin::copyMainAudio(const clap_process_t& process, std::uint32_t start, std::uint32_t end) noexcept
{
    if (process.audio_outputs==nullptr || process.audio_outputs_count==0) return;
    auto& output=process.audio_outputs[0];
    output.constant_mask=0;
    const auto* input=process.audio_inputs!=nullptr && process.audio_inputs_count>0?&process.audio_inputs[0]:nullptr;
    for (std::uint32_t channel=0; channel<output.channel_count; ++channel)
        for (std::uint32_t frame=start; frame<end; ++frame)
        {
            const double value=inputSample(input,channel,frame);
            if (output.data32!=nullptr && output.data32[channel]!=nullptr) output.data32[channel][frame]=static_cast<float>(value);
            else if (output.data64!=nullptr && output.data64[channel]!=nullptr) output.data64[channel][frame]=value;
        }
}

void MotionEnginePlugin::processAudio(const clap_process_t& process, std::uint32_t start, std::uint32_t end) noexcept
{
    if (start>=end) return;
    if (resetRequested_.exchange(false, std::memory_order_acq_rel)) resetControlState();
    if (hitRequested_.exchange(false,std::memory_order_acq_rel)) core_.triggerHit();
    core_.setParameters(readCoreParameters());
    copyMainAudio(process,start,end);
    const auto* input=process.audio_inputs!=nullptr && process.audio_inputs_count>0?&process.audio_inputs[0]:nullptr;
    const auto outputCount=process.audio_outputs!=nullptr?std::min<std::uint32_t>(process.audio_outputs_count,9):0;
    for (std::uint32_t port=1; port<outputCount; ++port) process.audio_outputs[port].constant_mask=0;

    constexpr double controlHz=240.0;
    for (std::uint32_t frame=start; frame<end; ++frame)
    {
        const float fraction=static_cast<float>(controlPhase_/sampleRate_);
        for (std::uint32_t port=1; port<outputCount; ++port)
        {
            auto& output=process.audio_outputs[port];
            const auto i=static_cast<std::size_t>(port-1);
            const float normalized=cvStart_[i]+(cvTarget_[i]-cvStart_[i])*fraction;
            const float value=std::clamp(normalized*2.0f-1.0f,-1.0f,1.0f);
            if (output.channel_count==0) continue;
            if (output.data32!=nullptr && output.data32[0]!=nullptr) output.data32[0][frame]=value;
            else if (output.data64!=nullptr && output.data64[0]!=nullptr) output.data64[0][frame]=value;
        }
        const auto clean=[](double value){return std::isfinite(value)?std::clamp(value,-1.0e6,1.0e6):0.0;};
        const double left=clean(inputSample(input,0,frame));
        const double right=input!=nullptr && input->channel_count>1?clean(inputSample(input,1,frame)):left;
        leftSquares_+=left*left; rightSquares_+=right*right; ++analysisFrames_;
        if (input!=nullptr) analysisChannels_=std::max(analysisChannels_,static_cast<int>(std::min(input->channel_count,2u)));
        controlPhase_+=controlHz;
        if (controlPhase_>=sampleRate_)
        {
            controlPhase_-=sampleRate_;
            motion::AudioAnalysis audio;
            audio.channels=analysisChannels_;
            audio.leftRms=std::sqrt(leftSquares_/analysisFrames_);
            audio.rightRms=std::sqrt(rightSquares_/analysisFrames_);
            audio.rms=std::sqrt((leftSquares_+rightSquares_)/(2.0*analysisFrames_));
            core_.process(1.0/controlHz,audio);
            cvStart_=cvTarget_; cvTarget_=core_.getOutputs();
            leftSquares_=rightSquares_=0.0; analysisFrames_=0; analysisChannels_=0;
        }
    }
}

void MotionEnginePlugin::onEvent(const clap_event_header_t& event) noexcept
{
    if (event.space_id!=CLAP_CORE_EVENT_SPACE_ID) return;
    int key=-1, channel=-1;
    if (event.type==CLAP_EVENT_MIDI && event.size>=sizeof(clap_event_midi_t))
    {
        const auto& midi=reinterpret_cast<const clap_event_midi_t&>(event);
        if (midi.port_index!=0 || (midi.data[0]&0xf0u)!=0x90u || midi.data[1]>127
            || midi.data[2]==0 || midi.data[2]>127) return;
        key=midi.data[1]; channel=(midi.data[0]&0x0fu)+1;
    }
    else if (event.type==CLAP_EVENT_NOTE_ON && event.size>=sizeof(clap_event_note_t))
    {
        const auto& note=reinterpret_cast<const clap_event_note_t&>(event);
        if (note.port_index!=0 || note.key<0 || note.key>127 || note.channel<0 || note.channel>15
            || note.note_id < -1 || !std::isfinite(note.velocity) || note.velocity<0 || note.velocity>1) return;
        key=note.key; channel=note.channel+1; // Native velocity zero remains an onset.
    }
    else return;
    // Apply a pending reset before the hit so the first note after state restore
    // or host reset is not erased when the next audio span begins.
    if (resetRequested_.exchange(false, std::memory_order_acq_rel)) resetControlState();
    core_.triggerHit();
    midiHits_.fetch_add(1,std::memory_order_relaxed);
    lastMidi_.store(static_cast<std::uint32_t>(key|(channel<<8)),std::memory_order_release);
}

int MotionEnginePlugin::effectiveParameterInt(clap_id id) const noexcept
{
    return static_cast<int>(std::llround(parameters().effectiveValue(id)));
}

void MotionEnginePlugin::hitFromUi() noexcept { hitRequested_.store(true,std::memory_order_release); _host.requestProcess(); }
void MotionEnginePlugin::resetMotionFromUi() noexcept { onReset(); _host.requestProcess(); }

std::string MotionEnginePlugin::midiActivityText() const
{
    const auto count=midiHits_.load(std::memory_order_relaxed);
    if (count==0) return "MIDI: no note hits received";
    const auto last=lastMidi_.load(std::memory_order_acquire);
    return "MIDI: note "+std::to_string(last&127u)+" / CH "+std::to_string(last>>8)
        +" / hits "+std::to_string(count);
}

MotionEnginePlugin::UiEdit* MotionEnginePlugin::uiEdit(clap_id id) noexcept
{
    if (!parameters().contains(id) || parameters().isReadOnly(id)) return nullptr;
    for (auto& edit:uiEdits_) if (edit.id==id) return &edit;
    for (auto& edit:uiEdits_) if (edit.id==CLAP_INVALID_ID) { edit.id=id; return &edit; }
    return nullptr;
}
void MotionEnginePlugin::beginUiEdit(clap_id id) noexcept
{
    if (auto* edit=uiEdit(id)) { edit->active=true; edit->end=false; }
    serviceUiEdits();
}
void MotionEnginePlugin::setUiValue(clap_id id, double value) noexcept
{
    if (!std::isfinite(value)) return;
    if (auto* edit=uiEdit(id)) { edit->active=edit->hasValue=true; edit->value=value; }
    serviceUiEdits();
}
void MotionEnginePlugin::endUiEdit(clap_id id) noexcept
{
    if (auto* edit=uiEdit(id)) edit->end=true;
    serviceUiEdits();
}
void MotionEnginePlugin::setUiValueOnce(clap_id id, double value) noexcept
{
    beginUiEdit(id); setUiValue(id,value); endUiEdit(id);
}
void MotionEnginePlugin::serviceUiEdits() noexcept
{
    for (auto& edit:uiEdits_)
    {
        if (!edit.active) continue;
        if (!edit.began) { if (!beginParameterGesture(edit.id)) { requestUiService(); return; } edit.began=true; }
        if (edit.hasValue) { if (!setParameterFromGui(edit.id,edit.value)) { requestUiService(); return; } edit.hasValue=false; markStateDirty(); }
        if (edit.end) { if (!endParameterGesture(edit.id)) { requestUiService(); return; } edit.active=edit.began=false; }
    }
}
void MotionEnginePlugin::requestUiService() noexcept
{
    if (!uiServiceRequested_.exchange(true,std::memory_order_acq_rel)) _host.requestCallback();
}
void MotionEnginePlugin::onMainThreadCallback() noexcept
{
    uiServiceRequested_.store(false,std::memory_order_release); serviceUiEdits();
}
