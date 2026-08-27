#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

class MotionCanvas final : public juce::Component
{
public:
    explicit MotionCanvas(MotionEngineAudioProcessor& processor);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void tick();
    void setSelectedZone(int zone);
    std::function<void(int)> onZoneSelected;

private:
    enum class DragMode { none, body, zoneMove, zoneRadius };

    juce::Rectangle<float> worldBounds() const;
    juce::Point<float> worldToScreen(float x, float y) const;
    juce::Point<float> screenToWorld(juce::Point<float> point) const;
    float worldRadiusToPixels(float radius) const;
    void setParameterPlain(const juce::String& id, float value);
    void beginZoneGesture();
    void endZoneGesture();

    MotionEngineAudioProcessor& processor;
    DragMode dragMode = DragMode::none;
    int dragZone = -1;
    int selectedZone = 0;
    juce::Point<float> lastDragWorld;
    double lastDragTimeMs = 0.0;
    juce::Point<float> flickVelocity;
    std::vector<juce::Point<float>> trail;
};

class OutputStrip final : public juce::Component
{
public:
    OutputStrip(MotionEngineAudioProcessor& processor, int index);

    void paint(juce::Graphics&) override;
    void resized() override;
    void update(const motion::MotionEngineCore::Snapshot& snapshot, const BridgeEngine::SlotStatus& status);

private:
    void configureCompactSlider(juce::Slider& slider);

    MotionEngineAudioProcessor& processor;
    int index = 0;
    float currentValue = 0.5f;

    juce::Label indexLabel;
    juce::ComboBox sourceBox;
    juce::Slider minSlider;
    juce::Slider maxSlider;
    juce::ComboBox curveBox;
    juce::Slider smoothSlider;
    juce::TextButton mapButton { "MAP" };
    juce::TextButton clearButton { "×" };
    juce::Label targetLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> minAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> curveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttachment;
};

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
    void configureLabel(juce::Label& label, const juce::String& text);
    void updateModelLabels();
    void bindSelectedZone(int zone);

    MotionEngineAudioProcessor& processor;
    MotionCanvas canvas;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label bridgeLabel;
    juce::Label outputsTitleLabel;

    juce::ComboBox modelBox;
    juce::ComboBox constraintBox;
    juce::TextButton hitButton { "HIT" };
    juce::TextButton resetButton { "RESET" };

    juce::Slider timeSlider;
    juce::Slider energySlider;
    juce::Slider dampingSlider;
    juce::Slider audioKickSlider;
    juce::Label timeLabel;
    juce::Label energyLabel;
    juce::Label dampingLabel;
    juce::Label audioKickLabel;

    std::array<juce::Slider, 4> motionSliders;
    std::array<juce::Label, 4> motionLabels;

    juce::ComboBox zoneBox;
    juce::Slider zoneRadiusSlider;
    juce::Slider zoneFalloffSlider;
    juce::Label zoneLabel;
    juce::Label zoneRadiusLabel;
    juce::Label zoneFalloffLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> constraintAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> energyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> audioKickAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> motionAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> zoneRadiusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> zoneFalloffAttachment;

    std::array<std::unique_ptr<OutputStrip>, motion::kNumOutputs> outputStrips;
    int displayedModel = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionEngineAudioProcessorEditor)
};
