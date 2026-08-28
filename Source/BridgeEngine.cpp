#include "BridgeEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <arpa/inet.h>
 #include <fcntl.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

namespace
{
constexpr int bridgePort = 19782;
constexpr int sendRateHz = 120;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalidSocket = -1;
#endif

NativeSocket nativeSocket(std::intptr_t value) noexcept
{
    return static_cast<NativeSocket>(value);
}

void closeSocket(NativeSocket socket) noexcept
{
#if defined(_WIN32)
    if (socket != invalidSocket)
        closesocket(socket);
#else
    if (socket != invalidSocket)
        close(socket);
#endif
}

std::string makeSessionId(const void* identity) noexcept
{
    static std::atomic<std::uint64_t> counter { 1 };
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(identity));
    std::uint64_t value = ticks ^ (address * 0x9e3779b97f4a7c15ull)
                        ^ (counter.fetch_add(1, std::memory_order_relaxed) * 0xbf58476d1ce4e5b9ull);
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;

    char buffer[17] {};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::vector<std::string> split(const std::string& text, char delimiter)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto end = text.find(delimiter, start);
        fields.emplace_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return fields;
}
} // namespace

BridgeEngine::BridgeEngine(motion::MotionEngineCore& core) noexcept
    : core_(core), sessionId_(makeSessionId(this))
{
    status_.sessionId = sessionId_;
#if defined(_WIN32)
    WSADATA data {};
    socketRuntimeReady_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    socketRuntimeReady_ = true;
#endif
    if (!socketRuntimeReady_)
        return;

    const auto socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == invalidSocket)
        return;

#if defined(_WIN32)
    u_long nonBlocking = 1;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
#else
    const int flags = fcntl(socketHandle, F_GETFL, 0);
    if (flags >= 0)
        fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
    if (::bind(socketHandle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
    {
        closeSocket(socketHandle);
        return;
    }

    socket_ = static_cast<std::intptr_t>(socketHandle);
}

BridgeEngine::~BridgeEngine()
{
    sendGoodbye();
    running_.store(false, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();

    closeSocket(nativeSocket(socket_));
    socket_ = -1;
#if defined(_WIN32)
    if (socketRuntimeReady_)
        WSACleanup();
#endif
}

void BridgeEngine::start()
{
    if (nativeSocket(socket_) == invalidSocket || running_.exchange(true, std::memory_order_acq_rel))
        return;
    thread_ = std::thread([this] { run(); });
}

void BridgeEngine::requestMap(int slot) noexcept
{
    if (slot >= 0 && slot < motion::kNumOutputs)
        pendingMap_[static_cast<std::size_t>(slot)].store(true, std::memory_order_release);
}

void BridgeEngine::requestUnmap(int slot) noexcept
{
    if (slot >= 0 && slot < motion::kNumOutputs)
        pendingUnmap_[static_cast<std::size_t>(slot)].store(true, std::memory_order_release);
}

BridgeEngine::Status BridgeEngine::getStatus() const
{
    const std::scoped_lock lock(statusMutex_);
    return status_;
}

void BridgeEngine::run() noexcept
{
    using clock = std::chrono::steady_clock;
    auto nextSend = clock::now();
    auto sentWindowStart = nextSend;
    std::uint64_t sentInWindow = 0;

    while (running_.load(std::memory_order_acquire))
    {
        for (int slot = 0; slot < motion::kNumOutputs; ++slot)
        {
            if (pendingMap_[static_cast<std::size_t>(slot)].exchange(false, std::memory_order_acq_rel))
                sendCommand("MAP", slot);
            if (pendingUnmap_[static_cast<std::size_t>(slot)].exchange(false, std::memory_order_acq_rel))
                sendCommand("UNMAP", slot);
        }

        sendValues(sequence_++, core_.getOutputs());
        ++sentInWindow;
        receiveTelemetry();

        const auto now = clock::now();
        const double windowSeconds = std::chrono::duration<double>(now - sentWindowStart).count();
        if (windowSeconds >= 1.0)
        {
            const std::scoped_lock lock(statusMutex_);
            status_.sentHz = static_cast<double>(sentInWindow) / windowSeconds;
            sentInWindow = 0;
            sentWindowStart = now;
        }

        nextSend += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / sendRateHz));
        if (nextSend < now)
            nextSend = now;
        std::this_thread::sleep_until(nextSend);
    }
}

