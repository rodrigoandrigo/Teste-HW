#include "../pch.h"
#include "SampleJournal.h"
#include <atomic>
#include <algorithm>
#include <sstream>

namespace {
SRWLOCK journalLock = SRWLOCK_INIT;
HANDLE journalFile = INVALID_HANDLE_VALUE;
PVOID exceptionObserver = nullptr;
bool observerAttempted = false;
using Observer = LONG (WINAPI*)(EXCEPTION_POINTERS*);
using RegisterObserver = PVOID (WINAPI*)(ULONG, Observer);
struct ObserverInstall { RegisterObserver function; Observer callback; PVOID result = nullptr; };
void InstallObserver(void* context)
{
    auto* install = static_cast<ObserverInstall*>(context);
    install->result = install->function(1, install->callback);
}
std::atomic<DWORD> journalError(ERROR_SUCCESS);
thread_local bool observingFault = false;
thread_local char lastOperation[128] = {};

void AppendLocked(const char* message, size_t size) noexcept
{
    if (journalFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    if (!WriteFile(journalFile, message, DWORD(size), &written, nullptr) || written != size)
        journalError = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers(journalFile)) journalError = GetLastError();
}
void FaultLine(const SampleNative::Fault& fault, const char* kind) noexcept
{
    // No heap allocation, SDK calls or blocking locks in the exception observer.
    if (observingFault || !TryAcquireSRWLockExclusive(&journalLock)) return;
    observingFault = true;
    MEMORY_BASIC_INFORMATION memory = {};
    VirtualQuery(fault.address, &memory, sizeof(memory));
    char line[512] = {};
    int count = sprintf_s(line,
        "%s SEH=0x%08lX Address=%p ModuleBase=%p ModuleOffset=0x%llX Thread=%lu AccessKind=%llu AccessAddress=0x%llX Operation=%s\r\n",
        kind, fault.code, fault.address, memory.AllocationBase,
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(fault.address) - reinterpret_cast<ULONG_PTR>(memory.AllocationBase)),
        GetCurrentThreadId(), static_cast<unsigned long long>(fault.accessKind), static_cast<unsigned long long>(fault.accessAddress), lastOperation);
    if (count > 0) AppendLocked(line, size_t(count));
    observingFault = false;
    ReleaseSRWLockExclusive(&journalLock);
}
LONG CALLBACK ObserveException(EXCEPTION_POINTERS* pointers)
{
    const auto* record = pointers->ExceptionRecord;
    if (SampleNative::IsHardwareFault(record->ExceptionCode)) {
        SampleNative::Fault fault;
        fault.code = record->ExceptionCode; fault.address = record->ExceptionAddress;
        if (record->NumberParameters >= 2) {
            fault.accessKind = record->ExceptionInformation[0]; fault.accessAddress = record->ExceptionInformation[1];
        }
        FaultLine(fault, "FIRST_CHANCE");
    }
    return EXCEPTION_CONTINUE_SEARCH; // Evidence only; never mask other threads' faults.
}
std::wstring Folder()
{
    return Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
}
}
namespace Teste_HW { namespace Samples {
std::wstring SampleJournal::Open(const wchar_t* key)
{
    std::wstring path = Folder() + L"\\XeSS_sample_" + key + L"_trace.txt";
    CREATEFILE2_EXTENDED_PARAMETERS options = {};
    options.dwSize = sizeof(options); options.dwFileFlags = FILE_FLAG_WRITE_THROUGH;
    HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS, &options);
    if (file == INVALID_HANDLE_VALUE) throw Platform::Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
    LARGE_INTEGER zero = {}, length = {};
    if (!GetFileSizeEx(file, &length) || !SetFilePointerEx(file, zero, nullptr, FILE_END)) {
        DWORD error = GetLastError(); CloseHandle(file);
        throw Platform::Exception::CreateException(HRESULT_FROM_WIN32(error));
    }
    AcquireSRWLockExclusive(&journalLock);
    if (journalFile != INVALID_HANDLE_VALUE) CloseHandle(journalFile);
    journalFile = file; journalError = ERROR_SUCCESS;
    if (!length.QuadPart) AppendLocked("\xEF\xBB\xBF", 3);
    ReleaseSRWLockExclusive(&journalLock);
    Write(L"RUN BEGIN " + std::wstring(key));
    if (Error()) throw Platform::Exception::CreateException(HRESULT_FROM_WIN32(Error()));
    if (!observerAttempted) {
        observerAttempted = true;
        // Optional export: the Store headers do not declare this desktop API.
        // Absence never bypasses the calling-thread SEH guard.
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"kernel32.dll", &module);
        auto install = module ? reinterpret_cast<RegisterObserver>(GetProcAddress(module, "AddVectoredExceptionHandler")) : nullptr;
        if (install) {
            Write(L"Optional first-chance observer registration BEGIN");
            ObserverInstall context = { install, ObserveException };
            SampleNative::Fault fault;
            if (SampleNative::Invoke(InstallObserver, &context, &fault)) exceptionObserver = context.result;
            else WriteFault(fault);
            Write(L"Optional first-chance observer registration END");
        }
    }
    if (!exceptionObserver) Write(L"WARNING: native first-chance observer unavailable; synchronous SEH guard remains active.");
    if (Error()) throw Platform::Exception::CreateException(HRESULT_FROM_WIN32(Error()));
    return path;
}
void SampleJournal::WriteUtf8(const char* message) noexcept
{
    if (!message) return;
    SYSTEMTIME time = {}; GetSystemTime(&time);
    char prefix[96] = {};
    int count = sprintf_s(prefix, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [thread %lu] ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());
    AcquireSRWLockExclusive(&journalLock);
    if (count > 0) AppendLocked(prefix, size_t(count));
    AppendLocked(message, strlen(message)); AppendLocked("\r\n", 2);
    ReleaseSRWLockExclusive(&journalLock);
}
void SampleJournal::Write(const std::wstring& message) noexcept
{
    try {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, message.data(), int(message.size()), nullptr, 0, nullptr, nullptr);
        std::string text(size_t(bytes), '\0');
        if (bytes) WideCharToMultiByte(CP_UTF8, 0, message.data(), int(message.size()), &text[0], bytes, nullptr, nullptr);
        WriteUtf8(text.c_str());
    } catch (...) { journalError = ERROR_OUTOFMEMORY; }
}
void SampleJournal::WriteFault(const SampleNative::Fault& fault) noexcept { FaultLine(fault, "CAUGHT"); }
void SampleJournal::SetOperation(const char* name) noexcept { strncpy_s(lastOperation, name ? name : "", _TRUNCATE); }
DWORD SampleJournal::Error() noexcept { return journalError; }
void SampleJournal::Close(const wchar_t* status) noexcept
{
    try { Write(L"RUN END " + std::wstring(status)); } catch (...) {}
    AcquireSRWLockExclusive(&journalLock);
    if (journalFile != INVALID_HANDLE_VALUE) { FlushFileBuffers(journalFile); CloseHandle(journalFile); journalFile = INVALID_HANDLE_VALUE; }
    ReleaseSRWLockExclusive(&journalLock);
}
std::vector<Diagnostics::DiagnosticResult> SampleJournal::Recover()
{
    std::vector<Diagnostics::DiagnosticResult> results;
    for (const wchar_t* key : { L"FG", L"LL", L"SR" }) {
        std::wstring path = Folder() + L"\\XeSS_sample_" + key + L"_trace.txt";
        HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size = {}; bool valid = GetFileSizeEx(file, &size) != FALSE;
        LARGE_INTEGER offset = {}; offset.QuadPart = (std::max)(0LL, size.QuadPart - 65536);
        if (valid) valid = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;
        std::string text(valid ? size_t(size.QuadPart - offset.QuadPart) : 0, '\0');
        DWORD read = 0;
        if (valid && !text.empty()) valid = ReadFile(file, &text[0], DWORD(text.size()), &read, nullptr) != FALSE;
        CloseHandle(file);
        if (!valid || !read) continue;
        text.resize(read);
        if (offset.QuadPart) { auto newline = text.find('\n'); if (newline != std::string::npos) text.erase(0, newline + 1); }
        else if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
        int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring tail(size_t(chars), L'\0');
        if (chars) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), &tail[0], chars);
        auto begin = text.rfind("RUN BEGIN"), end = text.rfind("RUN END");
        bool interrupted = end == std::string::npos || (begin != std::string::npos && end < begin);
        Diagnostics::DiagnosticResult result;
        result.category = L"Intel SDK / Persistent sample trace";
        result.name = std::wstring(key) + L" previous session";
        bool previousFailure = !interrupted && text.compare(end, 12, "RUN END FAIL") == 0;
        result.status = interrupted ? Diagnostics::DiagnosticStatus::Inconclusive :
            previousFailure ? Diagnostics::DiagnosticStatus::Fail : Diagnostics::DiagnosticStatus::Skipped;
        result.summary = interrupted ? L"Previous sample did not record RUN END; last durable checkpoints recovered." : L"Previous sample trace recovered (completed run).";
        result.details = L"File=" + path + L"; tail limited to 64 KiB. FIRST_CHANCE is evidence, not necessarily fatal.\r\n" + tail;
        result.timestampUtc = Diagnostics::DiagnosticReportSerializer::UtcNowIso8601();
        results.push_back(std::move(result));
    }
    return results;
}
} }
