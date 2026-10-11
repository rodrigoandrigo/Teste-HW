#include "pch.h"
#include "IntelLatencyFgDiagnostics.h"
#include "../../XeSS_SDK_3_0_2/inc/xell/xell_d3d12.h"
#include "../../XeSS_SDK_3_0_2/inc/xess_fg/xefg_swapchain_d3d12.h"
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <mutex>

namespace
{
using namespace Teste_HW::Diagnostics;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
std::mutex probeMutex;

const wchar_t* CodeName(int value)
{
    switch (value)
    {
    case 0: return L"SUCCESS";
    case 2: return L"WARNING_OLD_DRIVER";
    case 3: return L"WARNING_TOO_FEW_FRAMES";
    case 4: return L"WARNING_FRAMES_ID_MISMATCH";
    case 5: return L"WARNING_MISSING_PRESENT_STATUS";
    case 6: return L"WARNING_RESOURCE_SIZES_MISMATCH";
    case -1: return L"ERROR_UNSUPPORTED_DEVICE";
    case -2: return L"ERROR_UNSUPPORTED_DRIVER";
    case -3: return L"ERROR_UNINITIALIZED";
    case -4: return L"ERROR_INVALID_ARGUMENT";
    case -5: return L"ERROR_DEVICE_OUT_OF_MEMORY";
    case -6: return L"ERROR_DEVICE";
    case -7: return L"ERROR_NOT_IMPLEMENTED";
    case -8: return L"ERROR_INVALID_CONTEXT";
    case -9: return L"ERROR_OPERATION_IN_PROGRESS";
    case -10: return L"ERROR_UNSUPPORTED";
    case -11: return L"ERROR_CANT_LOAD_LIBRARY";
    case -12: return L"ERROR_MISMATCH_INPUT_RESOURCES";
    case -13: return L"ERROR_INCORRECT_OUTPUT_RESOURCES";
    case -14: return L"ERROR_INCORRECT_INPUT_RESOURCES";
    case -15: return L"ERROR_LATENCY_REDUCTION_UNSUPPORTED";
    case -16: return L"ERROR_LATENCY_REDUCTION_FUNCTION_MISSING";
    case -17: return L"ERROR_HRESULT_FAILURE";
    case -18: return L"ERROR_DXGI_INVALID_CALL";
    case -19: return L"ERROR_POINTER_STILL_IN_USE";
    case -20: return L"ERROR_INVALID_DESCRIPTOR_HEAP";
    case -21: return L"ERROR_WRONG_CALL_ORDER";
    case -1000: return L"ERROR_UNKNOWN";
    default: return L"UNRECOGNIZED";
    }
}

template<class V> std::wstring Version(const V& value)
{
    return std::to_wstring(value.major) + L"." + std::to_wstring(value.minor) +
        L"." + std::to_wstring(value.patch);
}

struct Probe
{
    std::vector<DiagnosticResult> results;
    const wchar_t* category = L"Graphics / Intel XeLL";
    std::vector<HMODULE> modules;
    ComPtr<ID3D12Device> device;
    xell_context_handle_t ll = nullptr;
    xefg_swapchain_handle_t fg = nullptr;
    decltype(&xellDestroyContext) destroyLl = nullptr;
    decltype(&xefgSwapChainDestroy) destroyFg = nullptr;
    bool llConfigured = false;
    bool pipelinesBuilt = false;
    Clock::time_point started = Clock::now();

    DiagnosticResult& Add(const wchar_t* name, DiagnosticStatus status,
        const std::wstring& details = L"")
    {
        DiagnosticResult result;
        result.category = category;
        result.name = name;
        result.status = status;
        result.summary = status == DiagnosticStatus::Pass ? L"A operação terminou com sucesso." :
            (status == DiagnosticStatus::Skipped ? L"Etapa não executada; consulte os detalhes." :
                L"A operação não comprovou suporte funcional; consulte os detalhes.");
        result.details = details;
        results.push_back(std::move(result));
        return results.back();
    }

    template<class F> bool Call(const wchar_t* name, F function, const std::wstring& details = L"")
    {
        auto begin = Clock::now();
        const int code = static_cast<int>(function());
        auto& result = Add(name, code == 0 ? DiagnosticStatus::Pass :
            (code > 0 ? DiagnosticStatus::Inconclusive : DiagnosticStatus::Blocked),
            L"SDKResult=" + std::to_wstring(code) + L" (" + CodeName(code) + L"); " + details);
        result.durationMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
        return code == 0;
    }

