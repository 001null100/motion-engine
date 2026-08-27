#include "PluginProcessor.h"
#include "PluginEditor.h"

MotionEngineAudioProcessor::MotionEngineAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    bridge = std::make_unique<BridgeEngine>(parameters);
    bridge->start();
}

MotionEngineAudioProcessor::~MotionEngineAudioProcessor() = default;

void MotionEngineAudioProcessor::prepareToPlay(double, int) {}
void MotionEngineAudioProcessor::releaseResources() {}

bool MotionEngineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void MotionEngineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    // Prototype is deliberately transparent: audio passes through unchanged.
}

juce::AudioProcessorEditor* MotionEngineAudioProcessor::createEditor()
{
    return new MotionEngineAudioProcessorEditor(*this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MotionEngineAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>("source", "Debug Source",
        juce::StringArray { "Spring", "Sine", "Ramp", "Step", "Impulse" }, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("bridgeRate", "Bridge Rate",
        juce::StringArray { "30 Hz", "60 Hz", "120 Hz", "250 Hz", "500 Hz", "1000 Hz" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>("frequency", "Frequency",
        juce::NormalisableRange<float>(0.05f, 30.0f, 0.001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("stiffness", "Spring Stiffness",
        juce::NormalisableRange<float>(0.5f, 80.0f, 0.01f, 0.4f), 18.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("damping", "Spring Damping",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.01f), 2.4f));
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
