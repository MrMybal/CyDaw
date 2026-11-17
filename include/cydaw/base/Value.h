#pragma once
#include "Text.h"
#include <memory>
#include <variant>
#include <span>
namespace cydaw::base {
template<class T> class Array:public std::vector<T>{
public:
    using std::vector<T>::vector;Array()=default;template<class A>requires(!std::is_same_v<std::remove_cvref_t<A>,Array>&&requires(const A& a){a.begin();a.end();})Array(const A& a){for(auto& item:a)this->push_back(T(item));}
    int size()const noexcept{return int(std::vector<T>::size());}bool isEmpty()const noexcept{return this->empty();}
    void add(T value){this->push_back(std::move(value));}template<class... U>void add(T value,U... rest){add(std::move(value));(add(std::move(rest)),...);}
    void addArray(const Array& values){std::vector<T>::insert(this->end(),values.begin(),values.end());}
    void addArray(const T* values,int count){if(count>0)std::vector<T>::insert(this->end(),values,values+count);}
    void addIfNotAlreadyThere(T value){if(!contains(value))add(std::move(value));}
    int indexOf(const T& value)const{auto i=std::find(this->begin(),this->end(),value);return i==this->end()?-1:int(i-this->begin());}bool contains(const T& value)const{return indexOf(value)>=0;}
    T& getReference(int i){return (*this)[std::size_t(i)];}const T& getReference(int i)const{return (*this)[std::size_t(i)];}
    T getFirst()const{return this->empty()?T{}:this->front();}T getLast()const{return this->empty()?T{}:this->back();}
    T getUnchecked(int i)const{return (*this)[std::size_t(i)];}T* getRawDataPointer(){return this->data();}const T* getRawDataPointer()const{return this->data();}
    void set(int i,T value){if(i<0)return;if(i>=size())this->resize(i+1);(*this)[std::size_t(i)]=std::move(value);}
    void insert(int i,T value){std::vector<T>::insert(this->begin()+std::clamp(i,0,size()),std::move(value));}
    void remove(int i){if(i>=0&&i<size())this->erase(this->begin()+i);}void removeRange(int first,int count){first=std::clamp(first,0,size());this->erase(this->begin()+first,this->begin()+std::min(size(),first+std::max(0,count)));}
    int removeAllInstancesOf(const T& value){return int(std::erase(*this,value));}
    void removeLast(int count=1){this->resize(std::size_t(std::max(0,size()-count)));}
    void sort(){std::sort(this->begin(),this->end());}
    void clearQuick(){this->clear();}void ensureStorageAllocated(int n){this->reserve(std::size_t(std::max(0,n)));}void minimiseStorageOverheads(){this->shrink_to_fit();}
    void swapWith(Array& other)noexcept{this->swap(other);}void move(int from,int to){if(from<0||from>=size())return;auto item=std::move((*this)[from]);remove(from);insert(to,std::move(item));}
};
class Object;
class Value {
    using Storage=std::variant<std::monostate,bool,int,int64,uint64,double,Text,std::shared_ptr<Array<Value>>,std::shared_ptr<Object>>;
    Storage value_;
public:
    Value()=default;Value(std::nullptr_t){}Value(bool b):value_(b){}Value(int v):value_(v){}Value(unsigned v):value_(uint64(v)){}
    Value(int64 v):value_(v){}Value(uint64 v):value_(v){}Value(double v):value_(v){}Value(float v):value_(double(v)){}
    Value(Text s):value_(std::move(s)){}Value(const char* s):value_(Text(s)){}Value(std::string s):value_(Text(std::move(s))){}
    Value(Array<Value> a):value_(std::make_shared<Array<Value>>(std::move(a))){}
    Value(Object*);
    template<class T> requires(requires(const T& t){t.joinIntoString("");t.begin();t.end();})Value(const T& strings){Array<Value> array;array.reserve(strings.size());for(auto& text:strings)array.add(Value(Text(text.toStdString())));value_=std::make_shared<Array<Value>>(std::move(array));}
    template<class T> requires(!std::is_same_v<std::remove_cvref_t<T>,Value> && requires(const T& t){t.isVoid();t.getDynamicObject();t.getArray();})
    Value(const T& source);
    template<class T> requires(!std::is_same_v<T,Value> && requires(const T& t){t.isVoid();t.getDynamicObject();t.getArray();})
    operator T()const;
    template<class T> requires(!std::is_same_v<std::remove_cvref_t<T>,Text> && requires(const T& t){t.toStdString();})
    Value(const T& text):value_(Text(text.toStdString())){}

