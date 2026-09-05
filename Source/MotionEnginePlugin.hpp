#pragma once

#include "BridgeEngine.h"
#include "MotionEngineCore.h"
#include "ParameterIds.hpp"
#include <nullclap/Plugin.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>

class MotionEnginePlugin final : public nullclap::Plugin
{
public:
    static const clap_plugin_descriptor_t& descriptor() noexcept;
    explicit MotionEnginePlugin(const clap_host_t* host);
    ~MotionEnginePlugin() override = default;
    double parameterValue(clap_id id) const noexcept;
    int parameterInt(clap_id id) const noexcept;
    double effectiveParameterValue(clap_id id) const noexcept { return parameters().effectiveValue(id); }
    int effectiveParameterInt(clap_id id) const noexcept;
    motion::MotionEngineCore& motionCore() noexcept { return core_; }
    const motion::MotionEngineCore& motionCore() const noexcept { return core_; }
    BridgeEngine& bridge() noexcept { return bridge_; }

    void beginUiEdit(clap_id id) noexcept;
    void setUiValue(clap_id id, double value) noexcept;
    void endUiEdit(clap_id id) noexcept;
    void setUiValueOnce(clap_id id, double value) noexcept;
    void serviceUiEdits() noexcept;
    void hitFromUi() noexcept;
    void resetMotionFromUi() noexcept;
    std::string midiActivityText() const;

private:
    bool onActivate(double, std::uint32_t, std::uint32_t) noexcept override;
    void onReset() noexcept override;
    void onMainThreadCallback() noexcept override;
    void processAudio(const clap_process_t&, std::uint32_t, std::uint32_t) noexcept override;
    void onEvent(const clap_event_header_t&) noexcept override;
    bool loadExtraState(std::span<const std::byte>) override;
    void registerParameters();
    void registerPorts();
    void registerRemoteControls();
    motion::Parameters readCoreParameters() const noexcept;
    void resetControlState() noexcept;
    void copyMainAudio(const clap_process_t&, std::uint32_t, std::uint32_t) noexcept;
    struct UiEdit
    {
        clap_id id=CLAP_INVALID_ID;
        bool active=false, began=false, hasValue=false, end=true;
        double value=0.0;
    };
    UiEdit* uiEdit(clap_id) noexcept;
    void requestUiService() noexcept;

    motion::MotionEngineCore core_;
    BridgeEngine bridge_;
    double sampleRate_=48000.0;
    // Persistent 240 Hz clock and causal interpolation, independent of host blocks
    // and event-list splits. The stereo input is never delayed or processed.
    double controlPhase_=0.0, leftSquares_=0.0, rightSquares_=0.0;
    std::uint32_t analysisFrames_=0;
    int analysisChannels_=0;
    std::array<float, motion::kNumOutputs> cvStart_ {}, cvTarget_ {};
    std::array<UiEdit, 96> uiEdits_ {}; // Main thread only.
    std::atomic<bool> resetRequested_ {false}, uiServiceRequested_ {false}, hitRequested_ {false};
    std::atomic<std::uint32_t> midiHits_ {0}, lastMidi_ {0};
};