    bool Hr(const wchar_t* name, HRESULT code)
    {
        auto& result = Add(name, SUCCEEDED(code) ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked);
        result.hasHresult = true;
        result.hresult = code;
        return SUCCEEDED(code);
    }

    HMODULE Load(const wchar_t* name, bool packaged)
    {
        SetLastError(ERROR_SUCCESS);
        HMODULE module = packaged ? LoadPackagedLibrary(name, 0) :
            LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const DWORD error = module ? ERROR_SUCCESS : GetLastError();
        auto& result = Add(name, module ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
            packaged ? L"Loader=LoadPackagedLibrary." : L"Loader=LoadLibraryExW(SYSTEM32).");
        result.hasWin32Error = true;
        result.win32Error = error;
        if (module) modules.push_back(module);
        return module;
    }

    template<class T> T Export(HMODULE module, const char* name)
    {
        SetLastError(ERROR_SUCCESS);
        FARPROC address = GetProcAddress(module, name);
        const DWORD error = address ? ERROR_SUCCESS : GetLastError();
        std::wstring label(name, name + std::strlen(name));
        auto& result = Add((L"Export / " + label).c_str(), address ? DiagnosticStatus::Pass : DiagnosticStatus::Blocked,
            L"Symbol=" + label + L"; FunctionExecutionTested=false.");
        result.hasWin32Error = true;
        result.win32Error = error;
        return reinterpret_cast<T>(address);
    }

