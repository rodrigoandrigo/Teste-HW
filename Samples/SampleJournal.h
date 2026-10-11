#pragma once
#include "../Diagnostics/DiagnosticReport.h"
#include "NativeGuard.h"

namespace Teste_HW { namespace Samples {
class SampleJournal {
public:
    // Called on the UI thread before CoreWindow interop or SDK work.
    static std::wstring Open(const wchar_t* key);
    static void Write(const std::wstring& message) noexcept;
    static void WriteUtf8(const char* message) noexcept;
    static void WriteFault(const SampleNative::Fault& fault) noexcept;
    static void SetOperation(const char* name) noexcept;
    static void Close(const wchar_t* status) noexcept;
    static DWORD Error() noexcept;
    static std::vector<Diagnostics::DiagnosticResult> Recover();
};
} }
