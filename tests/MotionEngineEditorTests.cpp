#include "PluginEditor.h"
#include "EditorBindings.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(false)
namespace {
template<class T> T* find(juce::Component& parent,const juce::String& title={})
{
    if(auto* result=dynamic_cast<T*>(&parent);result!=nullptr&&(title.isEmpty()||parent.getTitle()==title))return result;
    for(int i=0;i<parent.getNumChildComponents();++i)if(auto* result=find<T>(*parent.getChildComponent(i),title))return result;
    return nullptr;
}
void checkControls(juce::Component& parent)
{
    for(int i=0;i<parent.getNumChildComponents();++i){
        auto* c=parent.getChildComponent(i);if(!c->isVisible()||dynamic_cast<juce::TooltipWindow*>(c))continue;
        CHECK(!c->getBounds().isEmpty());
        if(!parent.getLocalBounds().contains(c->getBounds()))std::fprintf(stderr,"Clipped %s (%d,%d %dx%d), parent %dx%d\n",c->getTitle().toRawUTF8(),c->getX(),c->getY(),c->getWidth(),c->getHeight(),parent.getWidth(),parent.getHeight());
        CHECK(parent.getLocalBounds().contains(c->getBounds()));
        if(dynamic_cast<OutputStrip*>(c)!=nullptr)checkControls(*c);
    }
}
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    clap_host_t host{CLAP_VERSION,nullptr,"Editor tests","Null Exo","","1",
        [](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
    auto* plugin=new MotionEnginePlugin(&host);const auto* api=plugin->clapPlugin();CHECK(api->init(api));
    {
        MotionEngineEditor editor(*plugin);editor.addToDesktop(0);editor.setVisible(true);editor.grabKeyboardFocus();
        for(const auto size:{juce::Point<int>(1120,700),juce::Point<int>(1320,820),juce::Point<int>(1700,1000)}){
            editor.setSize(size.x,size.y);editor.refreshFromPlugin();checkControls(editor);
            auto image=editor.createComponentSnapshot(editor.getLocalBounds());CHECK(image.isValid());
            auto output=juce::File::getCurrentWorkingDirectory().getChildFile("MotionEngine-"+juce::String(size.x)+"x"+juce::String(size.y)+".png").createOutputStream();
            CHECK(output&&output->openedOk());CHECK(juce::PNGImageFormat().writeImageToStream(image,*output));
        }
        auto* minimum=find<juce::Slider>(editor,"Output 1 minimum");CHECK(minimum);
        CHECK(minimum->getValueFromText("50%")==0.5);CHECK(minimum->getValueFromText("25")==0.25);
        minimum->showTextBox();auto* text=find<juce::TextEditor>(*minimum);CHECK(text);
        text->setText("37.5%",false);editor.refreshFromPlugin();CHECK(text->getText()=="37.5%");
        minimum->hideTextBox(false);CHECK(plugin->parameterValue(motion::ids::outputs[0].minimum)==0.375);
        auto* radius=find<juce::Slider>(editor,"Selected zone radius");auto* zone=find<StableComboBox>(editor,"Selected zone");CHECK(radius&&zone);
        radius->showTextBox();text=find<juce::TextEditor>(*radius);CHECK(text);text->setText("0.66",false);
        zone->setSelectedItemIndex(1,juce::sendNotificationSync);
        CHECK(plugin->parameterValue(motion::ids::zones[0].radius)==0.66);
        CHECK(plugin->parameterValue(motion::ids::zones[1].radius)==0.38);
        auto* model=find<StableComboBox>(editor,"Motion model");CHECK(model);
        for(int m=0;m<10;++m){model->setSelectedItemIndex(m,juce::sendNotificationSync);editor.refreshFromPlugin();CHECK(plugin->parameterInt(motion::ids::model)==m);}
        // Closing the editor releases its canvas ownership without a mouse-up event.
        auto* canvas=find<MotionCanvas>(editor);CHECK(canvas);
        motion::Parameters parameters;
        parameters.model=1;parameters.audioKick=0;
        auto& core=plugin->motionCore();core.setParameters(parameters);core.prepare(48000);
        const auto initial=core.getSnapshot();
        auto available=canvas->getLocalBounds().toFloat().reduced(14,14);available.removeFromBottom(24);
        const float side=std::min(available.getWidth(),available.getHeight());
        const juce::Point<float> body(available.getCentreX()+initial.x*side*0.5f,
                                      available.getCentreY()-initial.y*side*0.5f);
        const auto now=juce::Time::getCurrentTime();
        const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(),body,
            juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,canvas,canvas,now,body,now,1,false);
        canvas->mouseDown(down);
        for(int i=0;i<20;++i)core.process(1.0/240.0,{});
        CHECK(std::hypot(core.getSnapshot().x-initial.x,core.getSnapshot().y-initial.y)<1e-5f);
        editor.setVisible(false); // No mouseUp: the editor must cancel its owned drag.
        for(int i=0;i<100;++i)core.process(1.0/240.0,{});
        CHECK(std::hypot(core.getSnapshot().x-initial.x,core.getSnapshot().y-initial.y)>0.02f);
        editor.removeFromDesktop();
    }
    api->destroy(api);std::puts("Motion editor layout, percentage editing, zone ownership and model selection passed");
}
