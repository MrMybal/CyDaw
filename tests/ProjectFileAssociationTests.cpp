#include "ProjectFileAssociation.h"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace shell = cydaw::v2::shell;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct TestRoot {
    std::wstring name = L"Software\\CyDaw\\Tests\\FileAssociation-" + std::to_wstring(GetCurrentProcessId())
                     + L"-" + std::to_wstring(GetTickCount64());
    HKEY key {};
    TestRoot() { require(RegCreateKeyExW(HKEY_CURRENT_USER,name.c_str(),0,nullptr,0,KEY_ALL_ACCESS,
                                       nullptr,&key,nullptr)==ERROR_SUCCESS,"create isolated Classes root"); }
    ~TestRoot() { if(key)RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,name.c_str()); }
};
std::wstring read(HKEY root, const wchar_t* path) {
    wchar_t text[32768];DWORD bytes=sizeof(text);
    require(RegGetValueW(root,path,nullptr,RRF_RT_REG_SZ,nullptr,text,&bytes)==ERROR_SUCCESS,"read association text");
    return text;
}
int wmain(int argc,wchar_t** argv) {
    try {
        TestRoot root;
        const std::wstring first=LR"(D:\CyDaw tests\Édition\CyDAW 2.exe)";
        auto r=shell::registerProjectFilesAt(root.key,first);
        require(r && r.changed && r.defaultAssigned,"register missing project association");
        require(read(root.key,L".cydaw")==shell::projectProgId,"set missing default");
        require(read(root.key,L"CyDaw.Project\\DefaultIcon")==L"\""+first+L"\",-102","quote Unicode icon path and resource ID");
        require(read(root.key,L"CyDaw.Project\\shell\\open\\command")==L"\""+first+L"\" \"%1\"","quote executable and spaced project argument");
        DWORD kind{},size{};
        require(RegGetValueW(root.key,L".cydaw\\OpenWithProgids",shell::projectProgId,RRF_RT_ANY,&kind,nullptr,&size)==ERROR_SUCCESS
                && kind==REG_NONE && size==0,"Open with offer exists");
        r=shell::registerProjectFilesAt(root.key,first);
        require(r && !r.changed && r.defaultAssigned,"idempotent launch does not churn registry");
        const std::wstring moved=LR"(E:\Portable apps\CyDaw\CyDAW 2.exe)";
        r=shell::registerProjectFilesAt(root.key,moved);
        require(r && r.changed && read(root.key,L"CyDaw.Project\\DefaultIcon")==L"\""+moved+L"\",-102","portable move updates icon location");
        HKEY extension{};
        require(RegOpenKeyExW(root.key,L".cydaw",0,KEY_SET_VALUE,&extension)==ERROR_SUCCESS,"open test extension");
        const wchar_t other[]=L"Other.Application";
        const auto changed=RegSetValueExW(extension,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(other),sizeof(other));
        RegCloseKey(extension);require(changed==ERROR_SUCCESS,"set foreign handler");
        r=shell::registerProjectFilesAt(root.key,first);
        require(r && !r.defaultAssigned && read(root.key,L".cydaw")==other,"preserve another application's default");
        require(shell::registerProjectFilesAt(root.key,L"relative.exe").error==ERROR_INVALID_PARAMETER,"reject relative command");
        require(shell::registerProjectFilesAt(root.key,L"D:\\bad\"path.exe").error==ERROR_INVALID_PARAMETER,"reject command quoting injection");
        const auto denied=shell::registerProjectFilesAt(nullptr,moved);
        require(!denied && denied.error==ERROR_INVALID_HANDLE,"report inaccessible registry roots");
        RegDeleteTreeW(root.key,L".cydaw");
        const auto inherited=shell::registerProjectFilesAt(root.key,first,L"Other.SystemHandler");
        require(inherited && !inherited.defaultAssigned,"preserve an inherited machine-level handler");
        DWORD missingSize{};
        require(RegGetValueW(root.key,L".cydaw",nullptr,RRF_RT_REG_SZ,nullptr,nullptr,&missingSize)==ERROR_FILE_NOT_FOUND,
                "do not shadow the machine default with a per-user value");
        if(argc>1) {
            auto resource=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            require(resource!=nullptr,"load application resources");
            const bool hasApp=FindResourceW(resource,MAKEINTRESOURCEW(101),RT_GROUP_ICON)!=nullptr;
            const bool hasProject=FindResourceW(resource,MAKEINTRESOURCEW(shell::projectIconResource),RT_GROUP_ICON)!=nullptr;
            FreeLibrary(resource);require(hasApp&&hasProject,"application and project icon groups are embedded");
            HICON large{},smallIcon{};
            const auto count=ExtractIconExW(argv[1],-shell::projectIconResource,&large,&smallIcon,1);
            const bool extracted=count==2&&large&&smallIcon;
            if(large)DestroyIcon(large);if(smallIcon)DestroyIcon(smallIcon);
            require(extracted,"Windows shell extracts large and small project icons");
        }
        std::cout<<"Project icon resources, quoted Unicode commands, portable moves and preserved file defaults passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
