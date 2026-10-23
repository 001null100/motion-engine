#include "JuceGuiDelegate.hpp"
#include "PluginEditor.h"

#include <clap/ext/gui.h>

#include <cstdint>
#include <cstring>

#if JUCE_LINUX
// JUCE implements this in juce_Messaging_linux.cpp without exposing it in a public
// header. It is the same entry point JUCE's own plug-in wrappers use to pump the
// message queue and X11 events from a host-owned main thread.
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue(bool returnIfNoPendingMessages);
}
#endif

JuceGuiDelegate::JuceGuiDelegate(MotionEnginePlugin& plugin) noexcept
    : plugin_(plugin)
{
}

JuceGuiDelegate::~JuceGuiDelegate()
{
    destroy();
}

bool JuceGuiDelegate::isApiSupported(const char* api, bool floating) const noexcept
{
#if JUCE_WINDOWS
    return !floating && api != nullptr && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif JUCE_LINUX
    return !floating && api != nullptr && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#else
    (void)api;
    (void)floating;
    return false;
#endif
}

const char* JuceGuiDelegate::preferredApi() const noexcept
{
#if JUCE_WINDOWS
    return CLAP_WINDOW_API_WIN32;
#elif JUCE_LINUX
    return CLAP_WINDOW_API_X11;
#else
    return nullptr;
#endif
}

bool JuceGuiDelegate::create(const char* api, bool floating) noexcept
{
    if (!isApiSupported(api, floating) || editor_ != nullptr)
        return false;

    try
    {
        juceInitialiser_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
#if JUCE_LINUX
        // There is no JUCE message thread inside an X11 host. Without host timer
        // ticks the editor would never receive paint, input or juce::Timer messages.
        if (!plugin_.registerHostTimer(16, eventLoopTimer_))
        {
            juceInitialiser_.reset();
            return false;
        }
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            sizing_.setScale(display->scale);
#endif
        editor_ = std::make_unique<MotionEngineEditor>(plugin_);
        applyLogicalEditorSize();
        editor_->setVisible(false);
        return true;
    }
    catch (...)
    {
        editor_.reset();
        plugin_.unregisterHostTimer(eventLoopTimer_);
        eventLoopTimer_ = CLAP_INVALID_ID;
        juceInitialiser_.reset();
        return false;
    }
}

void JuceGuiDelegate::destroy() noexcept
{
    if (editor_ != nullptr)
    {
        editor_->setVisible(false);
        if (editor_->isOnDesktop())
            editor_->removeFromDesktop();
        editor_.reset();
    }
    if (eventLoopTimer_ != CLAP_INVALID_ID)
    {
        plugin_.unregisterHostTimer(eventLoopTimer_);
        eventLoopTimer_ = CLAP_INVALID_ID;
    }
    juceInitialiser_.reset();
}

bool JuceGuiDelegate::setScale(double scale) noexcept
{
#if JUCE_LINUX
    // JUCE's X11 peer applies its own display scale (Xft.dpi / GDK_SCALE). Ignore
    // the host's value so the logical/physical conversion matches the peer exactly;
    // CLAP lets X11 plug-ins query the scale themselves.
    (void)scale;
    return false;
#else
    // CLAP Win32 sizes are physical pixels while JUCE component bounds are logical.
    // Preserve the logical editor size and report/accept scaled physical sizes at
    // the host boundary so Windows DPI scaling is applied exactly once.
    if (!sizing_.setScale(scale))
        return false;
    applyLogicalEditorSize();
    return true;
#endif
}

bool JuceGuiDelegate::show() noexcept
{
    if (editor_ == nullptr)
        return false;
    editor_->setVisible(true);
    editor_->grabKeyboardFocus();
    return true;
}

bool JuceGuiDelegate::hide() noexcept
{
    if (editor_ == nullptr)
        return false;
    editor_->setVisible(false);
    return true;
}

bool JuceGuiDelegate::getSize(std::uint32_t& width, std::uint32_t& height) noexcept
{
    sizing_.getPhysicalSize(width, height);
    return editor_ != nullptr;
}

bool JuceGuiDelegate::getResizeHints(clap_gui_resize_hints_t& hints) noexcept
{
    hints = {};
    hints.can_resize_horizontally = true;
    hints.can_resize_vertically = true;
    hints.preserve_aspect_ratio = false;
    return true;
}

bool JuceGuiDelegate::adjustSize(std::uint32_t& width, std::uint32_t& height) noexcept
{
    sizing_.adjustPhysicalSize(width, height);
    return true;
}

bool JuceGuiDelegate::setSize(std::uint32_t width, std::uint32_t height) noexcept
{
    // set_size() is authoritative. Host-driven constraints are handled by
    // adjust_size(); silently clamping here would leave the host and child HWND
    // disagreeing about the accepted client size.
    if (width == 0 || height == 0)
        return false;
    sizing_.setPhysicalSize(width, height);
    applyLogicalEditorSize();
    return true;
}

bool JuceGuiDelegate::setParent(const clap_window_t& window) noexcept
{
#if JUCE_WINDOWS
    if (editor_ == nullptr || window.api == nullptr || window.win32 == nullptr
        || std::strcmp(window.api, CLAP_WINDOW_API_WIN32) != 0)
        return false;

    if (editor_->isOnDesktop())
        editor_->removeFromDesktop();

    editor_->addToDesktop(0, window.win32);
    applyLogicalEditorSize();
    return editor_->isOnDesktop();
#elif JUCE_LINUX
    if (editor_ == nullptr || window.api == nullptr || window.x11 == 0
        || std::strcmp(window.api, CLAP_WINDOW_API_X11) != 0)
        return false;

    if (editor_->isOnDesktop())
        editor_->removeFromDesktop();

    editor_->setTopLeftPosition(0, 0);
    editor_->addToDesktop(0, reinterpret_cast<void*>(static_cast<std::uintptr_t>(window.x11)));
    if (auto* peer = editor_->getPeer())
    {
        const auto previousWidth = sizing_.physicalWidth();
        const auto previousHeight = sizing_.physicalHeight();
        sizing_.setScale(peer->getPlatformScaleFactor());
        if (sizing_.physicalWidth() != previousWidth || sizing_.physicalHeight() != previousHeight)
            plugin_.requestGuiResize(sizing_.physicalWidth(), sizing_.physicalHeight());
    }
    applyLogicalEditorSize();
    return editor_->isOnDesktop();
#else
    (void)window;
    return false;
#endif
}

void JuceGuiDelegate::onTimer(clap_id timerId) noexcept
{
#if JUCE_LINUX
    if (timerId != eventLoopTimer_ || juceInitialiser_ == nullptr)
        return;
    while (juce::detail::dispatchNextMessageOnSystemQueue(true))
    {
    }
#else
    (void)timerId;
#endif
}

void JuceGuiDelegate::applyLogicalEditorSize() noexcept
{
    if (editor_ != nullptr)
        editor_->setSize(static_cast<int>(sizing_.logicalWidth()),
                         static_cast<int>(sizing_.logicalHeight()));
}
