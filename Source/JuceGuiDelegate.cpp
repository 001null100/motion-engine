#include "JuceGuiDelegate.hpp"
#include "PluginEditor.h"

#include <clap/ext/gui.h>

#include <cstring>

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
        editor_ = std::make_unique<MotionEngineEditor>(plugin_);
        applyLogicalEditorSize();
        editor_->setVisible(false);
        return true;
    }
    catch (...)
    {
        editor_.reset();
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
    juceInitialiser_.reset();
}

bool JuceGuiDelegate::setScale(double scale) noexcept
{
    // CLAP Win32 sizes are physical pixels while JUCE component bounds are logical.
    // Preserve the logical editor size and report/accept scaled physical sizes at
    // the host boundary so Windows DPI scaling is applied exactly once.
    if (!sizing_.setScale(scale))
        return false;
    applyLogicalEditorSize();
    return true;
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
#else
    (void)window;
    return false;
#endif
}

void JuceGuiDelegate::applyLogicalEditorSize() noexcept
{
    if (editor_ != nullptr)
        editor_->setSize(static_cast<int>(sizing_.logicalWidth()),
                         static_cast<int>(sizing_.logicalHeight()));
}
