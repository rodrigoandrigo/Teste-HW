#include "pch.h"
#include "DirectMlDiagnostics.h"

#include <d3d12.h>
#include <DirectML.h>
#include <wrl/client.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Microsoft::WRL::ComPtr;
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;
	using D3D12CreateDeviceFn = HRESULT (WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	using DmlCreateDeviceFn = HRESULT (WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	std::wstring FeatureLevelText(DML_FEATURE_LEVEL level)
	{
		uint32_t value = static_cast<uint32_t>(level);
		return std::to_wstring(value >> 12) + L"." + std::to_wstring((value >> 8) & 0xF) +
			L" (" + Hex32(value) + L")";
	}

	DiagnosticResult MakeResult(const wchar_t* name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Graphics / DirectML";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticResult HrResult(const wchar_t* name, HRESULT hr, const wchar_t* success,
		const wchar_t* failure, const std::wstring& extra = L"")
	{
		auto result = MakeResult(name, SUCCEEDED(hr) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			SUCCEEDED(hr) ? success : failure,
			L"HRESULT=" + Hex32(static_cast<uint32_t>(hr)) + (extra.empty() ? L"." : L"; " + extra));
		result.hasHresult = true;
		result.hresult = hr;
		return result;
	}

	void Complete(std::vector<DiagnosticResult>& results,
		std::chrono::high_resolution_clock::time_point started)
	{
		DiagnosticStatus aggregate = DiagnosticStatus::Pass;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Fail) { aggregate = DiagnosticStatus::Fail; break; }
			if (result.status == DiagnosticStatus::Blocked) aggregate = DiagnosticStatus::Blocked;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
		auto completion = MakeResult(L"DirectML suite completion", aggregate,
			aggregate == DiagnosticStatus::Pass
				? L"O device DirectML e um operador compilado foram criados com sucesso."
				: L"A cadeia DirectML encontrou um bloqueio; examine o primeiro erro registrado.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> DirectMlDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();

		SetLastError(ERROR_SUCCESS);
		HMODULE d3d12Module = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (d3d12Module == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Load d3d12.dll", DiagnosticStatus::Blocked,
				L"O AppContainer não conseguiu carregar o runtime D3D12.",
				L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			Complete(results, started);
			return results;
		}
		results.push_back(MakeResult(L"Load d3d12.dll", DiagnosticStatus::Pass,
			L"O runtime D3D12 foi carregado a partir do diretório do sistema."));

		auto createD3d12Device = reinterpret_cast<D3D12CreateDeviceFn>(
			GetProcAddress(d3d12Module, "D3D12CreateDevice"));
		if (createD3d12Device == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Resolve D3D12CreateDevice", DiagnosticStatus::Blocked,
				L"A exportação D3D12CreateDevice não foi encontrada.",
				L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			FreeLibrary(d3d12Module);
			Complete(results, started);
			return results;
		}

		ComPtr<ID3D12Device> d3d12Device;
		HRESULT d3dHr = createD3d12Device(nullptr, D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&d3d12Device));
		results.push_back(HrResult(L"D3D12CreateDevice", d3dHr,
			L"Um device D3D12 foi criado para servir de base ao DirectML.",
			L"Não foi possível criar o device D3D12 exigido pelo DirectML.",
			L"MinimumFeatureLevel=11.0"));
		if (FAILED(d3dHr))
		{
			FreeLibrary(d3d12Module);
			Complete(results, started);
			return results;
		}

		SetLastError(ERROR_SUCCESS);
		HMODULE directMlModule = LoadLibraryExW(L"DirectML.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (directMlModule == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Load DirectML.dll", DiagnosticStatus::Blocked,
				L"O AppContainer não conseguiu carregar DirectML.dll.",
				L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			d3d12Device.Reset();
			FreeLibrary(d3d12Module);
			Complete(results, started);
			return results;
		}
		results.push_back(MakeResult(L"Load DirectML.dll", DiagnosticStatus::Pass,
			L"DirectML.dll foi carregada a partir do diretório do sistema."));

		auto createDmlDevice = reinterpret_cast<DmlCreateDeviceFn>(
			GetProcAddress(directMlModule, "DMLCreateDevice"));
		if (createDmlDevice == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Resolve DMLCreateDevice", DiagnosticStatus::Blocked,
				L"A exportação DMLCreateDevice não foi encontrada.",
				L"GetLastError=" + std::to_wstring(error) + L".");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			d3d12Device.Reset();
			FreeLibrary(directMlModule);
			FreeLibrary(d3d12Module);
			Complete(results, started);
			return results;
		}

		ComPtr<IDMLDevice> dmlDevice;
		HRESULT dmlHr = createDmlDevice(d3d12Device.Get(), DML_CREATE_DEVICE_FLAG_NONE,
			IID_PPV_ARGS(&dmlDevice));
		results.push_back(HrResult(L"DMLCreateDevice", dmlHr,
			L"O device DirectML foi criado sobre o device D3D12.",
			L"DirectML.dll está presente, mas recusou a criação do device."));
		if (SUCCEEDED(dmlHr))
		{
			const DML_FEATURE_LEVEL requestedLevels[] =
			{
				DML_FEATURE_LEVEL_6_4, DML_FEATURE_LEVEL_6_3, DML_FEATURE_LEVEL_6_2,
				DML_FEATURE_LEVEL_6_1, DML_FEATURE_LEVEL_6_0, DML_FEATURE_LEVEL_5_2,
				DML_FEATURE_LEVEL_5_1, DML_FEATURE_LEVEL_5_0, DML_FEATURE_LEVEL_4_1,
				DML_FEATURE_LEVEL_4_0, DML_FEATURE_LEVEL_3_1, DML_FEATURE_LEVEL_3_0,
				DML_FEATURE_LEVEL_2_1, DML_FEATURE_LEVEL_2_0, DML_FEATURE_LEVEL_1_0
			};
			DML_FEATURE_QUERY_FEATURE_LEVELS query = { ARRAYSIZE(requestedLevels), requestedLevels };
			DML_FEATURE_DATA_FEATURE_LEVELS featureData = {};
			HRESULT featureHr = dmlDevice->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS,
				sizeof(query), &query, sizeof(featureData), &featureData);
			results.push_back(HrResult(L"DirectML feature level", featureHr,
				L"O nível máximo de recursos DirectML foi consultado.",
				L"O device DirectML não respondeu à consulta de feature level.",
				L"MaxSupportedFeatureLevel=" + FeatureLevelText(featureData.MaxSupportedFeatureLevel)));

			const struct { DML_TENSOR_DATA_TYPE type; const wchar_t* name; } dataTypes[] =
			{
				{ DML_TENSOR_DATA_TYPE_FLOAT32, L"FLOAT32" },
				{ DML_TENSOR_DATA_TYPE_FLOAT16, L"FLOAT16" },
				{ DML_TENSOR_DATA_TYPE_UINT8, L"UINT8" },
				{ DML_TENSOR_DATA_TYPE_INT8, L"INT8" }
			};
			std::wostringstream typeDetails;
			bool typeQueriesPassed = true;
			for (const auto& entry : dataTypes)
			{
				DML_FEATURE_QUERY_TENSOR_DATA_TYPE_SUPPORT typeQuery = { entry.type };
				DML_FEATURE_DATA_TENSOR_DATA_TYPE_SUPPORT typeSupport = {};
				HRESULT typeHr = dmlDevice->CheckFeatureSupport(DML_FEATURE_TENSOR_DATA_TYPE_SUPPORT,
					sizeof(typeQuery), &typeQuery, sizeof(typeSupport), &typeSupport);
				if (FAILED(typeHr)) typeQueriesPassed = false;
				typeDetails << entry.name << L"=" << (SUCCEEDED(typeHr) && typeSupport.IsSupported ? L"yes" : L"no")
					<< L"(" << Hex32(static_cast<uint32_t>(typeHr)) << L") ";
			}
			results.push_back(MakeResult(L"DirectML tensor data types",
				typeQueriesPassed ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				typeQueriesPassed ? L"As consultas de tipos de tensor foram concluídas."
					: L"Uma ou mais consultas de tipos de tensor falharam.", typeDetails.str()));

			const UINT sizes[] = { 4 };
			DML_BUFFER_TENSOR_DESC bufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT32,
				DML_TENSOR_FLAG_NONE, ARRAYSIZE(sizes), sizes, nullptr, 16, 0 };
			DML_TENSOR_DESC tensorDesc = { DML_TENSOR_TYPE_BUFFER, &bufferDesc };
			DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC identityDesc = { &tensorDesc, &tensorDesc, nullptr };
			DML_OPERATOR_DESC operatorDesc = { DML_OPERATOR_ELEMENT_WISE_IDENTITY, &identityDesc };
			ComPtr<IDMLOperator> identityOperator;
			HRESULT operatorHr = dmlDevice->CreateOperator(&operatorDesc, IID_PPV_ARGS(&identityOperator));
			results.push_back(HrResult(L"Create DirectML Identity operator", operatorHr,
				L"Um operador Identity FLOAT32 foi validado e criado.",
				L"O device existe, mas não conseguiu criar um operador básico."));

			if (SUCCEEDED(operatorHr))
			{
				ComPtr<IDMLCompiledOperator> compiledOperator;
				HRESULT compileHr = dmlDevice->CompileOperator(identityOperator.Get(),
					DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&compiledOperator));
				results.push_back(HrResult(L"Compile DirectML Identity operator", compileHr,
					L"O operador Identity foi compilado para execução na GPU.",
					L"O operador foi criado, mas sua compilação falhou."));
			}
		}

		dmlDevice.Reset();
		d3d12Device.Reset();
		FreeLibrary(directMlModule);
		FreeLibrary(d3d12Module);
		Complete(results, started);
		return results;
	}
}
}
