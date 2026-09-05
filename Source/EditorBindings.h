#pragma once
#include "MotionEnginePlugin.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <charconv>
#include <cmath>
#include <functional>
#include <memory>
#include <string_view>

namespace motion::ui
{
inline double parsePercent(std::string_view text, double current) noexcept
{
    const auto trim=[](std::string_view s){auto a=s.find_first_not_of(" \t\r\n");return a==s.npos?std::string_view{}:s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);};
    text=trim(text);if(!text.empty()&&text.back()=='%')text.remove_suffix(1);text=trim(text);
    if(!text.empty()&&text.front()=='+')text.remove_prefix(1);
    if(text.empty())return current;
    double value=0;auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!std::isfinite(value))return current;
    return std::clamp(value*0.01,0.0,1.0);
}
inline bool hasTextEditor(const juce::Component& component)
{
    for (int i=0;i<component.getNumChildComponents();++i)
    {
        const auto* child=component.getChildComponent(i);
        if (dynamic_cast<const juce::TextEditor*>(child)!=nullptr && child->isVisible()) return true;
        if (hasTextEditor(*child)) return true;
    }
    return false;
}
inline void syncSlider(juce::Slider& slider, double value)
{
    if (!slider.isMouseButtonDown(true) && !hasTextEditor(slider)) slider.setValue(value,juce::dontSendNotification);
}
inline void percent(juce::Slider& slider)
{
    slider.textFromValueFunction=[](double value){return juce::String(value*100.0,1)+"%";};
    slider.valueFromTextFunction=[&slider](const juce::String& text){return parsePercent(text.toStdString(),slider.getValue());};
}
inline void bind(MotionEnginePlugin& plugin, juce::Slider& slider,
                 std::function<clap_id()> parameter, const bool& syncing)
{
    const auto draggingId=std::make_shared<clap_id>(CLAP_INVALID_ID);
    slider.onDragStart=[&plugin,parameter,draggingId]{*draggingId=parameter();plugin.beginUiEdit(*draggingId);};
    slider.onValueChange=[&plugin,&slider,parameter,&syncing,draggingId]{
        if(syncing)return;
        if(*draggingId!=CLAP_INVALID_ID)plugin.setUiValue(*draggingId,slider.getValue());
        else plugin.setUiValueOnce(parameter(),slider.getValue());
    };
    slider.onDragEnd=[&plugin,draggingId]{
        if(*draggingId!=CLAP_INVALID_ID)plugin.endUiEdit(*draggingId);
        *draggingId=CLAP_INVALID_ID;
    };
    clap_param_info_t info{};
    for(std::uint32_t i=0;i<plugin.parameters().count();++i)
        if(plugin.parameters().info(i,info)&&info.id==parameter()){
            slider.setDoubleClickReturnValue(true,info.default_value);break;
        }
}
inline void finish(juce::Slider& slider, bool cancelText=false)
{
    slider.hideTextBox(cancelText);
    if(slider.onDragEnd)slider.onDragEnd();
}
inline void detach(juce::Slider& slider)
{
    finish(slider,true);
    slider.onValueChange={};slider.onDragStart={};slider.onDragEnd={};
}
}
