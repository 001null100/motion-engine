#include "BridgeEngine.h"
#include <cmath>

namespace
{
constexpr int bridgePort = 19782;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

float clamp01(double value)
{
    return static_cast<float>(juce::jlimit(0.0, 1.0, value));
}
}

BridgeEngine::BridgeEngine(juce::AudioProcessorValueTreeState& state)
    : juce::Thread("Motion Engine Bridge Sender"), parameters(state)
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

void BridgeEngine::triggerImpulse()
{
    pendingImpulse.store(true, std::memory_order_release);
}

void BridgeEngine::requestMap()
{
    pendingMap.store(true, std::memory_order_release);
}

void BridgeEngine::requestUnmap()
{
    pendingUnmap.store(true, std::memory_order_release);
}

BridgeEngine::Status BridgeEngine::getStatus() const
{
    const std::scoped_lock lock(statusMutex);
    return status;
}

int BridgeEngine::rateFromChoice(const int choiceIndex)
{
    static constexpr int rates[] { 30, 60, 120, 250, 500, 1000 };
    return rates[juce::jlimit(0, 5, choiceIndex)];
}

void BridgeEngine::run()
{
    using clock = std::chrono::steady_clock;
    const auto startedAt = clock::now();
    auto previous = startedAt;
    auto nextSend = startedAt;
    auto sentWindowStart = startedAt;
    uint64_t sentInWindow = 0;

    while (!threadShouldExit())
    {
        if (pendingMap.exchange(false, std::memory_order_acq_rel))
            sendCommand("MAP");
        if (pendingUnmap.exchange(false, std::memory_order_acq_rel))
            sendCommand("UNMAP");

        const int source = static_cast<int>(parameters.getRawParameterValue("source")->load());
        const int rateChoice = static_cast<int>(parameters.getRawParameterValue("bridgeRate")->load());
        const int requestedRate = rateFromChoice(rateChoice);
        const double frequency = parameters.getRawParameterValue("frequency")->load();
        const double stiffness = parameters.getRawParameterValue("stiffness")->load();
        const double damping = parameters.getRawParameterValue("damping")->load();

        const auto now = clock::now();
        const double dt = juce::jlimit(0.0, 0.05, std::chrono::duration<double>(now - previous).count());
        const double elapsed = std::chrono::duration<double>(now - startedAt).count();
        previous = now;

        const float value = generateValue(dt, elapsed, source, frequency, stiffness, damping);
        currentValue.store(value, std::memory_order_relaxed);
        sendValue(sequence++, value, requestedRate);
        ++sentInWindow;

        receiveTelemetry();

        const double windowSeconds = std::chrono::duration<double>(now - sentWindowStart).count();
        if (windowSeconds >= 1.0)
        {
            const std::scoped_lock lock(statusMutex);
            status.sentHz = static_cast<double>(sentInWindow) / windowSeconds;
            sentInWindow = 0;
            sentWindowStart = now;
        }

        const auto period = std::chrono::duration<double>(1.0 / static_cast<double>(requestedRate));
        nextSend += std::chrono::duration_cast<clock::duration>(period);
        if (nextSend < now)
            nextSend = now;

        std::this_thread::sleep_until(nextSend);
    }
}

float BridgeEngine::generateValue(const double dt, const double timeSeconds, const int source,
                                  const double frequency, const double stiffness, const double damping)
{
    const double phase = std::fmod(timeSeconds * frequency, 1.0);

    switch (source)
    {
        case 0: // Spring
        {
            if (pendingImpulse.exchange(false, std::memory_order_acq_rel))
                springVelocity += 2.4;

            const double acceleration = -stiffness * springPosition - damping * springVelocity;
            springVelocity += acceleration * dt;
            springPosition += springVelocity * dt;
            springPosition = juce::jlimit(-1.2, 1.2, springPosition);
            return clamp01(0.5 + springPosition * 0.4);
        }
        case 1: // Sine
            return clamp01(0.5 + 0.5 * std::sin(twoPi * phase));
        case 2: // Ramp
            return clamp01(phase);
        case 3: // Step
            return phase < 0.5 ? 0.0f : 1.0f;
        case 4: // Impulse, 5 ms pulse each period
            return phase < juce::jmin(0.25, frequency * 0.005) ? 1.0f : 0.0f;
        default:
            return 0.5f;
    }
}

void BridgeEngine::sendCommand(const juce::String& command)
{
    const auto message = "ME1|" + command + "\n";
    socket.write("127.0.0.1", bridgePort, message.toRawUTF8(), static_cast<int>(message.getNumBytesAsUTF8()));
}

void BridgeEngine::sendValue(const uint64_t seq, const float value, const int requestedRate)
{
    const auto message = juce::String::formatted("ME1|VALUE|%llu|%.9f|%d\n",
                                                 static_cast<unsigned long long>(seq), value, requestedRate);
    socket.write("127.0.0.1", bridgePort, message.toRawUTF8(), static_cast<int>(message.getNumBytesAsUTF8()));
}

void BridgeEngine::receiveTelemetry()
{
    char buffer[1024] {};
    while (socket.waitUntilReady(true, 0) > 0)
    {
        const int bytes = socket.read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
        if (bytes <= 0)
            break;

        buffer[bytes] = 0;
        const juce::String line = juce::String::fromUTF8(buffer, bytes).trim();
        juce::StringArray fields;
        fields.addTokens(line, "|", "");
        if (fields.size() < 9 || fields[0] != "ME1" || fields[1] != "STATUS")
            continue;

        const std::scoped_lock lock(statusMutex);
        status.bridgeSeen = true;
        status.targetName = fields[2];
        status.receivedHz = fields[3].getDoubleValue();
        status.appliedHz = fields[4].getDoubleValue();
        status.requestedHz = fields[5].getDoubleValue();
        status.worstGapMs = fields[6].getDoubleValue();
        status.mapped = fields[7].getIntValue() != 0;
        status.armed = fields[8].getIntValue() != 0;
    }
}
