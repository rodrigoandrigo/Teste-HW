#include "pch.h"
#include "HyperVWmiDiagnostics.h"

// O SDK oculta a ABI WMI de projetos UWP. Expondo somente as declarações COM
// conseguimos testar em runtime se o AppContainer realmente alcança o serviço.
#undef WINAPI_PARTITION_PKG_WINMGMT
#define WINAPI_PARTITION_PKG_WINMGMT 1
#include <Wbemidl.h>
#include <wrl/client.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Microsoft::WRL::ComPtr;
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;
	using CoSetProxyBlanketFn = HRESULT (WINAPI*)(IUnknown*, DWORD, DWORD, OLECHAR*,
		DWORD, DWORD, RPC_AUTH_IDENTITY_HANDLE, DWORD);

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
		result.category = L"Virtualization / Hyper-V WMI";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	void AddHr(std::vector<DiagnosticResult>& results, const wchar_t* name, HRESULT hr,
		const wchar_t* success, const wchar_t* failure, const std::wstring& extra = L"")
	{
		auto result = MakeResult(name, SUCCEEDED(hr) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			SUCCEEDED(hr) ? success : failure,
			L"HRESULT=" + Hex32(static_cast<uint32_t>(hr)) + (extra.empty() ? L"." : L"; " + extra));
		result.hasHresult = true;
		result.hresult = hr;
		results.push_back(std::move(result));
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> HyperVWmiDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();
		HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool uninitialize = SUCCEEDED(initHr);
		bool comUsable = SUCCEEDED(initHr) || initHr == RPC_E_CHANGED_MODE;
		AddHr(results, L"CoInitializeEx", initHr,
			L"COM foi inicializado para a consulta WMI.",
			initHr == RPC_E_CHANGED_MODE ? L"COM já usa outro modelo, mas permanece utilizável." : L"COM não pôde ser inicializado.");

		if (comUsable)
		{
			HRESULT securityHr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
				RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
			if (securityHr == RPC_E_TOO_LATE)
				AddHr(results, L"CoInitializeSecurity", S_OK,
					L"A segurança COM já havia sido inicializada no processo.", L"", L"OriginalHRESULT=" + Hex32(static_cast<uint32_t>(securityHr)));
			else
				AddHr(results, L"CoInitializeSecurity", securityHr,
					L"A segurança COM foi configurada.", L"A configuração de segurança COM falhou.");

			ComPtr<IWbemLocator> locator;
			HRESULT locatorHr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&locator));
			AddHr(results, L"CoCreateInstance(WbemLocator)", locatorHr,
				L"O localizador WMI foi criado.", L"O AppContainer não conseguiu criar o localizador WMI.");

			if (SUCCEEDED(locatorHr))
			{
				BSTR namespaceName = SysAllocString(L"ROOT\\virtualization\\v2");
				ComPtr<IWbemServices> services;
				HRESULT connectHr = locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr,
					0, nullptr, nullptr, &services);
				SysFreeString(namespaceName);
				AddHr(results, L"ConnectServer(ROOT\\virtualization\\v2)", connectHr,
					L"O namespace WMI do Hyper-V está acessível.",
					L"O namespace WMI do Hyper-V não está disponível ou foi negado.");

				if (SUCCEEDED(connectHr))
				{
					HMODULE ole32 = GetModuleHandleW(L"ole32.dll");
					auto setProxyBlanket = ole32 == nullptr ? nullptr :
						reinterpret_cast<CoSetProxyBlanketFn>(GetProcAddress(ole32, "CoSetProxyBlanket"));
					HRESULT blanketHr = setProxyBlanket == nullptr ? HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND) :
						setProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
							RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
					AddHr(results, L"CoSetProxyBlanket(Hyper-V WMI)", blanketHr,
						L"A segurança do proxy WMI foi configurada.", L"O proxy WMI rejeitou a configuração de segurança.");

					BSTR language = SysAllocString(L"WQL");
					BSTR query = SysAllocString(L"SELECT Name, ElementName, EnabledState FROM Msvm_ComputerSystem");
					ComPtr<IEnumWbemClassObject> enumerator;
					HRESULT queryHr = services->ExecQuery(language, query,
						WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
					SysFreeString(query);
					SysFreeString(language);
					AddHr(results, L"ExecQuery(Msvm_ComputerSystem)", queryHr,
						L"A consulta WQL às máquinas virtuais do Hyper-V foi aceita.",
						L"O namespace abriu, mas a consulta Hyper-V falhou.");

					if (SUCCEEDED(queryHr))
					{
						ComPtr<IWbemClassObject> object;
						ULONG returned = 0;
						HRESULT nextHr = enumerator->Next(3000, 1, &object, &returned);
						std::wstring extra = L"Returned=" + std::to_wstring(returned);
						if (SUCCEEDED(nextHr) && returned == 1)
						{
							VARIANT value;
							VariantInit(&value);
							HRESULT getHr = object->Get(L"ElementName", 0, &value, nullptr, nullptr);
							if (SUCCEEDED(getHr) && value.vt == VT_BSTR && value.bstrVal)
								extra += L"; FirstElementName=" + std::wstring(value.bstrVal);
							VariantClear(&value);
						}
						AddHr(results, L"IEnumWbemClassObject::Next", nextHr,
							returned ? L"A consulta retornou uma instância Msvm_ComputerSystem."
								: L"A consulta foi executada; nenhuma instância foi retornada.",
							L"A enumeração WMI falhou ou excedeu o tempo limite.", extra);
					}
				}
			}
		}

		if (uninitialize)
			CoUninitialize();

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
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
		auto completion = MakeResult(L"Hyper-V WMI suite completion", aggregateStatus,
			aggregateStatus == DiagnosticStatus::Pass
				? L"A cadeia COM, namespace Hyper-V e consulta WQL foi concluída sem bloqueios."
				: L"A cadeia Hyper-V WMI terminou bloqueada; examine o primeiro HRESULT registrado.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