void BridgeEngine::sendCommand(const char* command, int slot) noexcept
{
    char buffer[96] {};
    std::snprintf(buffer, sizeof(buffer), "ME3|%s|%s|%d\n", command, sessionId_.c_str(), slot);
    sendPacket(buffer);
}

void BridgeEngine::sendValues(std::uint64_t sequence, const std::array<float, motion::kNumOutputs>& values) noexcept
{
    char buffer[560] {};
    int written = std::snprintf(buffer, sizeof(buffer), "ME3|VALUES|%s|%llu",
                                sessionId_.c_str(), static_cast<unsigned long long>(sequence));
    for (const float raw : values)
    {
        if (written <= 0 || written >= static_cast<int>(sizeof(buffer)))
            return;
        const float value = std::clamp(raw, 0.0f, 1.0f);
        written += std::snprintf(buffer + written, sizeof(buffer) - static_cast<std::size_t>(written), "|%.9f", value);
    }
    if (written > 0 && written < static_cast<int>(sizeof(buffer) - 1))
    {
        buffer[written++] = '\n';
        buffer[written] = '\0';
        sendPacket(buffer);
    }
}

void BridgeEngine::sendGoodbye() noexcept
{
    if (nativeSocket(socket_) == invalidSocket)
        return;
    char buffer[64] {};
    std::snprintf(buffer, sizeof(buffer), "ME3|BYE|%s\n", sessionId_.c_str());
    sendPacket(buffer);
}

void BridgeEngine::sendPacket(const std::string& message) noexcept
{
    const auto socketHandle = nativeSocket(socket_);
    if (socketHandle == invalidSocket)
        return;

    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(bridgePort);
    inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr);
    sendto(socketHandle, message.data(), static_cast<int>(message.size()), 0,
           reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
}

void BridgeEngine::receiveTelemetry() noexcept
{
    const auto socketHandle = nativeSocket(socket_);
    if (socketHandle == invalidSocket)
        return;

    char buffer[2048] {};
    for (;;)
    {
        sockaddr_in source {};
#if defined(_WIN32)
        int sourceLength = sizeof(source);
#else
        socklen_t sourceLength = sizeof(source);
#endif
        const int bytes = static_cast<int>(recvfrom(socketHandle, buffer, static_cast<int>(sizeof(buffer) - 1), 0,
                                                    reinterpret_cast<sockaddr*>(&source), &sourceLength));
        if (bytes <= 0)
            break;

        buffer[bytes] = '\0';
        std::string line(buffer, static_cast<std::size_t>(bytes));
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        const auto fields = split(line, '|');
        if (fields.size() < 9 || fields[0] != "ME3" || fields[1] != "STATUS" || fields[2] != sessionId_)
            continue;

        const int mappedMask = static_cast<int>(std::strtol(fields[6].c_str(), nullptr, 10));
        const int armedMask = static_cast<int>(std::strtol(fields[7].c_str(), nullptr, 10));
        const auto names = split(fields[8], '~');

        const std::scoped_lock lock(statusMutex_);
        status_.bridgeSeen = true;
        status_.receivedHz = std::strtod(fields[3].c_str(), nullptr);
        status_.appliedHz = std::strtod(fields[4].c_str(), nullptr);
        status_.worstGapMs = std::strtod(fields[5].c_str(), nullptr);
        for (int slot = 0; slot < motion::kNumOutputs; ++slot)
        {
            auto& status = status_.slots[static_cast<std::size_t>(slot)];
            status.mapped = (mappedMask & (1 << slot)) != 0;
            status.armed = (armedMask & (1 << slot)) != 0;
            status.targetName = static_cast<std::size_t>(slot) < names.size() && !names[static_cast<std::size_t>(slot)].empty()
                ? names[static_cast<std::size_t>(slot)] : "None";
        }
    }
}
