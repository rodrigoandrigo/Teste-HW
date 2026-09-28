#include "pch.h"
#include "ComActivationDiagnostics.h"

#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace
{
	using Microsoft::WRL::ComPtr;
	using Teste_HW::Diagnostics::DiagnosticResult;
	using Teste_HW::Diagnostics::DiagnosticStatus;

	struct ComProbe
	{
		const wchar_t* name;
		GUID classId;
		DWORD classContext;
		const wchar_t* purpose;
	};

	struct WinRtProbe
	{
		const wchar_t* runtimeClass;
		const wchar_t* purpose;
	};

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	std::wstring GuidText(const GUID& value)
	{
		wchar_t buffer[40] = {};
		StringFromGUID2(value, buffer, ARRAYSIZE(buffer));
		return buffer;
	}

	DiagnosticResult MakeResult(const std::wstring& name, DiagnosticStatus status,
		const std::wstring& summary, const std::wstring& details = L"")
	{
		DiagnosticResult result;
		result.category = L"COM / API Activation Matrix";
		result.name = name;
		result.status = status;
		result.summary = summary;
		result.details = details;
		return result;
	}

	DiagnosticStatus ActivationStatus(HRESULT hr)
	{
		return SUCCEEDED(hr) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked;
	}

	DiagnosticResult ActivationResult(const std::wstring& name, HRESULT hr,
		const std::wstring& success, const std::wstring& failure, const std::wstring& details)
	{
		auto result = MakeResult(name, ActivationStatus(hr), SUCCEEDED(hr) ? success : failure,
			L"HRESULT=" + Hex32(static_cast<uint32_t>(hr)) + L"; " + details);
		result.hasHresult = true;
		result.hresult = hr;
		return result;
	}

	// Local GUIDs avoid importing registration metadata just to ask the runtime
	// whether these known, side-effect-free factories are activatable.
	const ComProbe ComProbes[] =
	{
		{ L"WIC Imaging Factory", { 0xCACAF262, 0x9370, 0x4615, { 0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A } }, CLSCTX_INPROC_SERVER, L"Image codecs and pixel conversion" },
		{ L"WIC Imaging Factory 2", { 0x317D06E8, 0x5F24, 0x433D, { 0xBD, 0xF7, 0x79, 0xCE, 0x68, 0xD8, 0xAB, 0xC2 } }, CLSCTX_INPROC_SERVER, L"Extended Windows Imaging Component factory" },
		{ L"WMI Locator", { 0x4590F811, 0x1D3A, 0x11D0, { 0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24 } }, CLSCTX_INPROC_SERVER, L"WMI namespace access" },
		{ L"MMDevice Enumerator", { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } }, CLSCTX_INPROC_SERVER, L"Windows audio endpoint enumeration" },
		{ L"MSXML6 DOM Document", { 0x88D96A05, 0xF192, 0x11D4, { 0xA6, 0x5F, 0x00, 0x40, 0x96, 0x32, 0x51, 0xE5 } }, CLSCTX_INPROC_SERVER, L"XML DOM parsing" },
		{ L"SAPI SpVoice", { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } }, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, L"Legacy speech synthesis" },
		{ L"DirectShow Filter Graph", { 0xE436EBB3, 0x524F, 0x11CE, { 0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } }, CLSCTX_INPROC_SERVER, L"Legacy media filter graph" },
		{ L"Shell Link", { 0x00021401, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } }, CLSCTX_INPROC_SERVER, L"Legacy Windows shell integration" },
		{ L"Task Scheduler", { 0x0F87369F, 0xA4E5, 0x4CFC, { 0xBD, 0x3E, 0x73, 0xE6, 0x15, 0x45, 0x72, 0xDD } }, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, L"Windows scheduled-task service" },
	};

	const WinRtProbe WinRtProbes[] =
	{
		{ L"Windows.Storage.ApplicationData", L"Package-local storage and settings" },
		{ L"Windows.Storage.KnownFolders", L"Brokered known-folder access" },
		{ L"Windows.System.MemoryManager", L"Application memory reporting" },
		{ L"Windows.Graphics.Imaging.BitmapDecoder", L"WinRT image decoding" },
		{ L"Windows.Media.MediaProperties.MediaEncodingProfile", L"Media encoding profiles" },
		{ L"Windows.Devices.Enumeration.DeviceInformation", L"Brokered device enumeration" },
		{ L"Windows.Networking.Sockets.StreamSocket", L"WinRT TCP sockets" },
		{ L"Windows.Security.Cryptography.CryptographicBuffer", L"WinRT cryptographic buffers" },
		{ L"Windows.ApplicationModel.Package", L"Current package metadata" },
	};
}

