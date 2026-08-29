#pragma once

#include "BridgeEngine.h"
#include "MotionEngineCore.h"
#include "ParameterIds.hpp"

#include <nullclap/Plugin.hpp>

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

    motion::MotionEngineCore& motionCore() noexcept { return core_; }
    const motion::MotionEngineCore& motionCore() const noexcept { return core_; }
    BridgeEngine& bridge() noexcept { return bridge_; }

private:
    bool onActivate(double sampleRate, std::uint32_t minFrames, std::uint32_t maxFrames) noexcept override;
    void onReset() noexcept override;
    void processAudio(const clap_process_t& process,
                      std::uint32_t startFrame,
                      std::uint32_t endFrame) noexcept override;
    void onEvent(const clap_event_header_t& event) noexcept override;
    bool loadExtraState(std::span<const std::byte> bytes) override;

    void registerParameters();
    void registerPorts();
    void registerRemoteControls();
    motion::Parameters readCoreParameters() const noexcept;
    motion::AudioAnalysis analyseSpan(const clap_process_t& process,
                                      std::uint32_t startFrame,
                                      std::uint32_t endFrame) const noexcept;
    void copyMainAudio(const clap_process_t& process,
                       std::uint32_t startFrame,
                       std::uint32_t endFrame) noexcept;
    void writeModulationOutputs(const clap_process_t& process,
                                std::uint32_t startFrame,
                                std::uint32_t endFrame,
                                const std::array<float, motion::kNumOutputs>& start,
                                const std::array<float, motion::kNumOutputs>& end) noexcept;

    motion::MotionEngineCore core_;
    BridgeEngine bridge_;
    double sampleRate_ = 48000.0;
};
