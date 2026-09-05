#include "MotionEnginePlugin.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(false)
namespace {
struct Host {
    bool audio=false; unsigned callbacks=0, wakeups=0;
    clap_host_t api{}; clap_host_thread_check_t threads{}; clap_host_params_t params{};
    Host() {
        api={CLAP_VERSION,this,"Motion contracts","Null Exo","","1",
            [](const clap_host_t* h,const char* id)->const void* {
                auto& s=*static_cast<Host*>(h->host_data);
                if (!std::strcmp(id,CLAP_EXT_THREAD_CHECK)) return &s.threads;
                if (!std::strcmp(id,CLAP_EXT_PARAMS)) return &s.params;
                return nullptr;
            },[](const clap_host_t*){},
            [](const clap_host_t* h){++static_cast<Host*>(h->host_data)->wakeups;},
            [](const clap_host_t* h){++static_cast<Host*>(h->host_data)->callbacks;}};
        threads.is_main_thread=[](const clap_host_t* h){return !static_cast<Host*>(h->host_data)->audio;};
        threads.is_audio_thread=[](const clap_host_t* h){return static_cast<Host*>(h->host_data)->audio;};
        params.request_flush=[](const clap_host_t* h){CHECK(!static_cast<Host*>(h->host_data)->audio);};
        params.rescan=[](const clap_host_t* h,clap_param_rescan_flags){CHECK(!static_cast<Host*>(h->host_data)->audio);};
        params.clear=[](const clap_host_t*,clap_id,clap_param_clear_flags){};
    }
};
struct Input {
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t api{this,
        [](const clap_input_events_t* x){return static_cast<std::uint32_t>(static_cast<Input*>(x->ctx)->events.size());},
        [](const clap_input_events_t* x,std::uint32_t i){return static_cast<Input*>(x->ctx)->events[i];}};
};
struct Output {
    struct Record{std::uint16_t type;clap_id id;double value;};
    std::array<Record,2048> events{};size_t count=0,capacity=events.size();
    clap_output_events_t api{this,[](const clap_output_events_t* x,const clap_event_header_t* event){
        auto& s=*static_cast<Output*>(x->ctx);if(s.count>=s.capacity)return false;
        auto& e=s.events[s.count++];e.type=event->type;
        if(event->type==CLAP_EVENT_PARAM_VALUE){const auto& v=*reinterpret_cast<const clap_event_param_value_t*>(event);e.id=v.param_id;e.value=v.value;}
        else e.id=reinterpret_cast<const clap_event_param_gesture_t*>(event)->param_id;
        return true;
    }};
};
clap_event_midi_t midi(unsigned time=0){clap_event_midi_t e{};e.header={sizeof(e),time,0,CLAP_EVENT_MIDI,0};e.data[0]=0x9f;e.data[1]=60;e.data[2]=100;return e;}
clap_event_note_t note(unsigned time=0){clap_event_note_t e{};e.header={sizeof(e),time,0,CLAP_EVENT_NOTE_ON,0};e.port_index=0;e.channel=15;e.key=60;e.note_id=7;e.velocity=0;return e;}
clap_event_param_value_t param(clap_id id,double value,unsigned time=0){clap_event_param_value_t e{};e.header={sizeof(e),time,0,CLAP_EVENT_PARAM_VALUE,0};e.param_id=id;e.value=value;e.note_id=e.port_index=e.channel=e.key=-1;return e;}
struct Fixture {
    Host host; MotionEnginePlugin* plugin=new MotionEnginePlugin(&host.api);const clap_plugin_t* api=plugin->clapPlugin();
    std::array<float,2048> left{},right{};std::array<std::array<float,2048>,10> data{};
    float* inPointers[2]{left.data(),right.data()};std::array<float*,10> outPointers{};
    clap_audio_buffer_t in{};std::array<clap_audio_buffer_t,9> out{};double rate;
    explicit Fixture(double sr=48000):rate(sr){
        CHECK(api->init(api));for(size_t i=0;i<data.size();++i)outPointers[i]=data[i].data();
        in.data32=inPointers;in.channel_count=2;out[0].data32=outPointers.data();out[0].channel_count=2;
        for(size_t i=1;i<out.size();++i){out[i].data32=&outPointers[i+1];out[i].channel_count=1;}
        CHECK(api->activate(api,sr,1,2048));host.audio=true;CHECK(api->start_processing(api));host.audio=false;
    }
    ~Fixture(){host.audio=true;api->stop_processing(api);host.audio=false;api->deactivate(api);api->destroy(api);}
    void set(clap_id id,double value){CHECK(plugin->parameters().setBaseValue(id,value));}
    void block(unsigned n=256,std::initializer_list<const clap_event_header_t*> events={},Output* output=nullptr){
        Input input;input.events=events;Output unused;clap_process_t p{};
        p.frames_count=n;p.in_events=&input.api;p.out_events=output?&output->api:&unused.api;
        p.audio_inputs=&in;p.audio_inputs_count=1;p.audio_outputs=out.data();p.audio_outputs_count=9;
        host.audio=true;CHECK(api->process(api,&p)==CLAP_PROCESS_CONTINUE);host.audio=false;
    }
    void reset(){host.audio=true;api->reset(api);host.audio=false;}
};
std::vector<float> render(unsigned blockSize,double rate,bool redundantEvents=false){
    Fixture f(rate);f.set(motion::ids::model,6);f.set(motion::ids::audioKick,0.7);
    f.set(motion::ids::outputs[1].source,13);f.set(motion::ids::outputs[2].source,14);
    f.reset();std::vector<float> result;result.reserve(16000*8);
    for(unsigned start=0;start<16000;){
        const unsigned n=std::min(blockSize,16000-start);
        for(unsigned i=0;i<n;++i){const auto t=start+i;const float amplitude=(t%2200<400)?0.8f:0.02f;f.left[i]=amplitude*std::sin(t*0.14f);f.right[i]=amplitude*std::sin(t*0.117f);}
        auto unchanged=param(motion::ids::energy,1.0,n/2);
        f.block(n,redundantEvents?std::initializer_list<const clap_event_header_t*>{&unchanged.header}:std::initializer_list<const clap_event_header_t*>{});
        for(unsigned i=0;i<n;++i){CHECK(f.data[0][i]==f.left[i]);CHECK(f.data[1][i]==f.right[i]);for(int lane=2;lane<10;++lane){const float v=f.data[lane][i];CHECK(std::isfinite(v)&&v>=-1&&v<=1);result.push_back(v);}}
        start+=n;
    }
    return result;
}
void blockIndependence(){
    for(double rate:{44100.,48000.,96000.}){
        const auto reference=render(1,rate);
        for(unsigned size:{17u,64u,511u,2048u})for(bool events:{false,true}){
            const auto actual=render(size,rate,events);float error=0;
            for(size_t i=0;i<actual.size();++i)error=std::max(error,std::abs(actual[i]-reference[i]));
            if(error>1e-6)std::fprintf(stderr,"Block-size error %.9f at %.0f Hz, size %u, split %d\n",error,rate,size,events);
            CHECK(error<=1e-6);
        }
    }
}
void notesAndMasks(){
    Fixture a,b;const auto* ports=static_cast<const clap_plugin_note_ports_t*>(a.api->get_extension(a.api,CLAP_EXT_NOTE_PORTS));clap_note_port_info_t info{};
    CHECK(ports&&ports->count(a.api,true)==1&&ports->get(a.api,0,true,&info));
    CHECK((info.supported_dialects&(CLAP_NOTE_DIALECT_CLAP|CLAP_NOTE_DIALECT_MIDI))==(CLAP_NOTE_DIALECT_CLAP|CLAP_NOTE_DIALECT_MIDI));
    auto raw=midi(50);auto native=note(50);a.out[0].constant_mask=3;for(int i=1;i<9;++i)a.out[i].constant_mask=1;
    a.block(512,{&raw.header});b.block(512,{&native.header});
    for(int lane=2;lane<10;++lane)for(unsigned i=0;i<512;++i)CHECK(a.data[lane][i]==b.data[lane][i]);
    for(const auto& port:a.out)CHECK(port.constant_mask==0);
}
void gesturesAndState()
{
    Fixture f;
    const auto id=motion::ids::energy;
    f.plugin->beginUiEdit(id);
    for(int n=0;n<1000;++n)f.plugin->setUiValue(id,(n%100)*0.01);
    f.plugin->setUiValue(id,0.37);f.plugin->endUiEdit(id);
    int begins=0,ends=0;double last=-1;
    for(int n=0;n<600;++n){
        Output out;out.capacity=n<10?0:1;f.block(1,{},&out);f.api->on_main_thread(f.api);
        for(std::size_t j=0;j<out.count;++j){const auto& e=out.events[j];if(e.id!=id)continue;
            begins+=e.type==CLAP_EVENT_PARAM_GESTURE_BEGIN;ends+=e.type==CLAP_EVENT_PARAM_GESTURE_END;
            if(e.type==CLAP_EVENT_PARAM_VALUE)last=e.value;
        }
    }
    CHECK(begins==1&&ends==1&&last==0.37&&f.plugin->parameterValue(id)==0.37);
    struct Stream {
        std::vector<std::byte> bytes;std::size_t cursor=0;
        clap_ostream_t out{this,[](const clap_ostream_t* x,const void* p,std::uint64_t n)->std::int64_t{
            auto& s=*static_cast<Stream*>(x->ctx);const auto* b=static_cast<const std::byte*>(p);s.bytes.insert(s.bytes.end(),b,b+n);return static_cast<std::int64_t>(n);}};
        clap_istream_t in{this,[](const clap_istream_t* x,void* p,std::uint64_t n)->std::int64_t{
            auto& s=*static_cast<Stream*>(x->ctx);const auto count=std::min<std::size_t>(static_cast<std::size_t>(n),s.bytes.size()-s.cursor);
            if(count)std::memcpy(p,s.bytes.data()+s.cursor,count);s.cursor+=count;return static_cast<std::int64_t>(count);}};
    } good,bad;
    const auto* state=static_cast<const clap_plugin_state_t*>(f.api->get_extension(f.api,CLAP_EXT_STATE));
    CHECK(state&&state->save(f.api,&good.out));const std::array opaque{std::byte{0x7f}};
    CHECK(nullclap::state::save(f.plugin->parameters(),opaque,&bad.out));
    f.set(id,1.5);CHECK(!state->load(f.api,&bad.in));CHECK(f.plugin->parameterValue(id)==1.5);
    for(int n=0;n<20;++n)f.block();const auto before=f.plugin->motionCore().getSnapshot();
    CHECK(state->load(f.api,&good.in));CHECK(f.plugin->parameterValue(id)==0.37);
    const auto after=f.plugin->motionCore().getSnapshot();CHECK(before.x==after.x&&before.y==after.y);
    auto onset=note();f.block(512,{&onset.header});CHECK(f.plugin->motionCore().getSnapshot().impact>0.5);
    const auto wakes=f.host.wakeups;f.plugin->resetMotionFromUi();f.plugin->hitFromUi();f.block(512);
    CHECK(f.host.wakeups>wakes&&f.plugin->motionCore().getSnapshot().impact>0.5);
}
void quickDragAndNumericSafety()
{
    motion::MotionEngineCore core;motion::Parameters p;p.audioKick=0;core.setParameters(p);core.prepare(48000);
    core.beginDrag(0.7f,-0.4f);core.dragTo(0.61f,0.26f);core.endDrag(0,0);
    core.process(1.0/240.0,{});auto s=core.getSnapshot();CHECK(std::abs(s.x-0.61)<0.005&&std::abs(s.y-0.26)<0.005);
    p.timeScale=std::numeric_limits<double>::quiet_NaN();p.energy=std::numeric_limits<double>::infinity();p.motion.fill(p.timeScale);
    p.outputs[0].minimum=p.timeScale;p.zones[0].x=p.timeScale;core.setParameters(p);
    core.beginDrag(std::numeric_limits<float>::quiet_NaN(),0);core.endDrag(std::numeric_limits<float>::infinity(),0);
    core.process(std::numeric_limits<double>::infinity(),{});
    for(int i=0;i<10;++i)core.process(0.01,{p.energy,p.timeScale,p.energy,2});
    s=core.getSnapshot();CHECK(std::isfinite(s.x)&&std::isfinite(s.y));for(float v:s.outputs)CHECK(std::isfinite(v)&&v>=0&&v<=1);
    std::thread gui([&core]{for(int i=0;i<5000;++i){core.beginDrag(0.6f,-0.6f);core.dragTo(-0.3f,0.3f);core.endDrag(0.1f,0.1f);}});
    for(int i=0;i<5000;++i){core.process(1.0/240.0,{});auto v=core.getSnapshot();CHECK(std::isfinite(v.x)&&std::isfinite(v.y));}
    gui.join();core.beginDrag(-0.27f,0.43f);core.endDrag(0,0);core.process(1.0/240.0,{});
    s=core.getSnapshot();CHECK(std::abs(s.x+0.27)<0.01&&std::abs(s.y-0.43)<0.01);
}
void deterministicResetAndCausality()
{
    for(int model=0;model<10;++model){
        Fixture f;f.set(motion::ids::model,model);f.reset();std::vector<float> first;
        for(int n=0;n<12;++n){f.block(512);first.insert(first.end(),f.data[2].begin(),f.data[2].begin()+512);}
        f.reset();std::size_t index=0;for(int n=0;n<12;++n){f.block(512);for(int i=0;i<512;++i)CHECK(f.data[2][i]==first[index++]);}
    }
    Fixture hit,idle;auto event=midi(300);hit.block(1024,{&event.header});idle.block(1024);
    for(int lane=2;lane<10;++lane)for(int i=0;i<300;++i)CHECK(hit.data[lane][i]==idle.data[lane][i]);
    CHECK(hit.plugin->midiActivityText().find("hits 1")!=std::string::npos);
    auto zero=midi();zero.data[2]=0;hit.block(256,{&zero.header});CHECK(hit.plugin->midiActivityText().find("hits 1")!=std::string::npos);
#if defined(NDEBUG)
    auto bad=midi();bad.port_index=1;hit.block(256,{&bad.header});bad.port_index=0;bad.data[2]=255;hit.block(256,{&bad.header});
    auto badNative=note();badNative.velocity=std::numeric_limits<double>::quiet_NaN();hit.block(256,{&badNative.header});
    CHECK(hit.plugin->midiActivityText().find("hits 1")!=std::string::npos);
#endif
}
void doublePrecisionAndInPlace()
{
    Fixture f;std::array<double,1024> left{},right{},cv{};
    for(std::size_t i=0;i<left.size();++i){left[i]=std::sin(i*0.1);right[i]=std::cos(i*0.2);}
    const auto originalLeft=left,originalRight=right;
    double* input[]{left.data(),right.data()};double* control[]{cv.data()};
    f.in.data32=nullptr;f.in.data64=input;f.out[0].data32=nullptr;f.out[0].data64=input;
    f.out[1].data32=nullptr;f.out[1].data64=control;f.block(1024);
    CHECK(left==originalLeft&&right==originalRight);for(double value:cv)CHECK(std::isfinite(value)&&value>=-1&&value<=1);
    f.out[0].data64=nullptr;f.out[0].data32=f.outPointers.data();f.block(1024);
    for(std::size_t i=0;i<left.size();++i)CHECK(f.data[0][i]==static_cast<float>(left[i]));
    f.left[0]=std::numeric_limits<float>::quiet_NaN();f.right[0]=std::numeric_limits<float>::infinity();
    f.in.data32=f.inPointers;f.in.data64=nullptr;f.block(1024);
    for(double value:cv)CHECK(std::isfinite(value));
}

}
int main(){blockIndependence();notesAndMasks();gesturesAndState();quickDragAndNumericSafety();deterministicResetAndCausality();doublePrecisionAndInPlace();std::puts("Motion plugin contracts passed");}
