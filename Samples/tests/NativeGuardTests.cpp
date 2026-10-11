#include "../NativeGuard.h"
#include <stdexcept>
#include <cstdio>

static void NoFault(void* value) { ++*static_cast<int*>(value); }
static void AccessViolation(void*) {
    ULONG_PTR parameters[] = { 1, 0x12345678 };
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, parameters);
}
static void IllegalInstruction(void*) { RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, 0, 0, nullptr); }
static void ActualAccessViolation(void*) { *reinterpret_cast<volatile int*>(static_cast<ULONG_PTR>(1)) = 42; }
static void CppException(void*) { throw std::runtime_error("expected"); }
int main()
{
    SampleNative::Fault fault; int value = 0;
    if (!SampleNative::Invoke(NoFault, &value, &fault) || value != 1) return 1;
    if (SampleNative::Invoke(AccessViolation, nullptr, &fault) || fault.code != EXCEPTION_ACCESS_VIOLATION ||
        fault.accessKind != 1 || fault.accessAddress != 0x12345678 || !fault.address) return 2;
    if (SampleNative::Invoke(IllegalInstruction, nullptr, &fault) || fault.code != EXCEPTION_ILLEGAL_INSTRUCTION) return 3;
    bool caught = false;
    try { SampleNative::Invoke(CppException, nullptr, &fault); } catch (const std::runtime_error&) { caught = true; }
    if (!caught || SampleNative::IsHardwareFault(0xE06D7363) || SampleNative::IsHardwareFault(EXCEPTION_STACK_OVERFLOW)) return 4;
    if (SampleNative::Invoke(ActualAccessViolation, nullptr, &fault) || fault.code != EXCEPTION_ACCESS_VIOLATION ||
        fault.accessKind != 1 || fault.accessAddress != 1) return 5;
    puts("PASS: normal callback, synthetic AV/parameters, real invalid write, illegal instruction, C++ propagation, fatal-stack exclusion.");
    return 0;
}