namespace Teste_HW
{
namespace Diagnostics
{
	std::vector<DiagnosticResult> ComActivationDiagnostics::Run()
	{
		std::vector<DiagnosticResult> results;
		auto suiteStart = std::chrono::high_resolution_clock::now();

		HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool comInitialized = SUCCEEDED(comHr);
		bool comUsable = comInitialized || comHr == RPC_E_CHANGED_MODE;
		results.push_back(ActivationResult(L"COM apartment initialization", comHr,
			L"A thread está pronta para ativar componentes COM.",
			comHr == RPC_E_CHANGED_MODE
				? L"COM já usa outro apartment nesta thread; a matriz continuará com o apartment existente."
				: L"Não foi possível inicializar COM nesta thread.",
			L"RequestedApartment=MTA"));

		if (comUsable)
		{
			for (const auto& probe : ComProbes)
			{
				ComPtr<IUnknown> instance;
				HRESULT hr = CoCreateInstance(probe.classId, nullptr, probe.classContext,
					IID_IUnknown, reinterpret_cast<void**>(instance.GetAddressOf()));
				results.push_back(ActivationResult(L"COM: " + std::wstring(probe.name), hr,
					L"A classe COM foi localizada e uma instância IUnknown foi criada.",
					L"A classe COM não pôde ser ativada neste AppContainer.",
					L"CLSID=" + GuidText(probe.classId) + L"; ClassContext=" +
					std::to_wstring(probe.classContext) + L"; Purpose=" + probe.purpose));
			}
		}

		if (comInitialized) CoUninitialize();

		HRESULT roHr = RoInitialize(RO_INIT_MULTITHREADED);
		bool roInitialized = SUCCEEDED(roHr);
		bool roUsable = roInitialized || roHr == RPC_E_CHANGED_MODE;
		results.push_back(ActivationResult(L"WinRT initialization", roHr,
			L"A thread está pronta para consultar activation factories WinRT.",
			roHr == RPC_E_CHANGED_MODE
				? L"WinRT já usa outro apartment; a matriz continuará com o apartment existente."
				: L"Não foi possível inicializar WinRT nesta thread.",
			L"RequestedApartment=MTA"));

		if (roUsable)
		{
			for (const auto& probe : WinRtProbes)
			{
				HSTRING_HEADER header = {};
				HSTRING className = nullptr;
				HRESULT stringHr = WindowsCreateStringReference(probe.runtimeClass,
					static_cast<UINT32>(wcslen(probe.runtimeClass)), &header, &className);
				ComPtr<IActivationFactory> factory;
				HRESULT hr = SUCCEEDED(stringHr)
					? RoGetActivationFactory(className, IID_PPV_ARGS(&factory)) : stringHr;
				results.push_back(ActivationResult(L"WinRT: " + std::wstring(probe.runtimeClass), hr,
					L"A activation factory WinRT está disponível para o pacote.",
					L"A runtime class WinRT não pôde fornecer uma activation factory.",
					L"RuntimeClass=" + std::wstring(probe.runtimeClass) + L"; Purpose=" + probe.purpose));
			}
		}

		if (roInitialized) RoUninitialize();

		uint32_t passed = 0;
		uint32_t blocked = 0;
		for (const auto& result : results)
		{
			if (result.status == DiagnosticStatus::Pass) ++passed;
			else if (result.status == DiagnosticStatus::Blocked) ++blocked;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - suiteStart).count();
		DiagnosticStatus completionStatus = passed > 0 ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked;
		auto completion = MakeResult(L"COM/API activation matrix completion", completionStatus,
			passed > 0
				? L"A matriz terminou; resultados PASS identificam factories realmente ativáveis."
				: L"Nenhuma factory do catálogo pôde ser ativada.",
			L"Passed=" + std::to_wstring(passed) + L"; Blocked=" + std::to_wstring(blocked) +
			L"; CatalogOnly=true; FunctionalCallsBeyondActivation=false.");
		completion.durationMicroseconds = static_cast<uint64_t>(elapsed);
		results.push_back(std::move(completion));
		return results;
	}
}
}
