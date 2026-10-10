#include "pch.h"
#include "KvmDiagnostics.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	DiagnosticResult MakeResult(const std::wstring& name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"Virtualization / KVM";
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
	std::vector<DiagnosticResult> KvmDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto started = std::chrono::high_resolution_clock::now();
		results.push_back(MakeResult(L"KVM host ABI", DiagnosticStatus::Inconclusive,
			L"KVM é uma interface de kernel Linux; o Windows não fornece /dev/kvm como API nativa.",
			L"O teste procura pontes de dispositivo explícitas, mas não considera uma DLL como implementação KVM."));

		const wchar_t* devicePaths[] =
		{
			L"\\\\.\\KVM",
			L"\\\\.\\Global\\KVM",
			L"\\\\?\\GLOBALROOT\\Device\\KVM"
		};
		bool anyDeviceOpened = false;
		for (const wchar_t* path : devicePaths)
		{
			CREATEFILE2_EXTENDED_PARAMETERS parameters = {};
			parameters.dwSize = sizeof(parameters);
			parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
			parameters.dwFileFlags = 0;
			SetLastError(ERROR_SUCCESS);
			HANDLE device = CreateFile2FromAppW(path, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &parameters);
			DWORD openError = device == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
			auto openResult = MakeResult(L"Open KVM device " + std::wstring(path),
				device != INVALID_HANDLE_VALUE ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
				device != INVALID_HANDLE_VALUE
					? L"Uma ponte de dispositivo KVM foi aberta pelo AppContainer."
					: L"Nenhum dispositivo KVM acessível foi encontrado neste caminho.",
				L"Path=" + std::wstring(path) + L"; GetLastError=" + std::to_wstring(openError) +
				L" (" + Hex32(openError) + L").");
			if (openError != ERROR_SUCCESS)
			{
				openResult.hasWin32Error = true;
				openResult.win32Error = openError;
			}
			results.push_back(std::move(openResult));

			if (device != INVALID_HANDLE_VALUE)
			{
				anyDeviceOpened = true;
				// Linux x86 encodes KVM_GET_API_VERSION as ioctl number 0xAE00.
				// DeviceIoControl is not the Linux ioctl ABI; this is only meaningful
				// if a Windows bridge deliberately accepts the same request number.
				DWORD apiVersion = 0;
				DWORD bytesReturned = 0;
				SetLastError(ERROR_SUCCESS);
				BOOL ioctlSucceeded = DeviceIoControl(device, 0x0000AE00, nullptr, 0,
					&apiVersion, sizeof(apiVersion), &bytesReturned, nullptr);
				DWORD ioctlError = ioctlSucceeded ? ERROR_SUCCESS : GetLastError();
				auto ioctlResult = MakeResult(L"KVM_GET_API_VERSION bridge probe",
					ioctlSucceeded ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
					ioctlSucceeded ? L"A ponte respondeu à sonda experimental de versão KVM."
						: L"O dispositivo abriu, mas não aceitou a sonda experimental do ABI KVM.",
					L"Request=0x0000AE00; ApiVersion=" + std::to_wstring(apiVersion) +
					L"; BytesReturned=" + std::to_wstring(bytesReturned) +
					L"; GetLastError=" + std::to_wstring(ioctlError) + L" (" + Hex32(ioctlError) + L").");
				if (ioctlError != ERROR_SUCCESS)
				{
					ioctlResult.hasWin32Error = true;
					ioctlResult.win32Error = ioctlError;
				}
				results.push_back(std::move(ioctlResult));
				CloseHandle(device);
			}
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - started).count();
		auto completion = MakeResult(L"KVM suite completion",
			anyDeviceOpened ? DiagnosticStatus::Inconclusive : DiagnosticStatus::Blocked,
			anyDeviceOpened
				? L"Uma ponte de dispositivo foi encontrada; valide o resultado da sonda de ABI."
				: L"Nenhuma interface de dispositivo KVM ficou acessível ao AppContainer.",
			L"NativeWindowsKvmAbi=false; AnyDeviceOpened=" +
			std::wstring(anyDeviceOpened ? L"true" : L"false") + L".");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
