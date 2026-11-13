#include <cydaw/base/File.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <array>
int main(){int failures=0;
    auto compare=[&](const char* label,const cydaw::base::Text& ours,const juce::String& old){if(ours.toStdString()!=old.toStdString()){std::cerr<<label<<": ["<<ours.toStdString()<<"] != ["<<old.toStdString()<<"]\n";++failures;}};
    for(const char* s:{"a||b", "a b  c", "\"a b\" c", "", "a,b,", "un/deux/trois"}){
        auto ours=cydaw::base::TextList::fromTokens(s," ,|/","\"");auto old=juce::StringArray::fromTokens(s," ,|/","\"");compare("tokenization",ours.joinIntoString("~"),old.joinIntoString("~"));
    }
    for(const char* s:{"abc","été ÉTÉ","Français 🎬", "a\t\r\nb", "  alpha  "}){
        auto ours=cydaw::base::Text::fromUTF8(s);auto old=juce::String::fromUTF8(s);
        compare("trim",ours.trim(),old.trim());compare("lowercase",ours.toLowerCase(),old.toLowerCase());compare("uppercase",ours.toUpperCase(),old.toUpperCase());
        for(int i=0;i<=ours.length()+1;++i)compare("UTF-8 slice",ours.substring(i,i+2),old.substring(i,i+2));
    }
    for(double value:{-12.75,0.,1.23456789,123456.789})for(int n:{0,1,2,6})compare("decimal display",cydaw::base::Text(value,n),juce::String(value,n));
    for(const char* source:{"{\"id\":9223372036854775807,\"v\":[1,-1,0.25,true,null,\"é\"]}","{\"path\":\"C:\\\\test\\\\clip.wav\",\"name\":\"🎬\"}","[false,3.5,\"a\\nb\"]"}){
        auto doc=cydaw::base::JSON::parse(source);auto serialized=cydaw::base::JSON::toString(doc,true);auto native=juce::JSON::parse(juce::String::fromUTF8(serialized.toRawUTF8()));auto expected=juce::JSON::parse(juce::String::fromUTF8(source));
        if(juce::JSON::toString(native,true)!=juce::JSON::toString(expected,true)){std::cerr<<"Document semantics changed\nExpected: "<<juce::JSON::toString(expected,true).toStdString()<<"\nActual: "<<juce::JSON::toString(native,true).toStdString()<<"\nSerialized: "<<serialized.toStdString()<<"\n";++failures;}
    }
    if(!failures)std::cout<<"Text/document behavior preserved against the previous application runtime\n";return failures?1:0;
}
