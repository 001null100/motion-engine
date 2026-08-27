#include "PluginProcessor.h"
#include "PluginEditor.h"

MotionEngineAudioProcessor::MotionEngineAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withOutput("Motion 1", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 2", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 3", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 4", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 5", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 6", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 7", juce::AudioChannelSet::mono(), true)
        .withOutput("Motion 8", juce::AudioChannelSet::mono(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    motionCore = std::make_unique<motion::MotionEngineCore>(parameters);
    bridge = std::make_unique<BridgeEngine>(*motionCore);
    bridge->start();
}

MotionEngineAudioProcessor::~MotionEngineAudioProcessor() = default;

void MotionEngineAudioProcessor::prepareToPlay(const double sampleRate, int)
{
    motionCore->prepare(sampleRate);
}

void MotionEngineAudioProcessor::releaseResources() {}

bool MotionEngineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    if (input != output || (input != juce::AudioChannelSet::mono() && input != juce::AudioChannelSet::stereo()))
        return false;

    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto set = layouts.getChannelSet(false, bus);
        if (!set.isDisabled() && set != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void MotionEngineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            motionCore->triggerHit();

    auto input = getBusBuffer(buffer, true, 0);
    const auto previousOutputs = motionCore->getOutputs();
    motionCore->processBlock(input);
    const auto currentOutputs = motionCore->getOutputs();

    auto mainOutput = getBusBuffer(buffer, false, 0);
    for (int channel = 0; channel < mainOutput.getNumChannels(); ++channel)
    {
        if (channel < input.getNumChannels())
        {
            if (mainOutput.getWritePointer(channel) != input.getReadPointer(channel))
                mainOutput.copyFrom(channel, 0, input, channel, 0, input.getNumSamples());
        }
        else
        {
            mainOutput.clear(channel, 0, mainOutput.getNumSamples());
        }
    }

    const int samples = buffer.getNumSamples();
    for (int outputIndex = 0; outputIndex < motion::kNumOutputs; ++outputIndex)
    {
        if (outputIndex + 1 >= getBusCount(false))
            break;

        auto aux = getBusBuffer(buffer, false, outputIndex + 1);
        if (aux.getNumChannels() == 0)
            continue;

        auto* data = aux.getWritePointer(0);
        const float start = previousOutputs[static_cast<size_t>(outputIndex)];
        const float end = currentOutputs[static_cast<size_t>(outputIndex)];
        for (int sample = 0; sample < samples; ++sample)
        {
            const float t = samples > 1 ? static_cast<float>(sample) / static_cast<float>(samples - 1) : 1.0f;
            const float normalized = start + (end - start) * t;
            data[sample] = normalized * 2.0f - 1.0f;
        }

        for (int channel = 1; channel < aux.getNumChannels(); ++channel)
            aux.copyFrom(channel, 0, aux, 0, 0, samples);
    }
}

juce::AudioProcessorEditor* MotionEngineAudioProcessor::createEditor()
{
    return new MotionEngineAudioProcessorEditor(*this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MotionEngineAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>("model", "Motion Model", motion::MotionEngineCore::modelNames(), 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("constraint", "Constraint", motion::MotionEngineCore::constraintNames(), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("timeScale", "Time", juce::NormalisableRange<float>(0.1f, 3.0f, 0.001f, 0.45f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("energy", "Energy", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("globalDamping", "Global Damping", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 0.12f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("audioKick", "Audio Kick", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 0.45f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("motionA", "Motion A", 0.0f, 1.0f, 0.58f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("motionB", "Motion B", 0.0f, 1.0f, 0.34f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("motionC", "Motion C", 0.0f, 1.0f, 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("motionD", "Motion D", 0.0f, 1.0f, 0.5f));

    constexpr std::array<std::array<float, 4>, motion::kNumZones> zoneDefaults {{
        {{ -0.58f,  0.52f, 0.55f, 1.35f }},
        {{  0.58f,  0.52f, 0.55f, 1.35f }},
        {{ -0.58f, -0.52f, 0.55f, 1.35f }},
        {{  0.58f, -0.52f, 0.55f, 1.35f }}
    }};

    for (int zone = 0; zone < motion::kNumZones; ++zone)
    {
        const auto prefix = "zone" + juce::String(zone + 1);
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "X", "Zone " + juce::String(zone + 1) + " X", -1.0f, 1.0f, zoneDefaults[static_cast<size_t>(zone)][0]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Y", "Zone " + juce::String(zone + 1) + " Y", -1.0f, 1.0f, zoneDefaults[static_cast<size_t>(zone)][1]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Radius", "Zone " + juce::String(zone + 1) + " Radius", juce::NormalisableRange<float>(0.08f, 1.5f, 0.001f), zoneDefaults[static_cast<size_t>(zone)][2]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Falloff", "Zone " + juce::String(zone + 1) + " Falloff", juce::NormalisableRange<float>(0.2f, 4.0f, 0.001f), zoneDefaults[static_cast<size_t>(zone)][3]));
    }

    constexpr std::array<int, motion::kNumOutputs> defaultSources { 0, 1, 4, 8, 9, 10, 11, 12 };
    for (int outputIndex = 0; outputIndex < motion::kNumOutputs; ++outputIndex)
    {
        const auto prefix = "out" + juce::String(outputIndex + 1);
        const auto label = "Motion " + juce::String(outputIndex + 1);
        layout.add(std::make_unique<juce::AudioParameterChoice>(prefix + "Source", label + " Source", motion::MotionEngineCore::sourceNames(), defaultSources[static_cast<size_t>(outputIndex)]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Min", label + " Minimum", 0.0f, 1.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Max", label + " Maximum", 0.0f, 1.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(prefix + "Curve", label + " Curve", motion::MotionEngineCore::curveNames(), 1));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Smooth", label + " Smoothing", juce::NormalisableRange<float>(0.0f, 500.0f, 0.1f, 0.4f), 12.0f));
    }

    return layout;
}

void MotionEngineAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void MotionEngineAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MotionEngineAudioProcessor();
}
