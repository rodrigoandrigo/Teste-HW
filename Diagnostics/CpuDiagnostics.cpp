#include "pch.h"
#include "CpuDiagnostics.h"

#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#include <immintrin.h>
#endif

namespace
{
	Teste_HW::Diagnostics::DiagnosticResult MakeResult(
		const wchar_t* name,
		Teste_HW::Diagnostics::DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		Teste_HW::Diagnostics::DiagnosticResult result;
		result.category = L"CPU / Instructions";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}
}

#if defined(_M_IX86) || defined(_M_X64)
namespace
{
	struct InstructionProbe
	{
		bool executed;
		DWORD exceptionCode;
		uint64_t value;
		uint32_t auxiliary;
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

	InstructionProbe ProbeRdtsc()
	{
		InstructionProbe probe = {};
		__try
		{
			probe.value = __rdtsc();
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	InstructionProbe ProbeRdtscp()
	{
		InstructionProbe probe = {};
		__try
		{
			probe.value = __rdtscp(&probe.auxiliary);
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	InstructionProbe ProbeXgetbv()
	{
		InstructionProbe probe = {};
		__try
		{
			probe.value = _xgetbv(0);
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	InstructionProbe ProbeFsgsbase()
	{
		InstructionProbe probe = {};
		__try
		{
#if defined(_M_X64)
			probe.value = _readfsbase_u64();
#else
			probe.value = _readfsbase_u32();
#endif
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	InstructionProbe ProbeAvx()
	{
		InstructionProbe probe = {};
		__try
		{
			volatile float seed = static_cast<float>(GetTickCount() & 0xFFFF);
			__m256 left = _mm256_set1_ps(seed);
			__m256 right = _mm256_set1_ps(2.0f);
			__m256 sum = _mm256_add_ps(left, right);
			alignas(32) float values[8] = {};
			_mm256_store_ps(values, sum);
			probe.value = static_cast<uint64_t>(values[0]);
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	InstructionProbe ProbeAvx2()
	{
		InstructionProbe probe = {};
		__try
		{
			volatile int seed = static_cast<int>(GetTickCount() & 0xFFFF);
			__m256i left = _mm256_set1_epi32(seed);
			__m256i right = _mm256_set1_epi32(2);
			__m256i sum = _mm256_add_epi32(left, right);
			alignas(32) int values[8] = {};
			_mm256_store_si256(reinterpret_cast<__m256i*>(values), sum);
			probe.value = static_cast<uint32_t>(values[0]);
			probe.executed = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			probe.exceptionCode = GetExceptionCode();
		}
		return probe;
	}

	std::wstring ProbeDetails(const InstructionProbe& probe)
	{
		if (probe.executed)
		{
			return L"Instruction executed; observed value=" + Hex64(probe.value) +
				L", auxiliary=" + Hex32(probe.auxiliary) + L".";
		}
		return L"Instruction raised SEH exception " + Hex32(probe.exceptionCode) + L".";
	}
}
#endif

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> CpuDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

#if !defined(_M_IX86) && !defined(_M_X64)
		results.push_back(MakeResult(
			L"x86 CPUID suite",
			DiagnosticStatus::Skipped,
			L"CPUID and x86 instruction probes are not applicable to this architecture."));
#else
		int registers[4] = {};
		__cpuidex(registers, 0, 0);
		const uint32_t maxBasicLeaf = static_cast<uint32_t>(registers[0]);
		char vendorBytes[13] = {};
		std::memcpy(vendorBytes + 0, &registers[1], 4);
		std::memcpy(vendorBytes + 4, &registers[3], 4);
		std::memcpy(vendorBytes + 8, &registers[2], 4);
		std::wstring vendor(vendorBytes, vendorBytes + std::strlen(vendorBytes));

		__cpuidex(registers, static_cast<int>(0x80000000u), 0);
		const uint32_t maxExtendedLeaf = static_cast<uint32_t>(registers[0]);
		char brandBytes[49] = {};
		if (maxExtendedLeaf >= 0x80000004u)
		{
			for (uint32_t leaf = 0; leaf < 3; ++leaf)
			{
				__cpuidex(registers, static_cast<int>(0x80000002u + leaf), 0);
				std::memcpy(brandBytes + leaf * 16, registers, 16);
			}
		}
		std::wstring brand(brandBytes, brandBytes + std::strlen(brandBytes));
		while (!brand.empty() && brand.front() == L' ') brand.erase(brand.begin());

		results.push_back(MakeResult(
			L"CPUID identity",
			DiagnosticStatus::Pass,
			L"CPUID executed successfully.",
			L"Vendor=" + vendor + L"; Brand=" + brand + L"; MaxBasicLeaf=" +
			Hex32(maxBasicLeaf) + L"; MaxExtendedLeaf=" + Hex32(maxExtendedLeaf) + L"."));

		__cpuidex(registers, 1, 0);
		const uint32_t leaf1Ecx = static_cast<uint32_t>(registers[2]);
		const uint32_t leaf1Edx = static_cast<uint32_t>(registers[3]);
		const uint32_t signature = static_cast<uint32_t>(registers[0]);
		const uint32_t stepping = signature & 0xF;
		uint32_t model = (signature >> 4) & 0xF;
		uint32_t family = (signature >> 8) & 0xF;
		const uint32_t extendedModel = (signature >> 16) & 0xF;
		const uint32_t extendedFamily = (signature >> 20) & 0xFF;
		if (family == 0xF) family += extendedFamily;
		if (family == 0x6 || family == 0xF) model += extendedModel << 4;
		const bool xsave = (leaf1Ecx & (1u << 26)) != 0;
		const bool osxsave = (leaf1Ecx & (1u << 27)) != 0;
		const bool avx = (leaf1Ecx & (1u << 28)) != 0;
		const bool hypervisor = (leaf1Ecx & (1u << 31)) != 0;
		const bool rdtscAdvertised = (leaf1Edx & (1u << 4)) != 0;

		uint32_t leaf7Ebx = 0;
		uint32_t leaf7Ecx = 0;
		if (maxBasicLeaf >= 7)
		{
			__cpuidex(registers, 7, 0);
			leaf7Ebx = static_cast<uint32_t>(registers[1]);
			leaf7Ecx = static_cast<uint32_t>(registers[2]);
		}
		const bool fsgsbase = (leaf7Ebx & (1u << 0)) != 0;
		const bool avx2 = (leaf7Ebx & (1u << 5)) != 0;

		uint32_t extendedEcx = 0;
		uint32_t extendedEdx = 0;
		bool invariantTsc = false;
		if (maxExtendedLeaf >= 0x80000001u)
		{
			__cpuidex(registers, static_cast<int>(0x80000001u), 0);
			extendedEcx = static_cast<uint32_t>(registers[2]);
			extendedEdx = static_cast<uint32_t>(registers[3]);
		}
		if (maxExtendedLeaf >= 0x80000007u)
		{
			__cpuidex(registers, static_cast<int>(0x80000007u), 0);
			invariantTsc = (static_cast<uint32_t>(registers[3]) & (1u << 8)) != 0;
		}
		const bool rdtscp = (extendedEdx & (1u << 27)) != 0;
		const bool svm = (extendedEcx & (1u << 2)) != 0;

		std::wostringstream featureDetails;
		featureDetails << L"Leaf1.ECX=" << Hex32(leaf1Ecx) << L"; Leaf1.EDX=" << Hex32(leaf1Edx)
			<< L"; Leaf7.EBX=" << Hex32(leaf7Ebx) << L"; Leaf7.ECX=" << Hex32(leaf7Ecx)
			<< L"; Family=" << family << L"; Model=" << model << L"; Stepping=" << stepping
			<< L"; SSE2=" << ((leaf1Edx & (1u << 26)) ? L"yes" : L"no")
			<< L"; SSE4.1=" << ((leaf1Ecx & (1u << 19)) ? L"yes" : L"no")
			<< L"; SSE4.2=" << ((leaf1Ecx & (1u << 20)) ? L"yes" : L"no")
			<< L"; AES=" << ((leaf1Ecx & (1u << 25)) ? L"yes" : L"no")
			<< L"; PCLMULQDQ=" << ((leaf1Ecx & (1u << 1)) ? L"yes" : L"no")
			<< L"; BMI1=" << ((leaf7Ebx & (1u << 3)) ? L"yes" : L"no")
			<< L"; BMI2=" << ((leaf7Ebx & (1u << 8)) ? L"yes" : L"no")
			<< L"; RDRAND=" << ((leaf1Ecx & (1u << 30)) ? L"yes" : L"no")
			<< L"; RDSEED=" << ((leaf7Ebx & (1u << 18)) ? L"yes" : L"no")
			<< L"; LAHF/SAHF=" << ((extendedEcx & 1u) ? L"yes" : L"no")
			<< L"; InvariantTSC=" << (invariantTsc ? L"yes" : L"no")
			<< L"; VMX=" << ((leaf1Ecx & (1u << 5)) ? L"yes" : L"no")
			<< L"; SVM=" << (svm ? L"yes" : L"no") << L".";
		results.push_back(MakeResult(L"CPUID feature map", DiagnosticStatus::Pass,
			L"Raw feature leaves and kernel-relevant CPU capabilities were collected.", featureDetails.str()));

		std::wstring hypervisorVendor = L"not advertised";
		if (hypervisor)
		{
			__cpuidex(registers, static_cast<int>(0x40000000u), 0);
			char hypervisorBytes[13] = {};
			std::memcpy(hypervisorBytes + 0, &registers[1], 4);
			std::memcpy(hypervisorBytes + 4, &registers[2], 4);
			std::memcpy(hypervisorBytes + 8, &registers[3], 4);
			hypervisorVendor.assign(hypervisorBytes, hypervisorBytes + std::strlen(hypervisorBytes));
		}
		results.push_back(MakeResult(L"Hypervisor CPUID", DiagnosticStatus::Pass,
			hypervisor ? L"CPUID reports execution under a hypervisor." : L"CPUID does not advertise a hypervisor.",
			L"HypervisorPresent=" + std::wstring(hypervisor ? L"yes" : L"no") + L"; Vendor=" + hypervisorVendor + L"."));

		InstructionProbe xgetbvProbe = {};
		if (xsave && osxsave)
		{
			xgetbvProbe = ProbeXgetbv();
			results.push_back(MakeResult(L"XSAVE / OSXSAVE / XGETBV",
				xgetbvProbe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				xgetbvProbe.executed ? L"XGETBV executed successfully." : L"XGETBV was advertised but trapped.",
				L"XSAVE=yes; OSXSAVE=yes; XCR0=" + Hex64(xgetbvProbe.value) + L"; " + ProbeDetails(xgetbvProbe)));
		}
		else
		{
			results.push_back(MakeResult(L"XSAVE / OSXSAVE / XGETBV", DiagnosticStatus::Skipped,
				L"XGETBV was not executed because XSAVE/OSXSAVE is unavailable.",
				L"XSAVE=" + std::wstring(xsave ? L"yes" : L"no") + L"; OSXSAVE=" + (osxsave ? L"yes" : L"no") + L"."));
		}

		const bool ymmStateEnabled = xgetbvProbe.executed && (xgetbvProbe.value & 0x6) == 0x6;
		if (avx && ymmStateEnabled)
		{
			auto probe = ProbeAvx();
			results.push_back(MakeResult(L"AVX execution", probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				probe.executed ? L"An AVX arithmetic instruction executed successfully." : L"AVX was advertised and enabled but trapped.", ProbeDetails(probe)));
		}
		else
		{
			results.push_back(MakeResult(L"AVX execution", DiagnosticStatus::Skipped,
				L"AVX execution prerequisites are not satisfied.",
				L"CPUID.AVX=" + std::wstring(avx ? L"yes" : L"no") + L"; XCR0.XMM+YMM=" + (ymmStateEnabled ? L"yes" : L"no") + L"."));
		}

		if (avx2 && ymmStateEnabled)
		{
			auto probe = ProbeAvx2();
			results.push_back(MakeResult(L"AVX2 execution", probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				probe.executed ? L"An AVX2 integer instruction executed successfully." : L"AVX2 was advertised and enabled but trapped.", ProbeDetails(probe)));
		}
		else
		{
			results.push_back(MakeResult(L"AVX2 execution", DiagnosticStatus::Skipped,
				L"AVX2 execution prerequisites are not satisfied."));
		}

		if (rdtscAdvertised)
		{
			auto probe = ProbeRdtsc();
			results.push_back(MakeResult(L"RDTSC execution", probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				probe.executed ? L"RDTSC executed successfully." : L"RDTSC was advertised but trapped.", ProbeDetails(probe)));
		}
		else results.push_back(MakeResult(L"RDTSC execution", DiagnosticStatus::Skipped, L"RDTSC is not advertised by CPUID."));

		if (rdtscp)
		{
			auto probe = ProbeRdtscp();
			results.push_back(MakeResult(L"RDTSCP execution", probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				probe.executed ? L"RDTSCP executed successfully." : L"RDTSCP was advertised but trapped.", ProbeDetails(probe)));
		}
		else results.push_back(MakeResult(L"RDTSCP execution", DiagnosticStatus::Skipped, L"RDTSCP is not advertised by CPUID."));

		if (fsgsbase)
		{
			auto probe = ProbeFsgsbase();
			results.push_back(MakeResult(L"FSGSBASE execution", probe.executed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				probe.executed ? L"RDFSBASE executed successfully." : L"FSGSBASE was advertised but RDFSBASE trapped.", ProbeDetails(probe)));
		}
		else results.push_back(MakeResult(L"FSGSBASE execution", DiagnosticStatus::Skipped, L"FSGSBASE is not advertised by CPUID."));

		if (maxBasicLeaf >= 0xD && xsave)
		{
			__cpuidex(registers, 0xD, 0);
			std::wostringstream details;
			details << L"SupportedXCR0Mask=" << Hex64(
				(static_cast<uint64_t>(static_cast<uint32_t>(registers[3])) << 32) |
				static_cast<uint32_t>(registers[0]))
				<< L"; EnabledStateSize=" << static_cast<uint32_t>(registers[1])
				<< L"; MaximumStateSize=" << static_cast<uint32_t>(registers[2]) << L" bytes.";
			results.push_back(MakeResult(L"XSAVE state layout", DiagnosticStatus::Pass,
				L"CPUID leaf 0xD state sizing was collected.", details.str()));
		}
#endif

		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		auto completion = MakeResult(
			L"CPU suite completion",
			DiagnosticStatus::Pass,
			L"CPU identification and instruction probes completed.",
			L"Individual instruction results are recorded separately.");
		completion.durationMicroseconds = static_cast<uint64_t>(duration);
		results.push_back(std::move(completion));
		return results;
	}
}
}
