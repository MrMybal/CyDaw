#include <cydaw/base/Value.h>
#include <cydaw/base/File.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace cydaw::base {
Text Value::toString()const {
    if(auto* p=std::get_if<Text>(&value_))return *p;
    if(auto* p=std::get_if<bool>(&value_))return *p?"1":"0";
    if(auto* p=std::get_if<int>(&value_))return Text(*p);
    if(auto* p=std::get_if<int64>(&value_))return Text(*p);
    if(auto* p=std::get_if<uint64>(&value_))return Text(*p);
    if(auto* p=std::get_if<double>(&value_))return Text(*p);
    return isArray()?Text("[Array]"):isObject()?Text("[Object]"):Text{};
}
Value::operator bool()const{if(isBool())return std::get<bool>(value_);if(isString())return toString().getIntValue()!=0||toString().equalsIgnoreCase("true");if(isObject()||isArray())return true;return static_cast<double>(*this)!=0;}
Value::operator int64()const{if(auto* p=std::get_if<int64>(&value_))return *p;if(auto* p=std::get_if<uint64>(&value_))return int64(*p);if(auto* p=std::get_if<int>(&value_))return *p;if(auto* p=std::get_if<bool>(&value_))return *p;if(auto* p=std::get_if<double>(&value_)){if(!std::isfinite(*p)||*p<double(std::numeric_limits<int64>::min())||*p>=-double(std::numeric_limits<int64>::min()))return 0;return int64(*p);}return isString()?toString().getLargeIntValue():0;}
Value::operator uint64()const{if(auto* p=std::get_if<uint64>(&value_))return *p;return uint64(static_cast<int64>(*this));}
Value::operator double()const{if(auto* p=std::get_if<double>(&value_))return *p;if(auto* p=std::get_if<int64>(&value_))return double(*p);if(auto* p=std::get_if<uint64>(&value_))return double(*p);if(auto* p=std::get_if<int>(&value_))return *p;if(auto* p=std::get_if<bool>(&value_))return *p;return isString()?toString().getDoubleValue():0;}
Value Value::getProperty(const Identifier& name,const Value& fallback)const{auto* o=getDynamicObject();return o&&o->hasProperty(name)?o->getProperty(name):fallback;}
bool Value::hasProperty(const Identifier& name)const{auto* o=getDynamicObject();return o&&o->hasProperty(name);}
const Value& Value::operator[](const Identifier& name)const{static const Value empty;auto* o=getDynamicObject();return o?o->getProperty(name):empty;}
Value& Value::operator[](int i){auto* a=getArray();if(!a||i<0||i>=a->size())throw std::out_of_range("Invalid document array index");return (*a)[i];}
const Value& Value::operator[](int i)const{static const Value empty;auto* a=getArray();return a&&i>=0&&i<a->size()?(*a)[i]:empty;}
void Value::append(Value v){if(!isArray())value_=std::make_shared<Array<Value>>();getArray()->add(std::move(v));}
void Value::insert(int i,Value v){if(!isArray())value_=std::make_shared<Array<Value>>();getArray()->insert(i,std::move(v));}
void Value::remove(int i){if(auto* a=getArray())a->remove(i);}
Value Value::clone()const{if(auto* a=getArray()){Array<Value> b;b.reserve(a->size());for(const auto& v:*a)b.add(v.clone());return b;}if(auto* o=getDynamicObject()){auto* copy=new Object;Value owner(copy);auto& p=o->getProperties();for(int i=0;i<p.size();++i)copy->setProperty(p.getName(i),p.getValueAt(i).clone());return owner;}return *this;}
bool operator==(const Value& a,const Value& b){if(a.isObject()||b.isObject())return a.getDynamicObject()==b.getDynamicObject();if(auto* arr=a.getArray()){auto* other=b.getArray();return other&&*arr==*other;}if(b.isArray())return false;if(a.value_.index()==b.value_.index())return a.value_==b.value_;if(a.isVoid()||b.isVoid()||a.isString()||b.isString())return a.toString()==b.toString();return double(a)==double(b);}
namespace {
using Json=nlohmann::ordered_json;
Value read(const Json& j,unsigned depth){if(depth>256)throw std::runtime_error("Document nesting limit exceeded");if(j.is_null())return {};if(j.is_boolean())return j.get<bool>();if(j.is_number_unsigned()){auto n=j.get<uint64>();return n<=uint64(INT_MAX)?Value(int(n)):n<=uint64(INT64_MAX)?Value(int64(n)):Value(n);}if(j.is_number_integer()){auto n=j.get<int64>();return n>=INT_MIN&&n<=INT_MAX?Value(int(n)):Value(n);}if(j.is_number_float())return j.get<double>();if(j.is_string())return j.get<std::string>();if(j.is_array()){Array<Value> result;result.reserve(j.size());for(auto& v:j)result.add(read(v,depth+1));return result;}auto* object=new Object;Value owner(object);for(auto i=j.begin();i!=j.end();++i)object->setProperty(Identifier(Text(i.key())),read(i.value(),depth+1));return owner;}
Json write(const Value& v,unsigned depth){if(depth>256)throw std::runtime_error("Document nesting limit exceeded");if(v.isVoid())return nullptr;if(v.isBool())return bool(v);if(v.isInt())return int(v);if(v.isInt64()){auto t=v.toString();return t.startsWithChar('-')?Json(int64(v)):Json(uint64(v));}if(v.isDouble()){double d=double(v);if(!std::isfinite(d))throw std::runtime_error("Non-finite document number");return d;}if(v.isString())return v.toString().toStdString();if(auto* a=v.getArray()){auto j=Json::array();for(auto& x:*a)j.push_back(write(x,depth+1));return j;}if(auto* o=v.getDynamicObject()){auto j=Json::object();auto& p=o->getProperties();for(int i=0;i<p.size();++i)j[p.getName(i).toString().toStdString()]=write(p.getValueAt(i),depth+1);return j;}return nullptr;}
}
Result JSON::parse(const Text& text,Value& result){if(text.getNumBytesAsUTF8()>128u*1024u*1024u)return Result::fail("Document exceeds 128 MiB");try{auto j=Json::parse(text.toStdString());auto v=read(j,0);result=std::move(v);return Result::ok();}catch(const std::exception& e){return Result::fail(e.what());}}
Value JSON::parse(const Text& text){Value v;(void)parse(text,v);return v;}
Text JSON::toString(const Value& value,bool oneLine,int){try{return write(value,0).dump(oneLine?-1:2,' ',false,Json::error_handler_t::strict);}catch(...){return {};}}
Value JSON::parse(const File& file){auto input=file.createInputStream();return input?parse(*input):Value{};}
Value JSON::parse(InputStream& in){return parse(in.readEntireStreamAsString());}
Result JSON::writeToStream(OutputStream& out,const Value& value,bool oneLine){
    try {
        auto text=[&](const Text& t){if(!out.write(t.toRawUTF8(),t.getNumBytesAsUTF8()))throw std::runtime_error("Document write failed");};
        auto line=[&](unsigned depth){if(!oneLine){text("\n");text(Text::repeatedString("  ",int(depth)));}};
        auto emit=[&](auto&& self,const Value& v,unsigned depth)->void{
            if(depth>256)throw std::runtime_error("Document nesting limit exceeded");
            if(auto* a=v.getArray()){text("[");for(int i=0;i<a->size();++i){if(i)text(",");line(depth+1);self(self,(*a)[i],depth+1);}if(!a->isEmpty())line(depth);text("]");return;}
            if(auto* o=v.getDynamicObject()){text("{");auto& p=o->getProperties();for(int i=0;i<p.size();++i){if(i)text(",");line(depth+1);text(Json(p.getName(i).toString().toStdString()).dump());text(oneLine?":":": ");self(self,p.getValueAt(i),depth+1);}if(p.size())line(depth);text("}");return;}
            text(write(v,depth).dump());
        };
        emit(emit,value,0);return Result::ok();
    }catch(const std::exception& e){return Result::fail(e.what());}
}
Text JSON::escapeString(const Text& text){auto encoded=Json(text.toStdString()).dump();return encoded.substr(1,encoded.size()-2);}
}
