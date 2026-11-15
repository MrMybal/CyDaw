// Fixed audio/parameters/transport, no application, device or random voices.
#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/EffectProcessors.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace cydaw::engine;
std::uint64_t offset{};
void capture(const EffectDescriptor &desc, const char *name, int preset, std::ostream &dump) {
    NativeDspNode node;
    node.kind = desc.kind;
    applyEffectDefaults(desc, node.parameters);
    if (preset && desc.typeKey == "chorus") {
        node.parameters.effect1=.6f; node.parameters.effect2=0; node.parameters.effect3=14;
        node.parameters.effect4=.8f; node.parameters.effect5=.35f;
    }
    if (preset && desc.typeKey == "ping-pong") {
        node.parameters.effect1=375; node.parameters.effect2=.45f; node.parameters.effect3=.32f;
        node.parameters.effect4=1; node.parameters.effect5=8;
    }
    if (preset && desc.typeKey == "tremolo-pan") {
        node.parameters.effect1=4; node.parameters.effect2=0; node.parameters.effect3=0;
        node.parameters.effect4=1; node.parameters.effect5=9;
    }
    auto fx=makeEffectProcessor(node);
    if (!fx) throw std::runtime_error("No processor");
    fx->prepare({48000,512,2});
    AudioBuffer in(2,512),out(2,512);
    std::vector<float> data;
    ProcessContext context;
    context.transport.beatsPerMinute=118;
    for (unsigned at=0;at<96000;at+=512) {
        const auto n=std::min(512u,96000-at);
        for (unsigned ch=0;ch<2;++ch)
            for (unsigned i=0;i<n;++i)
                in.view(n).channel(ch)[i]= at+i<48000 ? float(.12*std::sin((at+i)*(.021+ch*.003))+.03*std::cos((at+i)*.09)) : 0;
        context.transport.samplePosition=at;
        fx->glideValues(n,48000);
        fx->process(in.view(n),out.view(n),context);
        for(unsigned ch=0;ch<2;++ch) data.insert(data.end(),out.view(n).channel(ch),out.view(n).channel(ch)+n);
    }
    double peak=0,energy=0;
    for(float v:data) {
        if (!std::isfinite(v)) throw std::runtime_error("nonfinite audio");
        peak=std::max(peak,std::abs(double(v)));energy+=double(v)*v;
    }
    dump.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()*sizeof(float)));
    std::cout<<name<<','<<offset<<','<<data.size()<<','<<peak<<','<<energy<<'\n';offset+=data.size();
}
int main(int argc,char **argv) {
    if(argc!=3 || std::string(argv[1])!="--dump") return 2;
    std::ofstream dump(argv[2],std::ios::binary);
    if(!dump) return 3;
    std::cout<<"name,offset,samples,peak,energy\n";
    for(const auto &desc:effectCatalogue) {
        if(desc.midi||desc.control||desc.container) continue;
        capture(desc,std::string(desc.typeKey).c_str(),0,dump);
        if(desc.typeKey=="chorus"||desc.typeKey=="ping-pong"||desc.typeKey=="tremolo-pan")
            capture(desc,(std::string(desc.typeKey)+"-example-depth-zero").c_str(),1,dump);
    }
}