    void Finish(bool frameGeneration)
    {
        bool released = true;
        if (fg)
        {
            category = L"Graphics / Intel XeSS Frame Generation";
            if (destroyFg && Call(L"xefgSwapChainDestroy", [&] { return destroyFg(fg); })) fg = nullptr;
            else released = false;
        }
        // The XeLL context must outlive the FG context.
        if (ll && !fg)
        {
            category = L"Graphics / Intel XeLL";
            if (destroyLl && Call(L"xellDestroyContext", [&] { return destroyLl(ll); })) ll = nullptr;
            else released = false;
        }
        if (released)
        {
            device.Reset();
            for (auto it = modules.rbegin(); it != modules.rend(); ++it) FreeLibrary(*it);
        }
        else
        {
            device.Detach();
            Add(L"Retain dependencies after teardown failure", DiagnosticStatus::Inconclusive,
                L"Runtime modules and device retained because a context may still own resources.");
        }
        category = frameGeneration ? L"Graphics / Intel XeSS Frame Generation" : L"Graphics / Intel XeLL";
        // A probe without presentation cannot demonstrate latency reduction or interpolation.
        DiagnosticStatus aggregate = DiagnosticStatus::Inconclusive;
        for (const auto& result : results)
        {
            if (result.status == DiagnosticStatus::Fail) { aggregate = DiagnosticStatus::Fail; break; }
            if (result.status == DiagnosticStatus::Blocked) aggregate = DiagnosticStatus::Blocked;
        }
        auto& result = Add(frameGeneration ? L"XeSS-FG suite completion" : L"XeLL suite completion", aggregate,
            L"SleepModeRoundTrip=" + std::wstring(llConfigured ? L"true" : L"false") +
            L"; PipelinesBuilt=" + (pipelinesBuilt ? L"true" : L"false") +
            L"; ProxySwapChainInitialized=false; FrameGenerationTested=false; LatencyReductionMeasured=false.");
        result.durationMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    }
};

std::vector<DiagnosticResult> Run(bool frameGeneration)
{
    Probe probe;
    std::unique_lock<std::mutex> lock(probeMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        probe.Add(L"Exclusive XeLL/FG probe", DiagnosticStatus::Blocked, L"Another XeLL/FG test is running.");
        probe.Finish(frameGeneration);
        return probe.results;
    }
    auto work = [&]
    {
        HMODULE llModule = probe.Load(L"libxell.dll", true);
        HMODULE fgModule = nullptr;
        if (frameGeneration)
        {
            probe.category = L"Graphics / Intel XeSS Frame Generation";
            fgModule = probe.Load(L"libxess_fg.dll", true);
        }
        if (!llModule && !fgModule) return;
        probe.category = frameGeneration ? L"Graphics / Intel XeSS Frame Generation" : L"Graphics / Intel XeLL";
        HMODULE d3d = probe.Load(L"d3d12.dll", false);
        if (!d3d) return;
        using CreateDevice = HRESULT (WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
        auto createDevice = probe.Export<CreateDevice>(d3d, "D3D12CreateDevice");
        if (!createDevice || !probe.Hr(L"D3D12CreateDevice", createDevice(nullptr,
            D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&probe.device)))) return;

        if (llModule)
        {
            probe.category = L"Graphics / Intel XeLL";
            auto version = probe.Export<decltype(&xellGetVersion)>(llModule, "xellGetVersion");
            auto create = probe.Export<decltype(&xellD3D12CreateContext)>(llModule, "xellD3D12CreateContext");
            probe.destroyLl = probe.Export<decltype(&xellDestroyContext)>(llModule, "xellDestroyContext");
            auto setMode = probe.Export<decltype(&xellSetSleepMode)>(llModule, "xellSetSleepMode");
            auto getMode = probe.Export<decltype(&xellGetSleepMode)>(llModule, "xellGetSleepMode");
            auto sleep = probe.Export<decltype(&xellSleep)>(llModule, "xellSleep");
            auto marker = probe.Export<decltype(&xellAddMarkerData)>(llModule, "xellAddMarkerData");
            auto reports = probe.Export<decltype(&xellGetFramesReports)>(llModule, "xellGetFramesReports");
            if (version)
            {
                xell_version_t value = {};
                if (probe.Call(L"xellGetVersion", [&] { return version(&value); }))
                    probe.results.back().details += L"Version=" + Version(value);
            }
            if (create && probe.destroyLl && probe.Call(L"xellD3D12CreateContext", [&] {
                return create(probe.device.Get(), &probe.ll); }) && probe.ll)
            {
                if (setMode && getMode)
                {
                    xell_sleep_params_t requested = {};
                    requested.bLowLatencyMode = 1;
                    if (probe.Call(L"xellSetSleepMode(enabled)", [&] { return setMode(probe.ll, &requested); },
                        L"MinimumIntervalUs=0; LowLatencyMode=1; this is configuration, not measured reduction."))
                    {
                        xell_sleep_params_t observed = {};
                        if (probe.Call(L"xellGetSleepMode", [&] { return getMode(probe.ll, &observed); }))
                        {
                            probe.llConfigured = observed.bLowLatencyMode == 1 && observed.minimumIntervalUs == 0 &&
                                observed.bLowLatencyBoost == 0;
                            probe.Add(L"XeLL sleep mode round-trip", probe.llConfigured ? DiagnosticStatus::Pass : DiagnosticStatus::Fail,
                                L"ObservedLowLatencyMode=" + std::to_wstring(observed.bLowLatencyMode) +
                                L"; ObservedMinimumIntervalUs=" + std::to_wstring(observed.minimumIntervalUs));
                        }
                    }
                    requested.bLowLatencyMode = 0;
                    if (probe.Call(L"xellSetSleepMode(pass-through)", [&] { return setMode(probe.ll, &requested); }))
                    {
                        if (sleep) probe.Call(L"xellSleep(pass-through)", [&] { return sleep(probe.ll, 1); }, L"FrameId=1; no GPU presentation.");
                        if (marker)
                        {
                            probe.Call(L"xellAddMarkerData(SIMULATION_START)", [&] { return marker(probe.ll, 1, XELL_SIMULATION_START); }, L"SyntheticFrame=true.");
                            probe.Call(L"xellAddMarkerData(SIMULATION_END)", [&] { return marker(probe.ll, 1, XELL_SIMULATION_END); }, L"SyntheticFrame=true.");
                        }
                        if (reports)
                        {
                            xell_frame_report_t values[64] = {};
                            probe.Call(L"xellGetFramesReports", [&] { return reports(probe.ll, values); },
                                L"Capacity=64; no rendered/presented frames; not a latency measurement.");
                        }
                    }
                }
            }
        }
        if (!frameGeneration || !fgModule) return;
        probe.category = L"Graphics / Intel XeSS Frame Generation";
        auto version = probe.Export<decltype(&xefgSwapChainGetVersion)>(fgModule, "xefgSwapChainGetVersion");
        auto create = probe.Export<decltype(&xefgSwapChainD3D12CreateContext)>(fgModule, "xefgSwapChainD3D12CreateContext");
        probe.destroyFg = probe.Export<decltype(&xefgSwapChainDestroy)>(fgModule, "xefgSwapChainDestroy");
        auto properties = probe.Export<decltype(&xefgSwapChainGetProperties)>(fgModule, "xefgSwapChainGetProperties");
        auto memory = probe.Export<decltype(&xefgSwapChainD3D12GetProperties)>(fgModule, "xefgSwapChainD3D12GetProperties");
        auto bind = probe.Export<decltype(&xefgSwapChainSetLatencyReduction)>(fgModule, "xefgSwapChainSetLatencyReduction");
        auto build = probe.Export<decltype(&xefgSwapChainD3D12BuildPipelines)>(fgModule, "xefgSwapChainD3D12BuildPipelines");
        auto buildStatus = probe.Export<decltype(&xefgSwapChainGetPipelineBuildStatus)>(fgModule, "xefgSwapChainGetPipelineBuildStatus");
        probe.Export<decltype(&xefgSwapChainD3D12InitFromSwapChain)>(fgModule, "xefgSwapChainD3D12InitFromSwapChain");
        probe.Export<decltype(&xefgSwapChainD3D12GetInitializationParameters)>(fgModule, "xefgSwapChainD3D12GetInitializationParameters");
        if (version)
        {
            xefg_swapchain_version_t value = {};
            if (probe.Call(L"xefgSwapChainGetVersion", [&] { return version(&value); }))
                probe.results.back().details += L"Version=" + Version(value);
        }
        if (!create || !probe.destroyFg || !probe.Call(L"xefgSwapChainD3D12CreateContext", [&] {
            return create(probe.device.Get(), &probe.fg); }) || !probe.fg) return;
        if (properties)
        {
            xefg_swapchain_properties_t value = {};
            if (probe.Call(L"xefgSwapChainGetProperties(pre-init)", [&] { return properties(probe.fg, &value); }))
                probe.results.back().details += L"MaxSupportedInterpolations=" + std::to_wstring(value.maxSupportedInterpolations);
        }
        if (bind && probe.ll)
            probe.Call(L"xefgSwapChainSetLatencyReduction", [&] { return bind(probe.fg, probe.ll); });
        else
            probe.Add(L"xefgSwapChainSetLatencyReduction", DiagnosticStatus::Skipped, L"XeLL context or binding export unavailable.");
        if (memory)
        {
            xefg_swapchain_d3d12_init_params_t params = {};
            params.maxInterpolatedFrames = 1;
            params.uiMode = XEFG_SWAPCHAIN_UI_MODE_NONE;
            xefg_swapchain_properties_t value = {};
            if (probe.Call(L"xefgSwapChainD3D12GetProperties(1920x1080)", [&] {
                return memory(probe.fg, &params, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM, &value); }))
                probe.results.back().details += L"Descriptors=" + std::to_wstring(value.requiredDescriptorCount) +
                    L"; TempBufferHeapSize=" + std::to_wstring(value.tempBufferHeapSize) +
                    L"; TempTextureHeapSize=" + std::to_wstring(value.tempTextureHeapSize) +
                    L"; ConstantBufferSize=" + std::to_wstring(value.constantBufferSize);
        }
        if (build)
            probe.pipelinesBuilt = probe.Call(L"xefgSwapChainD3D12BuildPipelines", [&] {
                return build(probe.fg, nullptr, 1, XEFG_SWAPCHAIN_INIT_FLAG_NONE); }, L"Blocking=true; InitFlags=NONE.");
        if (buildStatus && probe.pipelinesBuilt)
            probe.Call(L"xefgSwapChainGetPipelineBuildStatus", [&] { return buildStatus(probe.fg); });
        probe.Add(L"XeSS-FG proxy initialization and frame execution", DiagnosticStatus::Skipped,
            L"SDK initialization recreates a window swap chain. The XAML-only app supplies no native window swap chain. "
            L"No null/fabricated HWND was passed; no frames were generated or presented.");
    };
    try { work(); }
    catch (Platform::Exception^ exception)
    {
        probe.Hr(L"Unexpected WinRT failure", exception->HResult);
    }
    catch (...)
    {
        probe.Add(L"Unexpected native failure", DiagnosticStatus::Fail);
    }
    probe.Finish(frameGeneration);
    return probe.results;
}
}

namespace Teste_HW { namespace Diagnostics {
std::vector<DiagnosticResult> IntelLatencyFgDiagnostics::RunLowLatency() { return Run(false); }
std::vector<DiagnosticResult> IntelLatencyFgDiagnostics::RunFrameGeneration() { return Run(true); }
} }
