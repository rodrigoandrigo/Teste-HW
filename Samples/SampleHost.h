#pragma once
#define ENABLE_XELL
#include "../pch.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "../../XeSS_SDK_3_0_2/samples/basic_sample_super_resolution_dx12/d3dx12.h"
#include "../../XeSS_SDK_3_0_2/inc/xell/xell_d3d12.h"
#include "../../XeSS_SDK_3_0_2/inc/xess/xess_d3d12.h"
#include "../../XeSS_SDK_3_0_2/inc/xess_fg/xefg_swapchain_d3d12.h"
#include "../../XeSS_SDK_3_0_2/inc/xess_fg/xefg_swapchain_debug.h"
#include "../../XeSS_SDK_3_0_2/inc/xess/xess_debug.h"
#include <vector>
#include <string>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <cstdarg>
#include <map>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <climits>
#include <thread>
#include "SampleJournal.h"

namespace SampleRuntime
{
extern thread_local HWND window;
extern thread_local HRESULT windowResult;
extern thread_local std::map<std::wstring, HMODULE> modules;
extern thread_local uint64_t sdkCalls;
extern thread_local uint64_t generatedFrames;
extern thread_local bool warned;
extern thread_local uint64_t executeCalls;
extern thread_local uint64_t sleepCalls;
extern thread_local uint64_t failedGeneration;
extern thread_local uint64_t presentedFrames;
extern thread_local uint64_t validLatencyReports, lastPresentDurationNs;
void ObserveLatencyReports(const xell_frame_report_t* reports);
extern thread_local uint64_t readbacks, firstImageHash, lastImageHash, nonUniformPixels;
extern thread_local bool imageChanged;
extern thread_local std::chrono::steady_clock::time_point renderStart;
extern std::atomic<bool> cancelled;
extern std::atomic<bool> unsafeRuntime;
extern thread_local bool traceSdk;
void Checkpoint(const char* stage);
void TraceApi(const char* api, bool begin, int result = 0);
HMODULE LoadRuntime(const wchar_t* dll);
class HrError : public std::runtime_error
{
public:
    HRESULT code;
    explicit HrError(HRESULT hr, const std::string& operation = "Windows/D3D operation failed") : std::runtime_error(operation), code(hr) {}
};
inline HRESULT ProbeGraphicsDevice(IUnknown* adapter, const wchar_t* label)
{
    using Teste_HW::Samples::SampleJournal;
    SampleJournal::SetOperation("D3D12 diagnostic device creation");
    SampleJournal::Write(std::wstring(L"D3D12 diagnostic BEGIN ") + label);
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    wchar_t line[512] = {};
    swprintf_s(line, L"D3D12 diagnostic %s FeatureLevel=11_0 HRESULT=0x%08X Device=%p", label, static_cast<unsigned>(hr), device.Get());
    SampleJournal::Write(line);
    if (device)
    {
        HRESULT reason = device->GetDeviceRemovedReason();
        LUID luid = device->GetAdapterLuid();
        swprintf_s(line, L"D3D12 diagnostic %s RemovedReason=0x%08X AdapterLuid=%08X:%08X", label, static_cast<unsigned>(reason), static_cast<unsigned>(luid.HighPart), luid.LowPart);
        SampleJournal::Write(line);
    }
    else SampleJournal::Write(L"GetDeviceRemovedReason unavailable: no device returned.");
    return FAILED(hr) ? hr : (device ? hr : E_POINTER);
}
inline UINT DiagnoseGraphicsInitialization()
{
    using Teste_HW::Samples::SampleJournal;
    SampleJournal::Write(L"D3D12 baseline: before this sample enables debug layer; prior process debug state unknown. Default adapter/FL11_0 matches DirectML device arguments; no DirectML workload is executed.");
    ProbeGraphicsDevice(nullptr, L"baseline default (DirectML-equivalent arguments)");
    {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        wchar_t line[256] = {};
        swprintf_s(line, L"Baseline CreateDXGIFactory2 Flags=0 HRESULT=0x%08X Factory=%p", static_cast<unsigned>(hr), factory.Get());
        SampleJournal::Write(line);
        if (SUCCEEDED(hr) && factory)
        {
            for (UINT index = 0; index < 16; ++index)
            {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                hr = factory->EnumAdapters1(index, &adapter);
                swprintf_s(line, L"Baseline EnumAdapters1 Index=%u HRESULT=0x%08X Adapter=%p", index, static_cast<unsigned>(hr), adapter.Get());
                SampleJournal::Write(line);
                if (FAILED(hr) || !adapter) break;
                DXGI_ADAPTER_DESC1 desc = {};
                hr = adapter->GetDesc1(&desc);
                if (FAILED(hr)) { SampleJournal::Write(L"Baseline GetDesc1 failed; selection trace will record its HRESULT."); continue; }
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                    ProbeGraphicsDevice(adapter.Get(), (L"baseline explicit index=" + std::to_wstring(index)).c_str());
            }
        }
    } // All baseline devices are released before changing debug-layer state.
    UINT flags = 0;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    wchar_t line[256] = {};
    swprintf_s(line, L"D3D12GetDebugInterface HRESULT=0x%08X Controller=%p", static_cast<unsigned>(hr), debug.Get());
    SampleJournal::Write(line);
    if (SUCCEEDED(hr) && debug)
    {
        SampleJournal::SetOperation("EnableDebugLayer");
        SampleJournal::Write(L"EnableDebugLayer BEGIN (process-wide)");
        debug->EnableDebugLayer();
        SampleJournal::Write(L"EnableDebugLayer END (void API; no HRESULT)");
        flags = DXGI_CREATE_FACTORY_DEBUG;
    }
#else
    SampleJournal::Write(L"Release build: this sample does not enable the debug layer.");
#endif
    ProbeGraphicsDevice(nullptr, L"after debug configuration default");
    SampleJournal::Write(L"Sample DXGI factory flags=" + std::to_wstring(flags));
    return flags;
}
template<class T> T Resolve(const wchar_t* dll, const char* symbol)
{
    auto& module = modules[dll];
    if (!module) {
        Teste_HW::Samples::SampleJournal::Write(L"LoadPackagedLibrary BEGIN " + std::wstring(dll));
        module = LoadRuntime(dll);
        DWORD error = module ? 0 : GetLastError();
        wchar_t line[256] = {};
        swprintf_s(line, L"LoadPackagedLibrary END %s ModuleBase=%p Win32=%lu", dll, module, error);
        Teste_HW::Samples::SampleJournal::Write(line);
        if (!module) SetLastError(error);
    }
    if (!module) {
        DWORD error = GetLastError();
        std::string library;
        for (const wchar_t* p = dll; *p; ++p) library.push_back(static_cast<char>(*p)); // SDK DLL basenames are ASCII.
        throw HrError(HRESULT_FROM_WIN32(error), "LoadPackagedLibrary(" + library + ")");
    }
    auto fn = GetProcAddress(module, symbol);
    if (!fn) throw HrError(HRESULT_FROM_WIN32(GetLastError()), std::string("GetProcAddress(") + symbol + ")");
    return reinterpret_cast<T>(fn);
}
template<class T, class... A> auto Invoke(const wchar_t* dll, const char* name, A... args)
    -> decltype(Resolve<T>(dll, name)(args...))
{
    auto function = Resolve<T>(dll, name);
    Teste_HW::Samples::SampleJournal::SetOperation(name);
    if (traceSdk) TraceApi(name, true);
    auto result = function(args...);
    if (traceSdk || static_cast<int>(result) != 0) TraceApi(name, false, static_cast<int>(result));
    ++sdkCalls;
    if (strcmp(name, "xessD3D12Execute") == 0) ++executeCalls;
    if (strcmp(name, "xellSleep") == 0) ++sleepCalls;
    if (static_cast<int>(result) < 0)
        throw std::runtime_error(std::string(name) + ": SDKResult=" + std::to_string(static_cast<int>(result)));
    if (static_cast<int>(result) > 0) warned = true;
    return result;
}
void Wait(HANDLE event);
HRESULT Present(IDXGISwapChain* chain, UINT syncInterval, UINT flags);
void ObserveFrame(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* output);
HRESULT LoadShader(LPCWSTR path, const D3D_SHADER_MACRO*, ID3DInclude*, const char* entry,
    const char*, UINT, UINT, ID3DBlob** blob, ID3DBlob** errors);
}

