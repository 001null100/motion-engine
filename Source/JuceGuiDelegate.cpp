#include "JuceGuiDelegate.hpp"
#include "PluginEditor.h"

#include <clap/ext/gui.h>

#include <algorithm>
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
        editor_->setSize(static_cast<int>(width_), static_cast<int>(height_));
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

bool JuceGuiDelegate::setScale(double) noexcept
{
    return false;
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
    width = width_;
    height = height_;
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
    width = std::clamp<std::uint32_t>(width, 1120, 1800);
    height = std::clamp<std::uint32_t>(height, 700, 1100);
    return true;
}

bool JuceGuiDelegate::setSize(std::uint32_t width, std::uint32_t height) noexcept
{
    if (!adjustSize(width, height))
        return false;
    width_ = width;
    height_ = height;
    if (editor_ != nullptr)
        editor_->setSize(static_cast<int>(width_), static_cast<int>(height_));
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
    editor_->setSize(static_cast<int>(width_), static_cast<int>(height_));
    return editor_->isOnDesktop();
#else
    (void)window;
    return false;
#endif
}
