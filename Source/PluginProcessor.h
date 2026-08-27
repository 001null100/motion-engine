#pragma once

#include <JuceHeader.h>
#include <memory>
#include "MotionEngineCore.h"
#include "BridgeEngine.h"

class MotionEngineAudioProcessor final : public juce::AudioProcessor
{
public:
    MotionEngineAudioProcessor();
    ~MotionEngineAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    motion::MotionEngineCore& getMotionCore() noexcept { return *motionCore; }
    BridgeEngine& getBridge() noexcept { return *bridge; }

private:
    std::unique_ptr<motion::MotionEngineCore> motionCore;
    std::unique_ptr<BridgeEngine> bridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionEngineAudioProcessor)
};
