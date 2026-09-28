#include "pch.h"
#include "MemoryDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct JitProbe
	{
		bool executed;
		DWORD exceptionCode;
		uint32_t returnValue;
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

	std::wstring Bytes(uint64_t value)
	{
		std::wostringstream output;
		output << value << L" bytes (" << std::fixed << std::setprecision(2)
			<< (static_cast<long double>(value) / (1024.0L * 1024.0L)) << L" MiB)";
		return output.str();
	}

	DiagnosticResult MakeResult(
		const wchar_t* name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Virtual Memory / JIT";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult Win32Failure(const wchar_t* name, const std::wstring& summary, DWORD error)
	{
		auto result = MakeResult(name, DiagnosticStatus::Blocked, summary,
			L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
		result.hasWin32Error = true;
		result.win32Error = error;
		return result;
	}

	JitProbe ExecuteGeneratedCode(void* address)
	{
		JitProbe probe = {};
		__try
		{
#if defined(_M_X64)
			using GeneratedFunction = uint32_t(*)();
			probe.returnValue = reinterpret_cast<GeneratedFunction>(address)();
			probe.executed = true;
#else
			(void)address;
#endif
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	bool ReleaseAllocation(void* address)
	{
		return address == nullptr || VirtualFree(address, 0, MEM_RELEASE) != FALSE;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> MemoryDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);
		std::wostringstream systemDetails;
		systemDetails << L"PageSize=" << systemInfo.dwPageSize
			<< L"; AllocationGranularity=" << systemInfo.dwAllocationGranularity
			<< L"; MinimumApplicationAddress=" << Hex64(reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress))
			<< L"; MaximumApplicationAddress=" << Hex64(reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress))
			<< L"; ActiveProcessorMask=" << Hex64(systemInfo.dwActiveProcessorMask)
			<< L"; ProcessorCount=" << systemInfo.dwNumberOfProcessors << L".";
		results.push_back(MakeResult(L"System address-space geometry", DiagnosticStatus::Pass,
			L"Native page and virtual-address parameters were collected.", systemDetails.str()));

		MEMORYSTATUSEX memoryStatus = {};
		memoryStatus.dwLength = sizeof(memoryStatus);
		if (GlobalMemoryStatusEx(&memoryStatus))
		{
			std::wostringstream details;
			details << L"MemoryLoad=" << memoryStatus.dwMemoryLoad << L"%; TotalPhysical=" << Bytes(memoryStatus.ullTotalPhys)
				<< L"; AvailablePhysical=" << Bytes(memoryStatus.ullAvailPhys)
				<< L"; CommitLimit=" << Bytes(memoryStatus.ullTotalPageFile)
				<< L"; CommitAvailable=" << Bytes(memoryStatus.ullAvailPageFile)
				<< L"; UserVirtualTotal=" << Bytes(memoryStatus.ullTotalVirtual)
				<< L"; UserVirtualAvailable=" << Bytes(memoryStatus.ullAvailVirtual) << L".";
			results.push_back(MakeResult(L"Physical memory and commit", DiagnosticStatus::Pass,
				L"Global memory and commit limits were queried without exhausting commit.", details.str()));
		}
		else
		{
			results.push_back(Win32Failure(L"Physical memory and commit", L"GlobalMemoryStatusEx failed.", GetLastError()));
		}

		const SIZE_T cycleSize = 32ull * 1024 * 1024;
		SetLastError(ERROR_SUCCESS);
		void* cycle = VirtualAllocFromApp(nullptr, cycleSize, MEM_RESERVE, PAGE_NOACCESS);
		if (cycle == nullptr)
		{
			results.push_back(Win32Failure(L"Reserve / commit / decommit / release",
				L"VirtualAllocFromApp MEM_RESERVE failed.", GetLastError()));
		}
		else
		{
			void* committed = VirtualAllocFromApp(cycle, cycleSize, MEM_COMMIT, PAGE_READWRITE);
			if (committed == nullptr)
			{
				auto error = GetLastError();
				ReleaseAllocation(cycle);
				results.push_back(Win32Failure(L"Reserve / commit / decommit / release",
					L"Reservation succeeded but MEM_COMMIT failed.", error));
			}
			else
			{
				volatile uint8_t* bytes = static_cast<volatile uint8_t*>(committed);
				for (SIZE_T offset = 0; offset < cycleSize; offset += systemInfo.dwPageSize) bytes[offset] = static_cast<uint8_t>(offset);
				const bool decommitted = VirtualFree(committed, cycleSize, MEM_DECOMMIT) != FALSE;
				const DWORD decommitError = decommitted ? ERROR_SUCCESS : GetLastError();
				const bool released = ReleaseAllocation(cycle);
				const DWORD releaseError = released ? ERROR_SUCCESS : GetLastError();
				auto status = decommitted && released ? DiagnosticStatus::Pass : DiagnosticStatus::Fail;
				auto result = MakeResult(L"Reserve / commit / decommit / release", status,
					status == DiagnosticStatus::Pass ? L"A 32 MiB region completed the full virtual-memory lifecycle."
					: L"One or more virtual-memory lifecycle operations failed.",
					L"ReservedAddress=" + Hex64(reinterpret_cast<uintptr_t>(cycle)) +
					L"; TouchedPages=" + std::to_wstring(cycleSize / systemInfo.dwPageSize) +
					L"; DecommitError=" + std::to_wstring(decommitError) +
					L"; ReleaseError=" + std::to_wstring(releaseError) + L".");
				if (decommitError != ERROR_SUCCESS || releaseError != ERROR_SUCCESS)
				{
					result.hasWin32Error = true;
					result.win32Error = decommitError != ERROR_SUCCESS ? decommitError : releaseError;
				}
				results.push_back(std::move(result));
			}
		}

		const uint64_t maximumAddress = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
		const uint64_t safeSearchCeiling = (std::min<uint64_t>)(8ull * 1024 * 1024 * 1024 * 1024, maximumAddress / 4);
		uint64_t largestReservation = 0;
		DWORD firstReservationError = ERROR_SUCCESS;
		for (uint64_t candidate = 64ull * 1024 * 1024; candidate <= safeSearchCeiling; candidate *= 2)
		{
			SetLastError(ERROR_SUCCESS);
			void* reservation = VirtualAllocFromApp(nullptr, static_cast<SIZE_T>(candidate), MEM_RESERVE, PAGE_NOACCESS);
			if (reservation == nullptr)
			{
				firstReservationError = GetLastError();
				break;
			}
			largestReservation = candidate;
			VirtualFree(reservation, 0, MEM_RELEASE);
			if (candidate > safeSearchCeiling / 2) break;
		}
		DiagnosticResult reserveResult = MakeResult(L"Large contiguous reservation", DiagnosticStatus::Pass,
			L"Progressive reserve-only probes completed without retaining address space.",
			L"LargestSuccessfulReservation=" + Bytes(largestReservation) +
			L"; SafetyCeiling=" + Bytes(safeSearchCeiling) +
			L"; FirstFailureError=" + std::to_wstring(firstReservationError) + L" (" + Hex32(firstReservationError) + L").");
		if (largestReservation == 0)
		{
			reserveResult.status = DiagnosticStatus::Blocked;
			reserveResult.hasWin32Error = true;
			reserveResult.win32Error = firstReservationError;
		}
		results.push_back(std::move(reserveResult));

		void* protection = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (protection == nullptr)
		{
			results.push_back(Win32Failure(L"Page protection transitions", L"Could not allocate the protection test page.", GetLastError()));
		}
		else
		{
			*static_cast<volatile uint8_t*>(protection) = 0x5A;
			ULONG previousProtection = 0;
			const bool readOnly = VirtualProtectFromApp(protection, systemInfo.dwPageSize, PAGE_READONLY, &previousProtection) != FALSE;
			const DWORD readOnlyError = readOnly ? ERROR_SUCCESS : GetLastError();
			ULONG readOnlyPrevious = 0;
			const bool writable = readOnly && VirtualProtectFromApp(protection, systemInfo.dwPageSize, PAGE_READWRITE, &readOnlyPrevious) != FALSE;
			const DWORD writableError = writable ? ERROR_SUCCESS : GetLastError();
			MEMORY_BASIC_INFORMATION memoryInfo = {};
			const SIZE_T queried = VirtualQuery(protection, &memoryInfo, sizeof(memoryInfo));
			const bool released = ReleaseAllocation(protection);
			auto status = readOnly && writable && queried == sizeof(memoryInfo) && released ? DiagnosticStatus::Pass : DiagnosticStatus::Fail;
			auto result = MakeResult(L"Page protection transitions", status,
				status == DiagnosticStatus::Pass ? L"RW to R to RW transitions and VirtualQuery succeeded."
				: L"A page protection operation failed.",
				L"InitialProtection=" + Hex32(previousProtection) + L"; ReadOnlyPrevious=" + Hex32(readOnlyPrevious) +
				L"; FinalProtection=" + Hex32(memoryInfo.Protect) + L"; ReadOnlyError=" + std::to_wstring(readOnlyError) +
				L"; WritableError=" + std::to_wstring(writableError) + L".");
			if (status != DiagnosticStatus::Pass)
			{
				result.hasWin32Error = true;
				result.win32Error = readOnlyError != ERROR_SUCCESS ? readOnlyError : writableError;
			}
			results.push_back(std::move(result));
		}

#if defined(_M_X64)
		const std::array<uint8_t, 6> generatedCode = { 0xB8, 0x78, 0x56, 0x34, 0x12, 0xC3 }; // mov eax,12345678h; ret
		void* executable = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (executable == nullptr)
		{
			results.push_back(Win32Failure(L"JIT W^X execution", L"Could not allocate a writable JIT page.", GetLastError()));
		}
		else
		{
			std::memcpy(executable, generatedCode.data(), generatedCode.size());
			FlushInstructionCache(GetCurrentProcess(), executable, generatedCode.size());
			ULONG writableProtection = 0;
			SetLastError(ERROR_SUCCESS);
			const bool madeExecutable = VirtualProtectFromApp(executable, systemInfo.dwPageSize, PAGE_EXECUTE_READ, &writableProtection) != FALSE;
			const DWORD protectError = madeExecutable ? ERROR_SUCCESS : GetLastError();
			JitProbe firstProbe = {};
			if (madeExecutable) firstProbe = ExecuteGeneratedCode(executable);

			bool secondTransition = false;
			JitProbe secondProbe = {};
			DWORD secondTransitionError = ERROR_SUCCESS;
			if (madeExecutable && firstProbe.executed)
			{
				ULONG executeProtection = 0;
				if (VirtualProtectFromApp(executable, systemInfo.dwPageSize, PAGE_READWRITE, &executeProtection))
				{
					static_cast<uint8_t*>(executable)[1] = 0x21;
					FlushInstructionCache(GetCurrentProcess(), executable, generatedCode.size());
					ULONG previous = 0;
					secondTransition = VirtualProtectFromApp(executable, systemInfo.dwPageSize, PAGE_EXECUTE_READ, &previous) != FALSE;
					if (secondTransition) secondProbe = ExecuteGeneratedCode(executable);
					else secondTransitionError = GetLastError();
				}
				else secondTransitionError = GetLastError();
			}

			ReleaseAllocation(executable);
			const bool passed = madeExecutable && firstProbe.executed && firstProbe.returnValue == 0x12345678u &&
				secondTransition && secondProbe.executed && secondProbe.returnValue == 0x12345621u;
			auto result = MakeResult(L"JIT W^X execution", passed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				passed ? L"Generated x64 code executed across two RW to RX transitions."
				: L"Dynamic code allocation, protection or execution was blocked.",
				L"FirstReturn=" + Hex32(firstProbe.returnValue) + L"; SecondReturn=" + Hex32(secondProbe.returnValue) +
				L"; ProtectError=" + std::to_wstring(protectError) + L"; SecondTransitionError=" +
				std::to_wstring(secondTransitionError) + L"; FirstException=" + Hex32(firstProbe.exceptionCode) +
				L"; SecondException=" + Hex32(secondProbe.exceptionCode) + L".");
			if (protectError != ERROR_SUCCESS || secondTransitionError != ERROR_SUCCESS)
			{
				result.hasWin32Error = true;
				result.win32Error = protectError != ERROR_SUCCESS ? protectError : secondTransitionError;
			}
			results.push_back(std::move(result));
		}
#else
		results.push_back(MakeResult(L"JIT W^X execution", DiagnosticStatus::Skipped,
			L"The generated instruction sequence is implemented only for x64."));
#endif

		for (SIZE_T executableSize : { static_cast<SIZE_T>(64 * 1024), static_cast<SIZE_T>(1024 * 1024), static_cast<SIZE_T>(16 * 1024 * 1024) })
		{
			void* block = VirtualAllocFromApp(nullptr, executableSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (block == nullptr)
			{
				auto result = Win32Failure(L"Large executable block", L"Allocation failed for " + Bytes(executableSize) + L".", GetLastError());
				results.push_back(std::move(result));
				break;
			}
			static_cast<uint8_t*>(block)[0] = 0xC3;
			ULONG previous = 0;
			const bool protectedBlock = VirtualProtectFromApp(block, executableSize, PAGE_EXECUTE_READ, &previous) != FALSE;
			const DWORD error = protectedBlock ? ERROR_SUCCESS : GetLastError();
			ReleaseAllocation(block);
			auto result = MakeResult(L"Large executable block", protectedBlock ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				protectedBlock ? L"A large RW allocation transitioned to RX." : L"The RW to RX transition was blocked.",
				L"Size=" + Bytes(executableSize) + L"; PreviousProtection=" + Hex32(previous) + L"; Error=" + std::to_wstring(error) + L".");
			if (error != ERROR_SUCCESS) { result.hasWin32Error = true; result.win32Error = error; }
			results.push_back(std::move(result));
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(L"Memory/JIT suite completion", DiagnosticStatus::Pass,
			L"Virtual memory and dynamic-code probes completed.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
