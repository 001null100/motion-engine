#include "BridgeEngine.h"
#include <chrono>

namespace
{
constexpr int bridgePort = 19782;
constexpr int sendRateHz = 120;
}

BridgeEngine::BridgeEngine(motion::MotionEngineCore& engine)
    : juce::Thread("Motion Engine Bridge Sender"), core(engine)
{
    socket.bindToPort(0, "127.0.0.1");
}

BridgeEngine::~BridgeEngine()
{
    signalThreadShouldExit();
    socket.shutdown();
    stopThread(1500);
}

void BridgeEngine::start()
{
    startThread(juce::Thread::Priority::normal);
}

void BridgeEngine::requestMap(const int slot)
{
    if (juce::isPositiveAndBelow(slot, motion::kNumOutputs))
        pendingMap[static_cast<size_t>(slot)].store(true, std::memory_order_release);
}

void BridgeEngine::requestUnmap(const int slot)
{
    if (juce::isPositiveAndBelow(slot, motion::kNumOutputs))
        pendingUnmap[static_cast<size_t>(slot)].store(true, std::memory_order_release);
}

BridgeEngine::Status BridgeEngine::getStatus() const
{
    const std::scoped_lock lock(statusMutex);
    return status;
}

void BridgeEngine::run()
{
    using clock = std::chrono::steady_clock;
    auto nextSend = clock::now();
    auto sentWindowStart = nextSend;
    uint64_t sentInWindow = 0;

    while (!threadShouldExit())
    {
        for (int slot = 0; slot < motion::kNumOutputs; ++slot)
        {
            if (pendingMap[static_cast<size_t>(slot)].exchange(false, std::memory_order_acq_rel))
                sendCommand("MAP", slot);
            if (pendingUnmap[static_cast<size_t>(slot)].exchange(false, std::memory_order_acq_rel))
                sendCommand("UNMAP", slot);
        }

        sendValues(sequence++, core.getOutputs());
        ++sentInWindow;
        receiveTelemetry();

        const auto now = clock::now();
        const double windowSeconds = std::chrono::duration<double>(now - sentWindowStart).count();
        if (windowSeconds >= 1.0)
        {
            const std::scoped_lock lock(statusMutex);
            status.sentHz = static_cast<double>(sentInWindow) / windowSeconds;
            sentInWindow = 0;
            sentWindowStart = now;
        }

        nextSend += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / sendRateHz));
        if (nextSend < now)
            nextSend = now;
        std::this_thread::sleep_until(nextSend);
    }
}

void BridgeEngine::sendCommand(const juce::String& command, const int slot)
{
    const auto message = juce::String::formatted("ME2|%s|%d\n", command.toRawUTF8(), slot);
    socket.write("127.0.0.1", bridgePort, message.toRawUTF8(), static_cast<int>(message.getNumBytesAsUTF8()));
}

void BridgeEngine::sendValues(const uint64_t seq, const std::array<float, motion::kNumOutputs>& values)
{
    juce::String message = "ME2|VALUES|" + juce::String(static_cast<unsigned long long>(seq));
    for (const auto value : values)
        message += "|" + juce::String(juce::jlimit(0.0f, 1.0f, value), 9);
    message += "\n";
    socket.write("127.0.0.1", bridgePort, message.toRawUTF8(), static_cast<int>(message.getNumBytesAsUTF8()));
}

void BridgeEngine::receiveTelemetry()
{
    char buffer[2048] {};
    while (socket.waitUntilReady(true, 0) > 0)
    {
        const int bytes = socket.read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
        if (bytes <= 0)
            break;

        buffer[bytes] = 0;
        const juce::String line = juce::String::fromUTF8(buffer, bytes).trim();
        juce::StringArray fields;
        fields.addTokens(line, "|", "");
        if (fields.size() < 8 || fields[0] != "ME2" || fields[1] != "STATUS")
            continue;

        const int mappedMask = fields[5].getIntValue();
        const int armedMask = fields[6].getIntValue();
        juce::StringArray names;
        names.addTokens(fields[7], "~", "");

        const std::scoped_lock lock(statusMutex);
        status.bridgeSeen = true;
        status.receivedHz = fields[2].getDoubleValue();
        status.appliedHz = fields[3].getDoubleValue();
        status.worstGapMs = fields[4].getDoubleValue();
        for (int slot = 0; slot < motion::kNumOutputs; ++slot)
        {
            auto& slotStatus = status.slots[static_cast<size_t>(slot)];
            slotStatus.mapped = (mappedMask & (1 << slot)) != 0;
            slotStatus.armed = (armedMask & (1 << slot)) != 0;
            slotStatus.targetName = slot < names.size() && names[slot].isNotEmpty() ? names[slot] : "None";
        }
    }
}
