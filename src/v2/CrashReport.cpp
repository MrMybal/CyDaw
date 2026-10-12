// When CyDAW 2 crashes, the stack — with function names and lines read from
// the PDB next to the executable — is written to %TEMP%\cydaw2-crash.txt.

#include "CrashReport.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment(lib, "dbghelp.lib")
#endif

#include <cstdio>

namespace cydaw::v2 {

#if JUCE_WINDOWS
namespace {

LONG WINAPI writeCrashReport(EXCEPTION_POINTERS* exception)
{
    char path[MAX_PATH] {};
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "cydaw2-crash.txt");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "w") != 0 || file == nullptr)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(file, "CyDAW 2 crash: exception 0x%08lx at %p\n", exception->ExceptionRecord->ExceptionCode,
            exception->ExceptionRecord->ExceptionAddress);
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && exception->ExceptionRecord->NumberParameters >= 2)
        fprintf(file, "access violation %s address %p\n",
                exception->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]));

    auto process = GetCurrentProcess();
    auto thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (auto depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0)
            break;
        char buffer[sizeof(SYMBOL_INFO) + 512] {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        const auto named = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
        const auto lined = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line);
        fprintf(file, "#%02d %s", depth, named ? symbol->Name : "?");
        if (lined)
            fprintf(file, "  (%s:%lu)", line.FileName, line.LineNumber);
        fprintf(file, "\n");
    }
    SymCleanup(process);
    fclose(file);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashReport()
{
    SetUnhandledExceptionFilter(writeCrashReport);
}
#else
void installCrashReport() {}
#endif

} // namespace cydaw::v2
