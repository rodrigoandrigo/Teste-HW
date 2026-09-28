#include "pch.h"
#include "VmComputeDiagnostics.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;
	using GetServicePropertiesFn = HRESULT (WINAPI*)(PCWSTR, PWSTR*);

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
		result.category = L"Virtualization / vmcompute";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> VmComputeDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();

		SetLastError(ERROR_SUCCESS);
		HMODULE module = LoadLibraryExW(L"vmcompute.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (module == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Load vmcompute.dll", DiagnosticStatus::Blocked,
				L"O AppContainer não conseguiu carregar vmcompute.dll.",
				L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			return results;
		}

		results.push_back(MakeResult(L"Load vmcompute.dll", DiagnosticStatus::Pass,
			L"vmcompute.dll foi carregada a partir do diretório do sistema."));

		auto getServiceProperties = reinterpret_cast<GetServicePropertiesFn>(
			GetProcAddress(module, "HcsGetServiceProperties"));
		if (getServiceProperties == nullptr)
		{
			DWORD error = GetLastError();
			auto result = MakeResult(L"Resolve HcsGetServiceProperties", DiagnosticStatus::Blocked,
				L"A exportação de consulta do Host Compute Service não está disponível.",
				L"GetLastError=" + std::to_wstring(error) + L" (" + Hex32(error) + L").");
			result.hasWin32Error = true;
			result.win32Error = error;
			results.push_back(std::move(result));
			FreeLibrary(module);
			return results;
		}

		results.push_back(MakeResult(L"Resolve HcsGetServiceProperties", DiagnosticStatus::Pass,
			L"A API de consulta do Host Compute Service foi resolvida."));

		PWSTR response = nullptr;
		HRESULT hr = getServiceProperties(L"{}", &response);
		std::wstring details = L"HRESULT=" + Hex32(static_cast<uint32_t>(hr));
		if (response != nullptr)
		{
			std::wstring document(response);
			if (document.size() > 2048)
				document = document.substr(0, 2048) + L"... [truncado]";
			details += L"; Response=" + document;
			LocalFree(response);
		}
		else
		{
			details += L"; Response=<null>";
		}

		auto query = MakeResult(L"HcsGetServiceProperties", SUCCEEDED(hr) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
			SUCCEEDED(hr)
				? L"O AppContainer consultou o Host Compute Service com sucesso."
				: L"vmcompute.dll foi carregada, mas o acesso ao Host Compute Service falhou.",
			details);
		query.hasHresult = true;
		query.hresult = hr;
		results.push_back(std::move(query));

		FreeLibrary(module);
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
		auto completion = MakeResult(L"vmcompute suite completion", DiagnosticStatus::Pass,
			L"A sondagem de carregamento e acesso ao serviço foi concluída.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
