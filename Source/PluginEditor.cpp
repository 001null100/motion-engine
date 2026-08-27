#include "PluginEditor.h"

namespace
{
void setCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffa8b0bd));
}
}

MotionEngineAudioProcessorEditor::MotionEngineAudioProcessorEditor(MotionEngineAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(720, 500);

    titleLabel.setText("MOTION ENGINE / BITWIG BRIDGE SPIKE", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    for (auto* c : { &sourceBox, &rateBox })
        addAndMakeVisible(c);
    sourceBox.addItemList({ "Spring", "Sine", "Ramp", "Step", "Impulse" }, 1);
    rateBox.addItemList({ "30 Hz", "60 Hz", "120 Hz", "250 Hz", "500 Hz", "1000 Hz" }, 1);

    configureSlider(frequencySlider, " Hz");
    configureSlider(stiffnessSlider);
    configureSlider(dampingSlider);

    setCaption(sourceLabel, "Debug source");
    setCaption(rateLabel, "Requested bridge rate");
    setCaption(frequencyLabel, "Generator frequency");
    setCaption(stiffnessLabel, "Spring stiffness");
    setCaption(dampingLabel, "Spring damping");

    for (auto* l : { &sourceLabel, &rateLabel, &frequencyLabel, &stiffnessLabel, &dampingLabel,
                     &targetLabel, &telemetryLabel, &instructionLabel })
        addAndMakeVisible(l);

    for (auto* b : { &kickButton, &mapButton, &unmapButton })
        addAndMakeVisible(b);

    kickButton.onClick = [this] { processor.getBridge().triggerImpulse(); };
    mapButton.onClick = [this] { processor.getBridge().requestMap(); };
    unmapButton.onClick = [this] { processor.getBridge().requestUnmap(); };

    instructionLabel.setText("Plugin targets: touch/click the parameter first, then MAP TARGET.  Native Bitwig targets: press MAP TARGET, then hover the parameter.", juce::dontSendNotification);
    instructionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8d96a5));
    instructionLabel.setJustificationType(juce::Justification::centredLeft);

    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, "source", sourceBox);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters, "bridgeRate", rateBox);
    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "frequency", frequencySlider);
    stiffnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "stiffness", stiffnessSlider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters, "damping", dampingSlider);

    startTimerHz(20);
}

MotionEngineAudioProcessorEditor::~MotionEngineAudioProcessorEditor() = default;

void MotionEngineAudioProcessorEditor::configureSlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 24);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
}

void MotionEngineAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111419));
    auto area = getLocalBounds().toFloat().reduced(18.0f);
    g.setColour(juce::Colour(0xff1b2028));
    g.fillRoundedRectangle(area.withTrimmedTop(54.0f), 10.0f);

    const float value = processor.getBridge().getCurrentValue();
    const auto meter = juce::Rectangle<float>(36.0f, 400.0f, 648.0f, 14.0f);
    g.setColour(juce::Colour(0xff262c36));
    g.fillRoundedRectangle(meter, 7.0f);
    g.setColour(juce::Colour(0xffd8e7ff));
    g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * value), 7.0f);
}

void MotionEngineAudioProcessorEditor::resized()
{
    titleLabel.setBounds(24, 14, 620, 34);

    int y = 76;
    const int labelX = 36;
    const int controlX = 220;
    const int rowH = 42;
    const int controlW = 450;

    sourceLabel.setBounds(labelX, y, 170, 28); sourceBox.setBounds(controlX, y, controlW, 28); y += rowH;
    rateLabel.setBounds(labelX, y, 170, 28); rateBox.setBounds(controlX, y, controlW, 28); y += rowH;
    frequencyLabel.setBounds(labelX, y, 170, 28); frequencySlider.setBounds(controlX, y, controlW, 28); y += rowH;
    stiffnessLabel.setBounds(labelX, y, 170, 28); stiffnessSlider.setBounds(controlX, y, controlW, 28); y += rowH;
    dampingLabel.setBounds(labelX, y, 170, 28); dampingSlider.setBounds(controlX, y, controlW, 28); y += rowH + 8;

    kickButton.setBounds(36, y, 160, 32);
    mapButton.setBounds(210, y, 180, 32);
    unmapButton.setBounds(400, y, 120, 32);

    targetLabel.setBounds(36, y + 46, 648, 26);
    telemetryLabel.setBounds(36, y + 72, 648, 26);
    instructionLabel.setBounds(36, 428, 648, 52);
}

void MotionEngineAudioProcessorEditor::timerCallback()
{
    const auto s = processor.getBridge().getStatus();
    const auto bridgeText = s.bridgeSeen ? "Bridge online" : "Bridge not seen";
    const auto mappingText = s.mapped ? "mapped" : (s.armed ? "waiting for target" : "unmapped");

    targetLabel.setText(bridgeText + "  |  " + mappingText + "  |  Target: " + s.targetName,
                        juce::dontSendNotification);
    targetLabel.setColour(juce::Label::textColourId, s.bridgeSeen ? juce::Colour(0xffd8e7ff) : juce::Colour(0xffffb8a8));

    telemetryLabel.setText(juce::String::formatted("sent %.1f Hz  |  bridge rx %.1f Hz  |  applied %.1f Hz  |  requested %.0f Hz  |  worst gap %.2f ms",
                                                   s.sentHz, s.receivedHz, s.appliedHz, s.requestedHz, s.worstGapMs),
                           juce::dontSendNotification);
    telemetryLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa8b0bd));
    repaint();
}