inline void ThrowIfFailed(HRESULT hr) { if (FAILED(hr)) throw SampleRuntime::HrError(hr); }
inline void DebugPrintf(const char* format, ...)
{
    char buffer[2048] = {};
    va_list args; va_start(args, format); vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args); va_end(args);
    OutputDebugStringA(buffer);
}
inline void SetName(ID3D12Object* object, LPCWSTR name) { object->SetName(name); }
inline void SetNameIndexed(ID3D12Object* object, LPCWSTR name, UINT index)
{ object->SetName((std::wstring(name) + std::to_wstring(index)).c_str()); }
#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x,n) SetNameIndexed((x)[n].Get(), L#x,n)
inline UINT CalculateConstantBufferByteSize(UINT size) { return (size + 255) & ~255u; }

class Win32Application
{
public:
    static HWND GetHwnd()
    {
        if (!SampleRuntime::window) throw SampleRuntime::HrError(FAILED(SampleRuntime::windowResult) ? SampleRuntime::windowResult : E_HANDLE, "CoreWindow HWND interop");
        return SampleRuntime::window;
    }
};

class SampleBase
{
public:
    SampleBase(UINT width, UINT height, std::wstring) : m_width(width), m_height(height),
        m_aspectRatio(float(width) / height) {}
    virtual ~SampleBase() {}
    virtual void OnInit() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnRender() = 0;
    virtual void OnDestroy() = 0;
    virtual void OnKeyUp(UINT8) {}
    virtual void OnMouseWheel(WORD) {}
    virtual void OnMouseWheel(WPARAM) {}
    virtual void SetViewPort(UINT, UINT) {}
    virtual void OnSleep() {}
    virtual IDXGISwapChain3* GetSwapChain() = 0;
    virtual ID3D12CommandQueue* GetQueue() = 0;
    virtual ID3D12Device* GetDevice() = 0;
    void SetFrameTime(float value) { m_lastFrameTimeMS = value; }
    bool m_useAsyncFlip = false, m_fullScreen = false, m_switchFullScreen = false;
    bool m_switchResolution = false, m_showOnlyInterpolation = false, m_tagInterpolatedFrames = true;
    bool m_maximized = false, m_borderless = false, m_topmost = false;
    bool m_initializeFromApplicationSwapChain = false, m_enableExternalDescriptorHeap = false;
    bool m_useWaitableSwapChain = false, m_latencyReductionEnabled = true;
    float m_frameMS = 0, m_minimumFrameTimeMS = 0, m_lastFrameTimeMS = 0;
    uint32_t m_maxInterpolatedFrames = 1;
    int m_maxFrames = INT_MAX;
protected:
    UINT m_width, m_height;
    float m_aspectRatio;
    bool m_useWarpDevice = false, m_useDebugDevice = false;
    INT m_hardwareAdapterId = -1;
    UINT m_frame_count_limit = 0;
    std::wstring GetAssetFullPath(LPCWSTR name)
    {
        return std::wstring(Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data()) + L"\\" + name;
    }
    void SetCustomWindowText(LPCWSTR) {}
    void GetHardwareAdapter(IDXGIFactory1* factory, IDXGIAdapter1** adapter, bool = false)
    {
        if (!factory || !adapter)
            throw SampleRuntime::HrError(E_POINTER, "GetHardwareAdapter: null factory/output");
        *adapter = nullptr;
        HRESULT lastDeviceError = DXGI_ERROR_NOT_FOUND;
        for (UINT index = 0; ; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            Teste_HW::Samples::SampleJournal::SetOperation("DXGI EnumAdapters1");
            HRESULT hr = factory->EnumAdapters1(index, &candidate);
            wchar_t line[768] = {};
            swprintf_s(line, L"EnumAdapters1 Index=%u HRESULT=0x%08X Adapter=%p", index, static_cast<unsigned>(hr), candidate.Get());
            Teste_HW::Samples::SampleJournal::Write(line);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            ThrowIfFailed(hr);
            if (!candidate) throw SampleRuntime::HrError(E_POINTER, "EnumAdapters1 succeeded without an adapter");
            DXGI_ADAPTER_DESC1 desc = {};
            Teste_HW::Samples::SampleJournal::SetOperation("DXGI GetDesc1");
            hr = candidate->GetDesc1(&desc);
            swprintf_s(line, L"GetDesc1 Index=%u HRESULT=0x%08X", index, static_cast<unsigned>(hr));
            Teste_HW::Samples::SampleJournal::Write(line);
            ThrowIfFailed(hr);
            swprintf_s(line, L"Adapter Index=%u Description=%s Vendor=0x%04X Device=0x%04X Flags=0x%X", index, desc.Description, desc.VendorId, desc.DeviceId, desc.Flags);
            Teste_HW::Samples::SampleJournal::Write(line);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            // Exercise actual device creation, not the desktop sample's null-output validation path.
            Microsoft::WRL::ComPtr<ID3D12Device> probeDevice;
            Teste_HW::Samples::SampleJournal::SetOperation("D3D12CreateDevice adapter probe");
            hr = D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&probeDevice));
            swprintf_s(line, L"D3D12CreateDevice Index=%u FeatureLevel=11_0 HRESULT=0x%08X Device=%p", index, static_cast<unsigned>(hr), probeDevice.Get());
            Teste_HW::Samples::SampleJournal::Write(line);
            lastDeviceError = FAILED(hr) ? hr : E_POINTER;
            if (SUCCEEDED(hr) && probeDevice)
            {
                *adapter = candidate.Detach();
                return;
            }
        }
        throw SampleRuntime::HrError(lastDeviceError, "GetHardwareAdapter: no usable hardware adapter; see enumeration/device HRESULTs in trace");
    }
};
