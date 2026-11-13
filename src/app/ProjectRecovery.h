#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once
#include <cydaw/document/ProjectRecovery.h>

namespace cydaw::app {
class ProjectRecovery {
    static base::File nativeFile(const cydaw::base::File& f){return base::File(base::Text(f.getFullPathName().toStdString()));}
    static cydaw::base::File legacyFile(const base::File& f){return cydaw::base::File(cydaw::base::Text::fromUTF8(f.getFullPathName().toRawUTF8()));}
    static cydaw::base::Result result(const base::Result& r){return r.wasOk()?cydaw::base::Result::ok():cydaw::base::Result::fail(cydaw::base::Text::fromUTF8(r.getErrorMessage().toRawUTF8()));}
public:
    static constexpr int schemaVersion=document::ProjectRecovery::schemaVersion;
    static constexpr int maximumProjectVersion=document::ProjectRecovery::maximumProjectVersion;
    static cydaw::base::File defaultRecoveryFile(){return legacyFile(document::ProjectRecovery::defaultRecoveryFile());}
    static base::Value makeRecoveryDocument(base::Value v,const cydaw::base::File& f,cydaw::base::Time t=cydaw::base::Time::getCurrentTime()){return document::ProjectRecovery::makeRecoveryDocument(v,nativeFile(f),base::Time(t.toMilliseconds()));}
    static bool isRecoveryDocument(const base::Value& v){return document::ProjectRecovery::isRecoveryDocument(v);}
    static cydaw::base::File originalProject(const base::Value& v){return legacyFile(document::ProjectRecovery::originalProject(v));}
    static cydaw::base::Time recoveryTimestamp(const base::Value& v){return cydaw::base::Time(document::ProjectRecovery::recoveryTimestamp(v).toMilliseconds());}
    static cydaw::base::Result validateProjectDocument(const base::Value& v){return result(document::ProjectRecovery::validateProjectDocument(v));}
    static cydaw::base::Result writeDocumentAtomically(const cydaw::base::File& f,const base::Value& v,bool verify=true){return result(document::ProjectRecovery::writeDocumentAtomically(nativeFile(f),v,verify));}
    static cydaw::base::Result readDocument(const cydaw::base::File& f,base::Value& v){base::Value data;auto r=document::ProjectRecovery::readDocument(nativeFile(f),data);v=data;return result(r);}
    static bool discard(const cydaw::base::File& f){return document::ProjectRecovery::discard(nativeFile(f));}
};
}
