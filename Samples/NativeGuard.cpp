#include "NativeGuard.h"
namespace SampleNative {
bool IsHardwareFault(DWORD code)
{
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_DATATYPE_MISALIGNMENT;
}
static int Capture(EXCEPTION_POINTERS* pointers, Fault* fault)
{
    const auto* record = pointers->ExceptionRecord;
    if (!IsHardwareFault(record->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    fault->code = record->ExceptionCode;
    fault->address = record->ExceptionAddress;
    if (record->NumberParameters >= 2) {
        fault->accessKind = record->ExceptionInformation[0];
        fault->accessAddress = record->ExceptionInformation[1];
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool Invoke(Callback callback, void* context, Fault* fault)
{
    __try { callback(context); return true; }
    __except (Capture(GetExceptionInformation(), fault)) { return false; }
}
}
