#pragma once

#include "MotionEnginePlugin.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class StableComboBox final : public juce::ComboBox
{
public:
    void armSyncHold(double milliseconds = 450.0) noexcept
    {
        syncHoldUntilMs_ = std::max(syncHoldUntilMs_, juce::Time::getMillisecondCounterHiRes() + milliseconds);
    }

    bool canAcceptExternalSync() const noexcept
    {
        return !isPopupActive() && juce::Time::getMillisecondCounterHiRes() >= syncHoldUntilMs_;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        armSyncHold();
        juce::ComboBox::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        armSyncHold();
        juce::ComboBox::mouseUp(event);
    }

private:
    double syncHoldUntilMs_ = 0.0;
};

class MotionCanvas final : public juce::Component
{
public:
    explicit MotionCanvas(MotionEnginePlugin& plugin);

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
    void setParameter(clap_id id, double value);
    void beginZoneGesture();
    void endZoneGesture();

    MotionEnginePlugin& plugin_;
    DragMode dragMode_ = DragMode::none;
    int dragZone_ = -1;
    int selectedZone_ = 0;
    juce::Point<float> lastDragWorld_;
    double lastDragTimeMs_ = 0.0;
    juce::Point<float> flickVelocity_;
    std::vector<juce::Point<float>> trail_;
};

class OutputStrip final : public juce::Component
{
public:
    OutputStrip(MotionEnginePlugin& plugin, int index);

    void paint(juce::Graphics&) override;
    void resized() override;
    void sync(const motion::MotionEngineCore::Snapshot& snapshot, const BridgeEngine::SlotStatus& status);

private:
    void configureCompactSlider(juce::Slider& slider);
    void setOneShot(clap_id id, double value);
    void bindSlider(juce::Slider& slider, clap_id id);

    MotionEnginePlugin& plugin_;
    int index_ = 0;
    bool syncing_ = false;
    float currentValue_ = 0.5f;

    juce::Label indexLabel_;
    StableComboBox sourceBox_;
    juce::Slider minSlider_;
    juce::Slider maxSlider_;
    StableComboBox curveBox_;
    juce::Slider smoothSlider_;
    juce::TextButton mapButton_ { "MAP" };
    juce::TextButton clearButton_ { "X" };
    juce::Label targetLabel_;
};

class MotionEngineEditor final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit MotionEngineEditor(MotionEnginePlugin& plugin);
    ~MotionEngineEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureSlider(juce::Slider& slider, const juce::String& suffix = {});
    void configureLabel(juce::Label& label, const juce::String& text);
    void bindSlider(juce::Slider& slider, clap_id id);
    void setOneShot(clap_id id, double value);
    void updateModelLabels();
    void selectZone(int zone);
    void syncControls();

    MotionEnginePlugin& plugin_;
    MotionCanvas canvas_;
    bool syncing_ = false;
    int selectedZone_ = 0;
    int displayedModel_ = -1;

    juce::Label titleLabel_;
    juce::Label betaLabel_;
    juce::Label subtitleLabel_;
    juce::Label modelInfoLabel_;
    juce::Label bridgeLabel_;
    juce::Label outputsTitleLabel_;

    StableComboBox modelBox_;
    StableComboBox constraintBox_;
    juce::TextButton hitButton_ { "HIT" };
    juce::TextButton resetButton_ { "RESET" };

    juce::Slider timeSlider_;
    juce::Slider energySlider_;
    juce::Slider dampingSlider_;
    juce::Slider audioKickSlider_;
    juce::Label timeLabel_;
    juce::Label energyLabel_;
    juce::Label dampingLabel_;
    juce::Label audioKickLabel_;

    std::array<juce::Slider, 4> motionSliders_;
    std::array<juce::Label, 4> motionLabels_;

    StableComboBox zoneBox_;
    juce::Slider zoneRadiusSlider_;
    juce::Slider zoneFalloffSlider_;
    juce::Label zoneLabel_;
    juce::Label zoneRadiusLabel_;
    juce::Label zoneFalloffLabel_;

    std::array<std::unique_ptr<OutputStrip>, motion::kNumOutputs> outputStrips_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionEngineEditor)
};