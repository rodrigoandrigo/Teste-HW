#include "pch.h"
#include "WhpxDiagnostics.h"

#include <WinHvPlatformDefs.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	using GetCapabilityFn = HRESULT (WINAPI*)(WHV_CAPABILITY_CODE, void*, UINT32, UINT32*);
	using CreatePartitionFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE*);
	using SetPartitionPropertyFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE, WHV_PARTITION_PROPERTY_CODE, const void*, UINT32);
	using SetupPartitionFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE);
	using DeletePartitionFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE);
	using CreateVirtualProcessorFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE, UINT32, UINT32);
	using DeleteVirtualProcessorFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE, UINT32);
	using GetVirtualProcessorRegistersFn = HRESULT (WINAPI*)(WHV_PARTITION_HANDLE, UINT32, const WHV_REGISTER_NAME*, UINT32, WHV_REGISTER_VALUE*);

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

	DiagnosticResult MakeResult(
		const wchar_t* name,
		DiagnosticStatus status,
		const std::wstring& summary,
		const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Virtualization / WHPX";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult HresultResult(
		const wchar_t* name,
		HRESULT value,
		const wchar_t* successSummary,
		const wchar_t* failureSummary,
		const std::wstring& extraDetails = L"")
	{
		auto result = MakeResult(
			name,
			SUCCEEDED(value) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			SUCCEEDED(value) ? successSummary : failureSummary,
			L"HRESULT=" + Hex32(static_cast<uint32_t>(value)) +
			(extraDetails.empty() ? L"." : L"; " + extraDetails));
		result.hasHresult = true;
		result.hresult = value;
		return result;
	}

	template<typename T>
	T Resolve(HMODULE module, const char* name)
	{
		return reinterpret_cast<T>(GetProcAddress(module, name));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> WhpxDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();
		auto complete = [&results, suiteStart](DiagnosticStatus status, const std::wstring& summary)
		{
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::high_resolution_clock::now() - suiteStart).count();
			auto completion = MakeResult(L"WHPX suite completion", status, summary);
			completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
			results.push_back(std::move(completion));
		};

#if !defined(_M_X64)
		results.push_back(MakeResult(L"WHPX architecture", DiagnosticStatus::Skipped,
			L"This application validates WHPX only on x64."));
