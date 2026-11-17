#include <cydaw/control/DirectMidiBridge.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <atomic>
#include <condition_variable>
using namespace cydaw::control;
namespace {
int failures=0;
void check(bool yes,const char* what){std::cout<<(yes?"ok ":"FAIL ")<<what<<'\n';if(!yes)++failures;}
VirtualControl control(std::string id,std::string address,ControlKind kind=ControlKind::knob) {
    VirtualControl c;c.id=id;c.sourceDevice="fixture";c.sourceAddress=address;c.kind=kind;return c;
}
Router router(std::vector<VirtualControl> controls) {
    Router r;VirtualDevice d;d.id="box";d.controls=std::move(controls);r.devices.push_back(std::move(d));return r;
}
void assign(Router& r,const char* id,const char* target,double low=0,double high=1){r.assignments.push_back({"box",id,target,low,high});}
}
int main() {
    std::vector<std::vector<std::uint8_t>> bytes;
    DirectMidiBridge bridge([&](std::span<const std::uint8_t> data){bytes.emplace_back(data.begin(),data.end());});
    auto c=control("stick","axis/leftX",ControlKind::axis);
    c.chain={defaultModifier(ModifierKind::deadzone),defaultModifier(ModifierKind::drawnCurve)};
    c.chain[0].a=.1;
    auto r=router({c});assign(r,"stick","midi/live/cc/2/74",.2,.8);assign(r,"stick","midi/live/pb/3");
    assign(r,"stick","transport/play");
    bridge.configure(r);bridge.receive("fixture",{"axis/leftX",.51});
    check(bytes==std::vector<std::vector<std::uint8_t>>{{0xb1,74,64},{0xe2,0,64}},"deadzone centres joystick; one macro emits CC and pitch bend only");
    bridge.receive("fixture",{"axis/leftX",.52});check(bytes.size()==2,"identical quantized MIDI values are coalesced");
    bridge.receive("fixture",{"axis/leftX",1});check(bytes[2]==std::vector<std::uint8_t>{0xb1,74,102},"destination range maps maximum to 80 percent");
    check(bridge.claims("fixture","axis/leftX"),"mapped address is reserved");
    auto curve=defaultModifier(ModifierKind::drawnCurve);
    for(std::size_t i=0;i<curve.points.size();++i)curve.points[i]=std::pow(double(i)/16,2);
    c.chain={curve};r=router({c});assign(r,"stick","midi/live/cc/1/1");
    bridge.configure(r);bytes.clear();bridge.receive("fixture",{"axis/leftX",.5});
    check(bytes.size()==1 && bytes[0][2]==32,"drawn curve interpolates the saved points before MIDI conversion");
    check(std::abs(feedbackValue(r.devices[0].controls[0],.25)-.5)<1e-8,"drawn curve has a usable inverse for feedback");
    bridge.receive("fixture",{"axis/leftX",std::numeric_limits<double>::quiet_NaN()});check(bytes.size()==1,"invalid input cannot generate MIDI");
    auto a=control("a","button/a",ControlKind::button),b=control("b","button/b",ControlKind::button);
    r=router({a,b});assign(r,"a","midi/live/note/1/60",.2,.8);assign(r,"b","midi/live/note/1/60");
    bridge.configure(r);bytes.clear();
    bridge.receive("fixture",{"button/a",1});bridge.receive("fixture",{"button/a",1});bridge.receive("fixture",{"button/b",1});
    check(bytes.size()==1 && bytes[0]==std::vector<std::uint8_t>{0x90,60,102},"held/repeated keys do not retrigger; shared notes have owners");
    bridge.receive("fixture",{"button/a",0});check(bytes.size()==1,"nonzero range still releases first owner without stopping second");
    bridge.receive("fixture",{"button/b",0});check(bytes.size()==2 && bytes.back()==std::vector<std::uint8_t>{0x80,60,0},"last owner sends an explicit note-off");
    bridge.receive("fixture",{"button/a",.1});check(bytes.back()[0]==0x90,"soft momentary velocity still plays a note");
    bridge.releaseSource("fixture");check(bytes.back()[0]==0x80,"unplug/focus loss releases owned notes");
    bridge.receive("fixture",{"button/a",1});bridge.configure(r,{"fixture"});
    check(bytes.back()[0]==0x80 && !bridge.claims("fixture","button/a"),"remapping/disable releases notes and claims");
    auto n=bytes.size();bridge.receive("fixture",{"button/a",1});check(bytes.size()==n,"disabled input cannot play");
    bridge.configure(r);bridge.receive("fixture",{"button/a",1});bridge.panic();check(bytes.back()[0]==0x80,"panic releases notes");
    r.assignments.clear();assign(r,"a","transport/play");bridge.configure(r);
    check(bridge.claims("fixture","button/a"),"DAW-only key mappings also reserve the piano key");
    check(!DirectMidiBridge::isTarget("midi/live/cc/17/2") && !DirectMidiBridge::isTarget("midi/live/note/1/128"),"invalid MIDI destinations are rejected");
    // A live provider thread needs no UI pump. The main test thread is deliberately waiting.
    r=router({control("axis","axis/x")});assign(r,"axis","midi/live/cc/1/11");bridge.configure(r);
    std::mutex m;std::condition_variable cv;bool emitted=false;
    DirectMidiBridge threaded([&](auto){std::lock_guard lock(m);emitted=true;cv.notify_one();});threaded.configure(r);
    std::thread provider([&]{threaded.receive("fixture",{"axis/x",.7});});
    {std::unique_lock lock(m);check(cv.wait_for(lock,std::chrono::seconds(1),[&]{return emitted;}),"provider emits while UI/test thread is blocked");}
    provider.join();
    std::cout<<failures<<" failure(s)\n";return failures?1:0;
}