    bool isVoid()const noexcept{return std::holds_alternative<std::monostate>(value_);}bool isUndefined()const noexcept{return false;}
    bool isBool()const noexcept{return std::holds_alternative<bool>(value_);}bool isInt()const noexcept{return std::holds_alternative<int>(value_);}bool isInt64()const noexcept{return std::holds_alternative<int64>(value_)||std::holds_alternative<uint64>(value_);}
    bool isDouble()const noexcept{return std::holds_alternative<double>(value_);}bool isString()const noexcept{return std::holds_alternative<Text>(value_);}bool isArray()const noexcept{return std::holds_alternative<std::shared_ptr<Array<Value>>>(value_);}bool isObject()const noexcept{return std::holds_alternative<std::shared_ptr<Object>>(value_);}
    Array<Value>* getArray()const noexcept{auto* a=std::get_if<std::shared_ptr<Array<Value>>>(&value_);return a?a->get():nullptr;}
    Object* getDynamicObject()const noexcept{auto* a=std::get_if<std::shared_ptr<Object>>(&value_);return a?a->get():nullptr;}
    Text toString()const;
    explicit operator bool()const;explicit operator int()const{return int(static_cast<int64>(*this));}explicit operator int64()const;explicit operator uint64()const;explicit operator double()const;explicit operator float()const{return float(static_cast<double>(*this));}
    Value getProperty(const Identifier&,const Value& fallback={})const;bool hasProperty(const Identifier&)const;
    const Value& operator[](const Identifier&)const;Value& operator[](int i);const Value& operator[](int i)const;
    int size()const noexcept{auto* a=getArray();return a?a->size():0;}void append(Value);void insert(int,Value);void remove(int);
    Value clone()const;friend bool operator==(const Value&,const Value&);
};
class Properties {
    struct Entry {Identifier name;Value value;};
    std::vector<Entry> values_;
public:
    auto begin(){return values_.begin();}auto end(){return values_.end();}auto begin()const{return values_.begin();}auto end()const{return values_.end();}
    int size()const noexcept{return int(values_.size());}bool isEmpty()const noexcept{return values_.empty();}
    int indexOf(const Identifier& name)const{for(int i=0;i<size();++i)if(values_[i].name==name)return i;return -1;}
    bool contains(const Identifier& name)const{return indexOf(name)>=0;}
    bool set(const Identifier& name,Value v){auto i=indexOf(name);if(i<0){values_.push_back({name,std::move(v)});return true;}if(values_[i].value==v)return false;values_[i].value=std::move(v);return true;}
    void remove(const Identifier& name){auto i=indexOf(name);if(i>=0)values_.erase(values_.begin()+i);}void clear(){values_.clear();}
    Identifier getName(int i)const{return i>=0&&i<size()?values_[i].name:Identifier{};}
    const Value& getValueAt(int i)const{static const Value empty;return i>=0&&i<size()?values_[i].value:empty;}
    Value* getVarPointerAt(int i){return i>=0&&i<size()?&values_[i].value:nullptr;}
    const Value& operator[](const Identifier& name)const{return getValueAt(indexOf(name));}
};
template<class T>class Ref {
    std::shared_ptr<T> ptr_;
public:
    Ref()=default;Ref(T* p){if(p){ptr_=p->weak_from_this().lock();if(!ptr_)ptr_.reset(p);}}
    T* get()const noexcept{return ptr_.get();}T* operator->()const noexcept{return ptr_.get();}operator T*()const noexcept{return ptr_.get();}explicit operator bool()const noexcept{return bool(ptr_);}
};
class Object:public std::enable_shared_from_this<Object>{
    Properties properties_;
public:
    using Ptr=Ref<Object>;
    Object()=default;Object(const Object& o):std::enable_shared_from_this<Object>(),properties_(o.properties_){}
    Object& operator=(const Object& o){properties_=o.properties_;return *this;}
    virtual ~Object()=default;
    std::unique_ptr<Object> clone()const{auto out=std::make_unique<Object>();for(int i=0;i<properties_.size();++i)out->setProperty(properties_.getName(i),properties_.getValueAt(i).clone());return out;}
    int getReferenceCount()const noexcept{return int(weak_from_this().use_count());}
    void setProperty(const Identifier& name,Value value){properties_.set(name,std::move(value));}bool hasProperty(const Identifier& name)const{return properties_.contains(name);}
    const Value& getProperty(const Identifier& name)const{return properties_[name];}void removeProperty(const Identifier& name){properties_.remove(name);}
    Properties& getProperties()noexcept{return properties_;}const Properties& getProperties()const noexcept{return properties_;}
};
inline Value::Value(Object* p){if(p){auto owner=p->weak_from_this().lock();if(!owner)owner.reset(p);value_=std::move(owner);}}
class InputStream;class OutputStream;class File;
// Document-adapter conversions keep old callers usable while their owning
// subsystem is migrated. They do not include or link an external framework.
template<class T> requires(!std::is_same_v<std::remove_cvref_t<T>,Value> && requires(const T& t){t.isVoid();t.getDynamicObject();t.getArray();})
Value::Value(const T& source){
    if(source.isVoid()||source.isUndefined())return;
    if(source.isBool()){*this=Value(bool(source));return;}if(source.isInt()){*this=Value(int(source));return;}if(source.isInt64()){*this=Value(int64(static_cast<long long>(source)));return;}if(source.isDouble()){*this=Value(double(source));return;}
    if(auto* a=source.getArray()){Array<Value> out;out.reserve(a->size());for(auto& v:*a)out.add(Value(v));*this=Value(std::move(out));return;}
    if(auto* o=source.getDynamicObject()){auto* copy=new Object;*this=Value(copy);auto& p=o->getProperties();for(int i=0;i<p.size();++i)copy->setProperty(Identifier(Text(p.getName(i).toString().toStdString())),Value(p.getValueAt(i)));return;}
    *this=Value(Text(source.toString().toStdString()));
}
template<class T> requires(!std::is_same_v<T,Value> && requires(const T& t){t.isVoid();t.getDynamicObject();t.getArray();})
Value::operator T()const {
    if(isVoid())return {};if(isBool())return T(bool(*this));if(isInt())return T(int(*this));if(isInt64())return T(static_cast<long long>(int64(*this)));if(isDouble())return T(double(*this));
    if(auto* a=getArray()){using ForeignArray=std::remove_pointer_t<decltype(T{}.getArray())>;ForeignArray out;out.ensureStorageAllocated(a->size());for(auto& v:*a)out.add(static_cast<T>(v));return T(out);}
    if(auto* o=getDynamicObject()){using ForeignObject=std::remove_pointer_t<decltype(T{}.getDynamicObject())>;auto* copy=new ForeignObject;T owner(copy);auto& p=o->getProperties();for(int i=0;i<p.size();++i)copy->setProperty(p.getName(i).toString().toRawUTF8(),static_cast<T>(p.getValueAt(i)));return owner;}
    return T(toString().toRawUTF8());
}
struct JSON {
    static Value parse(const Text&);
    static Value parse(const File&);
    template<class T> requires(requires(const T& t){t.toStdString();})static Value parse(const T& text){return parse(Text(text.toStdString()));}
    template<class T> requires(requires(T& t){t.readEntireStreamAsString();})static Value parse(T& stream){return parse(Text(stream.readEntireStreamAsString().toStdString()));}
    template<class T> requires(requires(const T& t){t.toStdString();})static Result parse(const T& text,Value& out){return parse(Text(text.toStdString()),out);}
    static Value parse(InputStream&);
    static Result writeToStream(OutputStream&,const Value&,bool oneLine=false);
    static Result parse(const Text&,Value&);
    static Text toString(const Value&,bool allOnOneLine=false,int maximumDecimalPlaces=15);
    static Text escapeString(const Text&);
};
}
