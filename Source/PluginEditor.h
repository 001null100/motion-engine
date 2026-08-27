#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MotionEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit MotionEngineAudioProcessorEditor(MotionEngineAudioProcessor&);
    ~MotionEngineAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureSlider(juce::Slider& slider, const juce::String& suffix = {});

    MotionEngineAudioProcessor& processor;

    juce::ComboBox sourceBox;
    juce::ComboBox rateBox;
    juce::Slider frequencySlider;
    juce::Slider stiffnessSlider;
    juce::Slider dampingSlider;
    juce::TextButton kickButton { "KICK SPRING" };
    juce::TextButton mapButton { "MAP TARGET" };
    juce::TextButton unmapButton { "UNMAP" };

    juce::Label titleLabel;
    juce::Label sourceLabel;
    juce::Label rateLabel;
    juce::Label frequencyLabel;
    juce::Label stiffnessLabel;
    juce::Label dampingLabel;
    juce::Label targetLabel;
    juce::Label telemetryLabel;
    juce::Label instructionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stiffnessAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionEngineAudioProcessorEditor)
};
