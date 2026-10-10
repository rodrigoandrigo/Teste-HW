#include "pch.h"
#include "AmdVDiagnostics.h"

#include <intrin.h>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct InstructionProbe
	{
		bool allocated;
		bool protectedExecutable;
		bool executed;
		DWORD allocationError;
		DWORD protectionError;
		DWORD exceptionCode;
	};

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	DiagnosticResult MakeResult(const wchar_t* name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Virtualization / AMD-V";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	InstructionProbe ExecuteGeneratedInstruction(const uint8_t* code, size_t codeSize)
	{
		InstructionProbe probe = {};
		SYSTEM_INFO systemInfo = {};
		GetNativeSystemInfo(&systemInfo);
		void* memory = VirtualAllocFromApp(nullptr, systemInfo.dwPageSize,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (memory == nullptr)
		{
			probe.allocationError = GetLastError();
			return probe;
		}
		probe.allocated = true;
		std::memcpy(memory, code, codeSize);
		DWORD previousProtection = 0;
		if (!VirtualProtectFromApp(memory, systemInfo.dwPageSize, PAGE_EXECUTE_READ, &previousProtection))
		{
			probe.protectionError = GetLastError();
			VirtualFree(memory, 0, MEM_RELEASE);
			return probe;
		}
		probe.protectedExecutable = true;
		FlushInstructionCache(GetCurrentProcess(), memory, codeSize);
		__try
		{
			reinterpret_cast<void(*)()>(memory)();
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		VirtualFree(memory, 0, MEM_RELEASE);
		return probe;
	}

	void AddInstructionProbe(std::vector<DiagnosticResult>& results, const wchar_t* name,
		const uint8_t* code, size_t codeSize)
	{
		auto probe = ExecuteGeneratedInstruction(code, codeSize);
		DiagnosticStatus status = probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked;
		std::wstring summary;
		if (probe.executed)
			summary = L"A instrução AMD SVM foi executada diretamente pelo processo.";
		else if (!probe.allocated || !probe.protectedExecutable)
			summary = L"A sonda dinâmica não pôde ser preparada para execução.";
		else
			summary = L"A instrução AMD SVM foi bloqueada e convertida em uma exceção SEH.";
		auto result = MakeResult(name, status, summary,
			L"Allocated=" + std::wstring(probe.allocated ? L"true" : L"false") +
			L"; Executable=" + std::wstring(probe.protectedExecutable ? L"true" : L"false") +
			L"; Executed=" + std::wstring(probe.executed ? L"true" : L"false") +
			L"; ExceptionCode=" + Hex32(probe.exceptionCode) +
			L"; AllocationError=" + std::to_wstring(probe.allocationError) +
			L"; ProtectionError=" + std::to_wstring(probe.protectionError) + L".");
		DWORD error = probe.allocationError ? probe.allocationError : probe.protectionError;
		if (error != ERROR_SUCCESS)
		{
			result.hasWin32Error = true;
			result.win32Error = error;
		}
		results.push_back(std::move(result));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> AmdVDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();

#if !defined(_M_X64)
		results.push_back(MakeResult(L"AMD-V architecture", DiagnosticStatus::Skipped,
			L"As sondas AMD-V deste aplicativo são implementadas somente para x64."));
#else
		int registers[4] = {};
		__cpuidex(registers, static_cast<int>(0x80000000u), 0);
		uint32_t maximumExtendedLeaf = static_cast<uint32_t>(registers[0]);
		uint32_t extendedEcx = 0;
		if (maximumExtendedLeaf >= 0x80000001u)
		{
			__cpuidex(registers, static_cast<int>(0x80000001u), 0);
			extendedEcx = static_cast<uint32_t>(registers[2]);
		}
		bool svmAdvertised = (extendedEcx & (1u << 2)) != 0;
		results.push_back(MakeResult(L"CPUID AMD SVM availability",
			svmAdvertised ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			svmAdvertised ? L"CPUID anuncia suporte AMD SVM ao processo."
				: L"CPUID não expõe AMD SVM ao processo.",
			L"MaxExtendedLeaf=" + Hex32(maximumExtendedLeaf) +
			L"; Leaf80000001.ECX=" + Hex32(extendedEcx) +
			L"; SVM=" + std::wstring(svmAdvertised ? L"yes" : L"no") + L"."));

		if (maximumExtendedLeaf >= 0x8000000Au)
		{
			__cpuidex(registers, static_cast<int>(0x8000000Au), 0);
			uint32_t eax = static_cast<uint32_t>(registers[0]);
			uint32_t ebx = static_cast<uint32_t>(registers[1]);
			uint32_t edx = static_cast<uint32_t>(registers[3]);
			results.push_back(MakeResult(L"CPUID SVM feature leaf",
				svmAdvertised ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				L"A folha arquitetural SVM foi lida.",
				L"Leaf8000000A.EAX=" + Hex32(eax) + L"; Revision=" + std::to_wstring(eax & 0xFF) +
				L"; ASIDCount=" + std::to_wstring(ebx) + L"; FeaturesEDX=" + Hex32(edx) +
				L"; NestedPaging=" + std::wstring((edx & 1) ? L"yes" : L"no") +
				L"; NRIPSave=" + std::wstring((edx & (1u << 3)) ? L"yes" : L"no") +
				L"; DecodeAssist=" + std::wstring((edx & (1u << 7)) ? L"yes" : L"no") + L"."));
		}
		else
		{
			results.push_back(MakeResult(L"CPUID SVM feature leaf", DiagnosticStatus::Blocked,
				L"A folha CPUID 0x8000000A não está disponível."));
		}

		// xor eax,eax; <SVM instruction>; ret. RAX=0 avoids exposing an arbitrary
		// physical address if a platform unexpectedly allows the instruction.
		const uint8_t vmrun[]  = { 0x31, 0xC0, 0x0F, 0x01, 0xD8, 0xC3 };
		const uint8_t vmload[] = { 0x31, 0xC0, 0x0F, 0x01, 0xDA, 0xC3 };
		const uint8_t vmsave[] = { 0x31, 0xC0, 0x0F, 0x01, 0xDB, 0xC3 };
		const uint8_t stgi[]   = { 0x0F, 0x01, 0xDC, 0xC3 };
		const uint8_t clgi[]   = { 0x0F, 0x01, 0xDD, 0xC3 };
		const uint8_t skinit[] = { 0x31, 0xC0, 0x0F, 0x01, 0xDE, 0xC3 };
		const uint8_t invlpga[] = { 0x31, 0xC0, 0x31, 0xC9, 0x0F, 0x01, 0xDF, 0xC3 };
		AddInstructionProbe(results, L"VMRUN instruction", vmrun, sizeof(vmrun));
		AddInstructionProbe(results, L"VMLOAD instruction", vmload, sizeof(vmload));
		AddInstructionProbe(results, L"VMSAVE instruction", vmsave, sizeof(vmsave));
		AddInstructionProbe(results, L"STGI instruction", stgi, sizeof(stgi));
		AddInstructionProbe(results, L"CLGI instruction", clgi, sizeof(clgi));
		AddInstructionProbe(results, L"SKINIT instruction", skinit, sizeof(skinit));
		AddInstructionProbe(results, L"INVLPGA instruction", invlpga, sizeof(invlpga));
#endif

		DiagnosticStatus aggregate = DiagnosticStatus::Pass;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Fail) { aggregate = DiagnosticStatus::Fail; break; }
			if (result.status == DiagnosticStatus::Blocked) aggregate = DiagnosticStatus::Blocked;
			else if (result.status == DiagnosticStatus::Skipped && aggregate == DiagnosticStatus::Pass)
				aggregate = DiagnosticStatus::Skipped;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
		auto completion = MakeResult(L"AMD-V suite completion", aggregate,
			aggregate == DiagnosticStatus::Pass
				? L"AMD SVM foi anunciado e as instruções testadas ficaram acessíveis."
				: L"AMD-V não está diretamente utilizável pelo processo; examine CPUID e as exceções das instruções.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
