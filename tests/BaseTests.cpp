#include <cydaw/base/Value.h>
#include <cydaw/document/NativeGraphState.h>
#include <cydaw/document/ProjectRecovery.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool pass,const char* text){if(!pass)throw std::runtime_error(text);}
int main(){try{
    Text words=Text::fromUTF8("Fran\xc3\xa7" "ais \xf0\x9f\x8e\xac");require(words.length()==10,"UTF-8 codepoints");require(words.substring(4,5)==Text::fromUTF8("\xc3\xa7"),"UTF-8 slicing");require(Text(words.toWideString().c_str())==words,"UTF-16/32 roundtrip");
    auto tokens=TextList::fromTokens("one \"two three\" four",true);require(tokens.size()==3&&tokens[1].unquoted()=="two three","quoted launch arguments");
    Value original;require(JSON::parse("{\"id\":9223372036854775807,\"name\":\"é🎬\",\"values\":[true,null,-1,0.125]}",original).wasOk(),"JSON fixture");require(int64(original.getProperty("id"))==INT64_MAX,"64-bit identity preserved");
    auto clone=original.clone();clone.getDynamicObject()->setProperty("name","changed");require(original.getProperty("name").toString()!=clone.getProperty("name").toString(),"deep document clone");
    auto serialized=JSON::toString(original,true);require(JSON::parse(serialized).getProperty("name").toString()==original.getProperty("name").toString(),"Unicode serialization");
    Value unchanged=original;require(JSON::parse("{broken",unchanged).failed()&&unchanged.getDynamicObject()==original.getDynamicObject(),"invalid JSON retains destination");
    cydaw::engine::NativeDspGraph graph;graph.outputNodeId=3;graph.nodes.push_back({1,cydaw::engine::NativeDspNodeKind::input,{}});graph.nodes.push_back({2,cydaw::engine::NativeDspNodeKind::gain,{}});graph.nodes.back().parameters.gainDb=-6;graph.nodes.push_back({3,cydaw::engine::NativeDspNodeKind::output,{}});graph.connections={{1,2,0,0},{2,3,0,0}};
    auto state=cydaw::document::serialiseNativeDspGraphState(graph);Text error;auto restored=cydaw::document::restoreNativeDspGraphState(JSON::parse(JSON::toString(state,true)),error);require(bool(restored)&&error.isEmpty()&&restored->nodes.size()==3&&restored->nodes[1].parameters.gainDb==-6,"actual DSP graph document roundtrip without JUCE");
    state.getDynamicObject()->setProperty("outputNode",99); // A malformed graph must be rejected independently of JSON validity.
    auto invalid=state.clone();invalid.getDynamicObject()->setProperty("nodes",Array<Value>{});require(!cydaw::document::restoreNativeDspGraphState(invalid,error),"invalid graph rejected");
    auto file=File::createTempFile(".cydaw-test");
    auto* projectObject=new Object;Value project(projectObject);projectObject->setProperty("format","CyDAW Project");projectObject->setProperty("version",36);projectObject->setProperty("name",words);projectObject->setProperty("id",int64(INT64_MAX));
    auto recovery=cydaw::document::ProjectRecovery::makeRecoveryDocument(project,file,Time(1234567890123));
    require(cydaw::document::ProjectRecovery::writeDocumentAtomically(file,recovery).wasOk(),"atomic project save without JUCE");
    Value loaded;require(cydaw::document::ProjectRecovery::readDocument(file,loaded).wasOk(),"actual project load without JUCE");require(loaded.getProperty("name").toString()==words&&int64(loaded.getProperty("id"))==INT64_MAX,"project text and identities preserved");
    require(cydaw::document::ProjectRecovery::recoveryTimestamp(loaded).toMilliseconds()==1234567890123,"recovery timestamp");
    MemoryBlock block("hello",5),decoded;require(decoded.fromBase64Encoding(block.toBase64Encoding())&&decoded==block,"binary state base64");require(!decoded.fromBase64Encoding("@@@=" )&&decoded==block,"invalid binary leaves prior state intact");
    require(file.deleteFile(),"remove private test project");
    std::cout<<"CyDaw original text/document/DSP state passed without JUCE\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