#else
		SetLastError(ERROR_SUCCESS);
		HMODULE module = LoadLibraryExW(L"WinHvPlatform.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (module == nullptr)
		{
			const DWORD error = GetLastError();
			auto result = MakeResult(L"Load WinHvPlatform.dll", DiagnosticStatus::Blocked,
				L"The AppContainer could not load WinHvPlatform.dll.",
				L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			complete(DiagnosticStatus::Blocked,
				L"A cadeia WHPX foi bloqueada antes da consulta de capacidades porque a DLL não pôde ser carregada.");
			return results;
		}
		results.push_back(MakeResult(L"Load WinHvPlatform.dll", DiagnosticStatus::Pass,
			L"WinHvPlatform.dll was loaded from the system directory."));

		auto getCapability = Resolve<GetCapabilityFn>(module, "WHvGetCapability");
		auto createPartition = Resolve<CreatePartitionFn>(module, "WHvCreatePartition");
		auto setPartitionProperty = Resolve<SetPartitionPropertyFn>(module, "WHvSetPartitionProperty");
		auto setupPartition = Resolve<SetupPartitionFn>(module, "WHvSetupPartition");
		auto deletePartition = Resolve<DeletePartitionFn>(module, "WHvDeletePartition");
		auto createVirtualProcessor = Resolve<CreateVirtualProcessorFn>(module, "WHvCreateVirtualProcessor");
		auto deleteVirtualProcessor = Resolve<DeleteVirtualProcessorFn>(module, "WHvDeleteVirtualProcessor");
		auto getVirtualProcessorRegisters = Resolve<GetVirtualProcessorRegistersFn>(module, "WHvGetVirtualProcessorRegisters");

		std::vector<std::wstring> missingExports;
		if (!getCapability) missingExports.emplace_back(L"WHvGetCapability");
		if (!createPartition) missingExports.emplace_back(L"WHvCreatePartition");
		if (!setPartitionProperty) missingExports.emplace_back(L"WHvSetPartitionProperty");
		if (!setupPartition) missingExports.emplace_back(L"WHvSetupPartition");
		if (!deletePartition) missingExports.emplace_back(L"WHvDeletePartition");
		if (!createVirtualProcessor) missingExports.emplace_back(L"WHvCreateVirtualProcessor");
		if (!deleteVirtualProcessor) missingExports.emplace_back(L"WHvDeleteVirtualProcessor");
		if (!getVirtualProcessorRegisters) missingExports.emplace_back(L"WHvGetVirtualProcessorRegisters");

		if (!missingExports.empty())
		{
			std::wstring names;
			for (size_t index = 0; index < missingExports.size(); ++index)
			{
				if (index != 0) names += L", ";
				names += missingExports[index];
			}
			const DWORD error = GetLastError();
			auto result = MakeResult(L"Resolve WHPX exports", DiagnosticStatus::Blocked,
				L"One or more required WHPX exports are unavailable.",
				L"Missing=" + names + L"; LastLoaderError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			FreeLibrary(module);
			complete(DiagnosticStatus::Blocked,
				L"A cadeia WHPX foi bloqueada porque uma ou mais exportações obrigatórias não foram encontradas.");
			return results;
		}
		results.push_back(MakeResult(L"Resolve WHPX exports", DiagnosticStatus::Pass,
			L"All functions required for partition and vCPU creation were resolved."));

		WHV_CAPABILITY hypervisorCapability = {};
		UINT32 writtenSize = 0;
		HRESULT capabilityHr = getCapability(
			WHvCapabilityCodeHypervisorPresent,
			&hypervisorCapability,
			sizeof(hypervisorCapability),
			&writtenSize);
		results.push_back(HresultResult(
			L"WHvGetCapability(HypervisorPresent)",
			capabilityHr,
			L"The WHPX capability query succeeded.",
			L"The WHPX capability query was denied or unavailable.",
			L"HypervisorPresent=" + std::wstring(hypervisorCapability.HypervisorPresent ? L"true" : L"false") +
			L"; WrittenSize=" + std::to_wstring(writtenSize) + L" bytes"));

		WHV_CAPABILITY features = {};
		writtenSize = 0;
		HRESULT featuresHr = getCapability(WHvCapabilityCodeFeatures, &features, sizeof(features), &writtenSize);
		results.push_back(HresultResult(
			L"WHvGetCapability(Features)",
			featuresHr,
			L"The WHPX implementation feature mask was returned.",
			L"The WHPX feature query failed.",
			L"Features=" + Hex64(features.Features.AsUINT64) + L"; WrittenSize=" + std::to_wstring(writtenSize) + L" bytes"));

		WHV_PARTITION_HANDLE partition = nullptr;
		HRESULT createHr = createPartition(&partition);
		results.push_back(HresultResult(
			L"WHvCreatePartition",
			createHr,
			L"A WHPX partition object was created inside the AppContainer.",
			L"The AppContainer could not create a WHPX partition.",
			L"PartitionHandle=" + Hex64(reinterpret_cast<uintptr_t>(partition))));

		bool virtualProcessorCreated = false;
		if (SUCCEEDED(createHr) && partition != nullptr)
		{
			const UINT32 processorCount = 1;
			HRESULT propertyHr = setPartitionProperty(
				partition,
				WHvPartitionPropertyCodeProcessorCount,
				&processorCount,
				sizeof(processorCount));
			results.push_back(HresultResult(
				L"WHvSetPartitionProperty(ProcessorCount)",
				propertyHr,
				L"The partition accepted a one-vCPU configuration.",
				L"The partition rejected the processor-count property.",
				L"ProcessorCount=1"));

			if (SUCCEEDED(propertyHr))
			{
				HRESULT setupHr = setupPartition(partition);
				results.push_back(HresultResult(
					L"WHvSetupPartition",
					setupHr,
					L"The hypervisor instantiated the configured partition.",
					L"The hypervisor could not instantiate the partition."));

				if (SUCCEEDED(setupHr))
				{
					HRESULT createVpHr = createVirtualProcessor(partition, 0, 0);
					virtualProcessorCreated = SUCCEEDED(createVpHr);
					results.push_back(HresultResult(
						L"WHvCreateVirtualProcessor",
						createVpHr,
						L"Virtual processor 0 was created inside the partition.",
						L"Partition setup succeeded, but vCPU creation failed.",
						L"VpIndex=0; Flags=0"));

					if (virtualProcessorCreated)
					{
						const WHV_REGISTER_NAME registerNames[] = { WHvX64RegisterRip, WHvX64RegisterRflags };
						WHV_REGISTER_VALUE registerValues[ARRAYSIZE(registerNames)] = {};
						HRESULT registersHr = getVirtualProcessorRegisters(
							partition,
							0,
							registerNames,
							ARRAYSIZE(registerNames),
							registerValues);
						results.push_back(HresultResult(
							L"WHvGetVirtualProcessorRegisters",
							registersHr,
							L"The created vCPU accepted a register-state query.",
							L"The vCPU was created, but its register state could not be queried.",
							L"RIP=" + Hex64(registerValues[0].Reg64) + L"; RFLAGS=" + Hex64(registerValues[1].Reg64)));
					}
				}
			}

			if (virtualProcessorCreated)
			{
				HRESULT deleteVpHr = deleteVirtualProcessor(partition, 0);
				results.push_back(HresultResult(
					L"WHvDeleteVirtualProcessor",
					deleteVpHr,
					L"Virtual processor 0 was deleted cleanly.",
					L"The vCPU cleanup operation failed."));
			}

			HRESULT deletePartitionHr = deletePartition(partition);
			results.push_back(HresultResult(
				L"WHvDeletePartition",
				deletePartitionHr,
				L"The WHPX partition was deleted cleanly.",
				L"The partition cleanup operation failed."));
		}

		FreeLibrary(module);
#endif

		DiagnosticStatus aggregateStatus = DiagnosticStatus::Pass;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Fail)
			{
				aggregateStatus = DiagnosticStatus::Fail;
				break;
			}
			if (result.status == DiagnosticStatus::Blocked)
				aggregateStatus = DiagnosticStatus::Blocked;
		}
		complete(aggregateStatus,
			aggregateStatus == DiagnosticStatus::Pass
				? L"A cadeia WHPX chegou à criação e validação da vCPU sem bloqueios."
				: L"A cadeia WHPX terminou com pelo menos uma etapa bloqueada; examine o primeiro erro registrado.");
		return results;
	}
}
}
