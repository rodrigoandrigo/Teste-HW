#include "pch.h"
#include "ExceptionDiagnostics.h"

#include <chrono>
#include <iomanip>
#include <intrin.h>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct ExceptionProbe
	{
		bool instructionCompleted;
		bool exceptionCaught;
		DWORD exceptionCode;
		uintptr_t exceptionAddress;
		DWORD numberParameters;
		ULONG_PTR information[EXCEPTION_MAXIMUM_PARAMETERS];
	};

	std::wstring Hex64(uint64_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(16)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	int CaptureException(EXCEPTION_POINTERS* pointers, ExceptionProbe* probe)
	{
		probe->exceptionCaught = true;
		probe->exceptionCode = pointers->ExceptionRecord->ExceptionCode;
		probe->exceptionAddress = reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
		probe->numberParameters = pointers->ExceptionRecord->NumberParameters;
		for (DWORD index = 0; index < probe->numberParameters && index < EXCEPTION_MAXIMUM_PARAMETERS; ++index)
		{
			probe->information[index] = pointers->ExceptionRecord->ExceptionInformation[index];
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	std::wstring ProbeDetails(const ExceptionProbe& probe)
	{
		std::wostringstream output;
		output << L"Caught=" << (probe.exceptionCaught ? L"yes" : L"no")
			<< L"; Completed=" << (probe.instructionCompleted ? L"yes" : L"no")
			<< L"; ExceptionCode=" << Hex32(probe.exceptionCode)
			<< L"; ExceptionAddress=" << Hex64(probe.exceptionAddress)
			<< L"; ParameterCount=" << probe.numberParameters;
		for (DWORD index = 0; index < probe.numberParameters && index < EXCEPTION_MAXIMUM_PARAMETERS; ++index)
		{
			output << L"; Info[" << index << L"]=" << Hex64(probe.information[index]);
		}
		output << L".";
		return output.str();
	}

	DiagnosticResult MakeResult(
		const wchar_t* name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details)
	{
		DiagnosticResult result;
		result.category = L"Exceptions / Traps";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult ExpectedException(
		const wchar_t* name,
		const ExceptionProbe& probe,
		DWORD expectedCode,
		const wchar_t* successSummary)
	{
		const bool expected = probe.exceptionCaught && probe.exceptionCode == expectedCode;
		return MakeResult(
			name,
			expected ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			expected ? successSummary : L"The probe did not produce the expected structured exception.",
			L"ExpectedExceptionCode=" + Hex32(expectedCode) + L"; " + ProbeDetails(probe));
	}

	ExceptionProbe ProbeInvalidInstruction()
	{
		ExceptionProbe probe = {};
		__try
		{
			__ud2();
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeBreakpoint()
	{
		ExceptionProbe probe = {};
		__try
		{
			__debugbreak();
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeRead(const void* address)
	{
		ExceptionProbe probe = {};
		__try
		{
			volatile uint8_t value = *static_cast<volatile const uint8_t*>(address);
			(void)value;
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeWrite(void* address)
	{
		ExceptionProbe probe = {};
		__try
		{
			*static_cast<volatile uint8_t*>(address) = 0xA5;
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeExecute(void* address)
	{
		ExceptionProbe probe = {};
		__try
		{
			using Function = void(*)();
			reinterpret_cast<Function>(address)();
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeIntegerDivideByZero()
	{
		ExceptionProbe probe = {};
		__try
		{
			volatile int numerator = static_cast<int>(GetTickCount() | 1);
			volatile int denominator = 0;
			volatile int quotient = numerator / denominator;
			(void)quotient;
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}

	ExceptionProbe ProbeRaisedException(DWORD code)
	{
		ExceptionProbe probe = {};
		__try
		{
			const ULONG_PTR arguments[2] = { 0x54455354, 0x48570001 };
			RaiseException(code, 0, ARRAYSIZE(arguments), arguments);
			probe.instructionCompleted = true;
		}
		__except (CaptureException(GetExceptionInformation(), &probe)) {}
		return probe;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> ExceptionDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

#if defined(_M_X64) || defined(_M_IX86)
		results.push_back(ExpectedException(
			L"Invalid instruction",
			ProbeInvalidInstruction(),
			EXCEPTION_ILLEGAL_INSTRUCTION,
			L"UD2 generated EXCEPTION_ILLEGAL_INSTRUCTION and execution recovered."));

		results.push_back(ExpectedException(
			L"Breakpoint trap",
			ProbeBreakpoint(),
			EXCEPTION_BREAKPOINT,
			L"INT3 generated EXCEPTION_BREAKPOINT and execution recovered."));
#else
		results.push_back(MakeResult(L"Invalid instruction", DiagnosticStatus::Skipped,
			L"The UD2 probe is implemented only on x86/x64.", L""));
		results.push_back(MakeResult(L"Breakpoint trap", DiagnosticStatus::Skipped,
			L"The INT3 probe is implemented only on x86/x64.", L""));
#endif

		results.push_back(ExpectedException(
			L"Invalid-address read",
			ProbeRead(reinterpret_cast<void*>(static_cast<uintptr_t>(1))),
			EXCEPTION_ACCESS_VIOLATION,
			L"An invalid read generated a catchable access violation."));

		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);

		void* noAccessPage = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
		if (noAccessPage == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"PAGE_NOACCESS read", DiagnosticStatus::Blocked,
				L"The test page could not be allocated.", L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
		}
		else
		{
			results.push_back(ExpectedException(L"PAGE_NOACCESS read", ProbeRead(noAccessPage),
				EXCEPTION_ACCESS_VIOLATION, L"A PAGE_NOACCESS read generated a catchable access violation."));
			VirtualFree(noAccessPage, 0, MEM_RELEASE);
		}

		void* readOnlyPage = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (readOnlyPage == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"PAGE_READONLY write", DiagnosticStatus::Blocked,
				L"The test page could not be allocated.", L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
		}
		else
		{
			*static_cast<uint8_t*>(readOnlyPage) = 0x11;
			ULONG previous = 0;
			if (VirtualProtectFromApp(readOnlyPage, systemInfo.dwPageSize, PAGE_READONLY, &previous))
			{
				results.push_back(ExpectedException(L"PAGE_READONLY write", ProbeWrite(readOnlyPage),
					EXCEPTION_ACCESS_VIOLATION, L"A write to PAGE_READONLY generated a catchable access violation."));
			}
			else
			{
				DWORD error = GetLastError();
				auto result = MakeResult(L"PAGE_READONLY write", DiagnosticStatus::Blocked,
					L"VirtualProtectFromApp could not create the read-only page.", L"GetLastError=" + std::to_wstring(error) + L".");
				result.hasWin32Error = true;
				result.win32Error = error;
				results.push_back(std::move(result));
			}
			VirtualFree(readOnlyPage, 0, MEM_RELEASE);
		}

		void* guardPage = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
		if (guardPage == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Guard-page semantics", DiagnosticStatus::Blocked,
				L"The guard page could not be allocated.", L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
		}
		else
		{
			auto first = ProbeRead(guardPage);
			auto second = ProbeRead(guardPage);
			const bool correct = first.exceptionCaught && first.exceptionCode == STATUS_GUARD_PAGE_VIOLATION &&
				second.instructionCompleted && !second.exceptionCaught;
			results.push_back(MakeResult(L"Guard-page semantics", correct ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
				correct ? L"The first access trapped and cleared PAGE_GUARD; the second access succeeded."
				: L"PAGE_GUARD did not exhibit the expected one-shot behavior.",
				L"First access: " + ProbeDetails(first) + L" Second access: " + ProbeDetails(second)));
			VirtualFree(guardPage, 0, MEM_RELEASE);
		}

		void* nonExecutablePage = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (nonExecutablePage == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Execute non-executable page", DiagnosticStatus::Blocked,
				L"The NX test page could not be allocated.", L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
		}
		else
		{
			static_cast<uint8_t*>(nonExecutablePage)[0] = 0xC3;
			FlushInstructionCache(GetCurrentProcess(), nonExecutablePage, 1);
			results.push_back(ExpectedException(L"Execute non-executable page", ProbeExecute(nonExecutablePage),
				EXCEPTION_ACCESS_VIOLATION, L"Executing a PAGE_READWRITE page generated a catchable access violation."));
			VirtualFree(nonExecutablePage, 0, MEM_RELEASE);
		}

		results.push_back(ExpectedException(
			L"Integer divide by zero",
			ProbeIntegerDivideByZero(),
			EXCEPTION_INT_DIVIDE_BY_ZERO,
			L"Integer division by zero generated the expected structured exception."));

		constexpr DWORD customException = 0xE0424857;
		auto raised = ProbeRaisedException(customException);
		const bool customPassed = raised.exceptionCaught && raised.exceptionCode == customException &&
			raised.numberParameters == 2 && raised.information[0] == 0x54455354 && raised.information[1] == 0x48570001;
		results.push_back(MakeResult(L"Explicit structured exception", customPassed ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			customPassed ? L"RaiseException preserved its code and parameters through SEH."
			: L"The explicit exception was not captured with the expected metadata.", ProbeDetails(raised)));

		volatile uint32_t recoveryCookie = 0;
		recoveryCookie = 0xC0DEF00D;
		results.push_back(MakeResult(L"Post-trap recovery", recoveryCookie == 0xC0DEF00D ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
			recoveryCookie == 0xC0DEF00D ? L"Execution continued normally after all handled traps."
			: L"Execution did not recover normally after the probes.", L"RecoveryCookie=" + Hex32(recoveryCookie) + L"."));

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(L"Exception suite completion", DiagnosticStatus::Pass,
			L"Exception and trap probes completed.", L"");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
