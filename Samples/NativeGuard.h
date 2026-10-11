#pragma once
#include <windows.h>

namespace SampleNative {
struct Fault {
    DWORD code = 0;
    void* address = nullptr;
    ULONG_PTR accessKind = 0;
    ULONG_PTR accessAddress = 0;
};
using Callback = void (*)(void*);
// Catch only hardware faults on this calling thread. C++ exceptions propagate.
bool Invoke(Callback callback, void* context, Fault* fault);
bool IsHardwareFault(DWORD code);
}
