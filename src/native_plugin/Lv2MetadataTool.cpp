#include <windows.h>
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
        return 1;
    auto module = LoadLibraryW(argv[1]);
    if (!module)
        return 2;
    auto write = reinterpret_cast<int (*)(const char *)>(GetProcAddress(module, "cydaw_write_lv2_metadata"));
    if (!write)
    {
        FreeLibrary(module);
        return 3;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, nullptr, 0, nullptr, nullptr);
    auto text = new char[bytes];
    WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, text, bytes, nullptr, nullptr);
    int result = write(text);
    delete[] text;
    FreeLibrary(module);
    return result;
}
