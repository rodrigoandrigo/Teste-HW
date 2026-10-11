#include "SampleHost.h"
#include "SampleAnimations.h"
#include "LL/basic_sample.h"
#include "FG/basic_sample.h"
#include "SR/basic_sample_d3d12.h"
#include "Fsr3Sample.h"
#include "Fsr3UpscaleSample.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include "../Diagnostics/IntelLatencyFgDiagnostics.h"

namespace SampleRuntime {
thread_local HWND window = nullptr;
thread_local HRESULT windowResult = E_NOINTERFACE;
thread_local std::map<std::wstring, HMODULE> modules;
thread_local uint64_t sdkCalls = 0, generatedFrames = 0, executeCalls = 0, sleepCalls = 0, failedGeneration = 0;
thread_local bool warned = false;
thread_local uint64_t presentedFrames = 0;
thread_local uint64_t validLatencyReports = 0, lastPresentDurationNs = 0;
void ObserveLatencyReports(const xell_frame_report_t* reports)
{
    uint64_t valid = 0; uint32_t newest = 0;
    for (size_t i = 0; i < 64; ++i) {
        const auto& r = reports[i];
        if (r.m_sim_start_ts && r.m_sim_end_ts >= r.m_sim_start_ts &&
            r.m_render_submit_start_ts && r.m_render_submit_end_ts >= r.m_render_submit_start_ts &&
            r.m_present_start_ts && r.m_present_end_ts >= r.m_present_start_ts) {
            ++valid;
            if (r.m_frame_id >= newest) { newest = r.m_frame_id; lastPresentDurationNs = r.m_present_end_ts - r.m_present_start_ts; }
        }
    }
    validLatencyReports = (std::max)(validLatencyReports, valid);
}
thread_local uint64_t readbacks = 0, firstImageHash = 0, lastImageHash = 0, nonUniformPixels = 0;
thread_local bool imageChanged = false;
thread_local std::chrono::steady_clock::time_point renderStart;
std::atomic<bool> cancelled(false);
thread_local bool traceSdk = true;
std::atomic<bool> unsafeRuntime(false);
HMODULE LoadRuntime(const wchar_t* dll)
{
    // Process-lifetime ownership prevents unloading SDK code while driver/SDK
    // callbacks may still exist. Contexts and graphics objects are still torn down.
    static std::mutex cacheLock;
    static std::map<std::wstring, HMODULE> cache;
    std::lock_guard<std::mutex> lock(cacheLock);
    auto& module = cache[dll];
    if (!module) module = LoadPackagedLibrary(dll, 0);
    return module;
}
void Checkpoint(const char* stage)
{
    Teste_HW::Samples::SampleJournal::SetOperation(stage);
    if (traceSdk) Teste_HW::Samples::SampleJournal::WriteUtf8(stage);
}
void TraceApi(const char* api, bool begin, int result)
{
    char line[256] = {};
    sprintf_s(line, "API %s %s SDKResult=%d", begin ? "BEGIN" : "END", api, result);
    Teste_HW::Samples::SampleJournal::WriteUtf8(line);
}
class NativeFault : public std::exception {
public:
    SampleNative::Fault fault;
    explicit NativeFault(const SampleNative::Fault& value) : fault(value) {}
    const char* what() const noexcept override { return "Native hardware exception; restart the application before another sample."; }
};
struct Step { SampleBase* sample; int operation; };
static void ExecuteStep(void* context)
{
    auto* step = static_cast<Step*>(context);
    switch (step->operation) {
    case 0: step->sample->OnInit(); break;
    case 1: step->sample->OnSleep(); break;
    case 2: step->sample->OnUpdate(); break;
    case 3: step->sample->OnRender(); break;
    case 4: step->sample->OnDestroy(); break;
    case 5: if (step->sample->GetDevice()) ThrowIfFailed(step->sample->GetDevice()->GetDeviceRemovedReason()); break;
    }
}
static void RunStep(SampleBase* sample, int operation, const char* label)
{
    Checkpoint(label);
    Step step = { sample, operation }; SampleNative::Fault fault;
    if (!SampleNative::Invoke(ExecuteStep, &step, &fault)) {
        Teste_HW::Samples::SampleJournal::WriteFault(fault);
        unsafeRuntime = true; throw NativeFault(fault);
    }
}
HRESULT Present(IDXGISwapChain* chain, UINT syncInterval, UINT flags)
{
    Checkpoint("IDXGISwapChain::Present BEGIN");
    HRESULT hr = chain->Present(syncInterval, flags);
    if (hr != S_OK) throw HrError(hr, "IDXGISwapChain::Present (including occlusion/status)");
    ++presentedFrames; return hr;
}
void Wait(HANDLE event)
{
    Checkpoint("GPU/event wait BEGIN (timeout=5000 ms)");
    DWORD result = WaitForSingleObjectEx(event, 5000, FALSE);
    if (result != WAIT_OBJECT_0) {
        unsafeRuntime = true;
        throw HrError(HRESULT_FROM_WIN32(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError()));
    }
}

void ObserveFrame(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* output)
{
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - renderStart).count();
    if (readbacks >= 3 || (readbacks == 1 && elapsed < 1) || (readbacks == 2 && elapsed < 9)) return;
    Teste_HW::Samples::SampleJournal::Write(L"GPU output readback BEGIN");
    // The application output is in PRESENT state here. Read it on the same
    // queue before Present; this verifies pixels, not scanout or image quality.
    struct Resources {
        Microsoft::WRL::ComPtr<ID3D12Resource> source, buffer;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    };
    std::unique_ptr<Resources> r(new Resources());
    r->source = output; r->device = device; r->queue = queue;
    auto desc = output->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {}; UINT64 bytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&r->buffer)));
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&r->allocator)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r->allocator.Get(), nullptr, IID_PPV_ARGS(&r->commands)));
    auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    r->commands->ResourceBarrier(1, &toCopy);
    auto dest = CD3DX12_TEXTURE_COPY_LOCATION(r->buffer.Get(), footprint);
    auto src = CD3DX12_TEXTURE_COPY_LOCATION(output, 0);
    r->commands->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    r->commands->ResourceBarrier(1, &toPresent);
    ThrowIfFailed(r->commands->Close());
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r->fence)));
    HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!event) throw HrError(HRESULT_FROM_WIN32(GetLastError()), "readback fence event");
    ID3D12CommandList* lists[] = { r->commands.Get() };
    queue->ExecuteCommandLists(1, lists);
    try {
        ThrowIfFailed(queue->Signal(r->fence.Get(), 1));
        ThrowIfFailed(r->fence->SetEventOnCompletion(1, event)); Wait(event);
        ThrowIfFailed(device->GetDeviceRemovedReason());
    } catch (...) {
        if (!unsafeRuntime) CloseHandle(event);
        else Teste_HW::Samples::SampleJournal::Write(L"Readback timeout: pending fence event retained until app restart.");
        r.release(); cancelled = true; throw; // retain submitted resources after timeout/device failure
    }
    CloseHandle(event);
    void* data = nullptr; D3D12_RANGE range = { 0, SIZE_T(bytes) };
    ThrowIfFailed(r->buffer->Map(0, &range, &data));
    const BYTE* start = static_cast<const BYTE*>(data) + footprint.Offset;
    uint64_t hash = 14695981039346656037ULL, different = 0;
    for (UINT y = 0; y < desc.Height; ++y) {
        const BYTE* row = start + size_t(y) * footprint.Footprint.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            const BYTE* pixel = row + size_t(x) * 4;
            if (pixel[0] != start[0] || pixel[1] != start[1] || pixel[2] != start[2]) ++different;
            for (UINT channel = 0; channel < 3; ++channel) { hash ^= pixel[channel]; hash *= 1099511628211ULL; }
        }
    }
    D3D12_RANGE written = {}; r->buffer->Unmap(0, &written);
    if (!readbacks) firstImageHash = hash;
    else if (hash != firstImageHash) imageChanged = true;
    lastImageHash = hash; nonUniformPixels = (std::max)(nonUniformPixels, different); ++readbacks;
    Teste_HW::Samples::SampleJournal::Write(L"GPU output readback END Hash=" + std::to_wstring(hash));
}
HRESULT LoadShader(LPCWSTR path, const D3D_SHADER_MACRO*, ID3DInclude*, const char* entry,
    const char*, UINT, UINT, ID3DBlob** blob, ID3DBlob** errors)
{
    if (errors) *errors = nullptr;
    *blob = nullptr;
    std::wstring filename(path); filename += L".";
    for (const char* p = entry; *p; ++p) filename += wchar_t(*p);
    filename += L".cso";
    std::replace(filename.begin(), filename.end(), L'/', L'\\');
    HANDLE file = CreateFile2FromAppW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size = {}; HRESULT hr = S_OK;
    if (!GetFileSizeEx(file, &size)) hr = HRESULT_FROM_WIN32(GetLastError());
    else if (size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) hr = E_INVALIDARG;
    else hr = D3DCreateBlob(static_cast<SIZE_T>(size.QuadPart), blob);
    if (SUCCEEDED(hr)) {
        DWORD bytes = 0;
        if (!ReadFile(file, (*blob)->GetBufferPointer(), DWORD(size.QuadPart), &bytes, nullptr)) hr = HRESULT_FROM_WIN32(GetLastError());
        else if (bytes != size.QuadPart) hr = HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
    }
    CloseHandle(file);
    if (FAILED(hr) && *blob) { (*blob)->Release(); *blob = nullptr; }
    return hr;
}
}

namespace Teste_HW { namespace Samples {
using namespace Diagnostics;
using Clock = std::chrono::steady_clock;
void SampleAnimations::Cancel() { SampleRuntime::cancelled = true; }
void SampleAnimations::Begin() { SampleRuntime::cancelled = false; }
bool SampleAnimations::IsCancelled() { return SampleRuntime::cancelled; }
void SampleAnimations::BlockAfterNativeFault() { SampleRuntime::unsafeRuntime = true; }

struct PanelAttachment {
    IUnknown* panel;
    IDXGISwapChain* chain;
    HRESULT result = E_PENDING;
    SampleNative::Fault fault;
    bool safe = true;
    float width = 0, height = 0;
};
static void ScalePanelNative(void* context)
{
    auto* state = static_cast<PanelAttachment*>(context);
    Microsoft::WRL::ComPtr<IDXGISwapChain2> chain;
    state->result = state->chain->QueryInterface(IID_PPV_ARGS(&chain));
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (SUCCEEDED(state->result)) state->result = chain->GetDesc1(&desc);
    if (SUCCEEDED(state->result)) {
        if (!desc.Width || !desc.Height || state->width <= 0 || state->height <= 0) {
            state->result = E_INVALIDARG; return;
        }
        // Matrix units are XAML DIPs. CompositionScale applies the final
        // physical-pixel scaling; do not multiply it a second time here.
        DXGI_MATRIX_3X2_F matrix = { state->width / desc.Width, 0, 0,
            state->height / desc.Height, 0, 0 };
        state->result = chain->SetMatrixTransform(&matrix);
    }
}
// These bindings are accessed exclusively by the XAML dispatcher.
static Microsoft::WRL::ComPtr<IDXGISwapChain> presentationChain;
static Windows::Foundation::EventRegistrationToken panelSizeToken, panelScaleToken;
static bool panelEventsBound = false;
static void UpdatePanelPresentation(Windows::UI::Xaml::Controls::SwapChainPanel^ panel)
{
    if (!presentationChain || panel->ActualWidth <= 0 || panel->ActualHeight <= 0) return;
    PanelAttachment state = {};
    state.chain = presentationChain.Get();
    state.width = float(panel->ActualWidth); state.height = float(panel->ActualHeight);
    SampleJournal::SetOperation("IDXGISwapChain2::SetMatrixTransform");
    if (!SampleNative::Invoke(ScalePanelNative, &state, &state.fault)) {
        SampleJournal::WriteFault(state.fault); SampleRuntime::unsafeRuntime = true;
        SampleAnimations::Cancel(); return;
    }
    SampleJournal::Write(L"Presentation fit: PanelDips=" + std::to_wstring(state.width) + L"x" +
        std::to_wstring(state.height) + L"; CompositionScale=" + std::to_wstring(panel->CompositionScaleX) +
        L"x" + std::to_wstring(panel->CompositionScaleY) + L"; HRESULT=" + std::to_wstring(state.result));
    if (FAILED(state.result)) SampleAnimations::Cancel();
}
static void AttachNative(void* context)
{
    auto* state = static_cast<PanelAttachment*>(context);
    if (state->chain) {
        ScalePanelNative(context);
        if (FAILED(state->result)) return;
    }
    ISwapChainPanelNative* native = nullptr;
    state->result = state->panel->QueryInterface(__uuidof(ISwapChainPanelNative), reinterpret_cast<void**>(&native));
    if (SUCCEEDED(state->result)) {
        state->result = native->SetSwapChain(state->chain);
        native->Release();
    }
}
// SetSwapChain must execute on the XAML dispatcher, never the render worker.
static void Attach(Windows::UI::Xaml::Controls::SwapChainPanel^ panel, IDXGISwapChain* chain)
{
    Microsoft::WRL::ComPtr<IDXGISwapChain> retained = chain;
    auto state = std::make_shared<PanelAttachment>();
    state->chain = chain;
    SampleJournal::Write(chain ? L"XAML attachment BEGIN" : L"XAML detachment BEGIN");
    concurrency::create_task(panel->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
        ref new Windows::UI::Core::DispatchedHandler([panel, retained, state]() {
            if (panelEventsBound) {
                panel->SizeChanged -= panelSizeToken;
                panel->CompositionScaleChanged -= panelScaleToken;
                panelEventsBound = false;
            }
            presentationChain.Reset();
            panel->UpdateLayout();
            state->width = float(panel->ActualWidth); state->height = float(panel->ActualHeight);
            state->panel = reinterpret_cast<IUnknown*>(panel);
            SampleJournal::SetOperation("ISwapChainPanelNative::SetSwapChain");
            state->safe = SampleNative::Invoke(AttachNative, state.get(), &state->fault);
            if (state->safe && SUCCEEDED(state->result) && retained) {
                presentationChain = retained;
                panelSizeToken = panel->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler(
                    [panel](Platform::Object^, Windows::UI::Xaml::SizeChangedEventArgs^) { UpdatePanelPresentation(panel); });
                panelScaleToken = panel->CompositionScaleChanged += ref new Windows::Foundation::TypedEventHandler<
                    Windows::UI::Xaml::Controls::SwapChainPanel^, Platform::Object^>(
                    [panel](Windows::UI::Xaml::Controls::SwapChainPanel^, Platform::Object^) { UpdatePanelPresentation(panel); });
                panelEventsBound = true;
                UpdatePanelPresentation(panel);
            }
        }))).get();
    if (!state->safe) {
        SampleJournal::WriteFault(state->fault); SampleRuntime::unsafeRuntime = true;
        throw SampleRuntime::NativeFault(state->fault);
    }
    wchar_t line[96] = {}; swprintf_s(line, L"XAML attachment/detachment END HRESULT=0x%08X", unsigned(state->result));
    SampleJournal::Write(line);
    if (FAILED(state->result)) throw SampleRuntime::HrError(state->result, "ISwapChainPanelNative::SetSwapChain/QueryInterface");
}

std::vector<DiagnosticResult> SampleAnimations::Run(Animation animation,
    Windows::UI::Xaml::Controls::SwapChainPanel^ panel, HWND hwnd, HRESULT windowResult,
    const std::wstring& tracePath, int fsrPreset)
{
    std::vector<DiagnosticResult> results;
    const bool presetSequence = animation == Animation::Fsr3Upscaling || animation == Animation::SuperResolution;
    if (presetSequence && fsrPreset < 0) {
        const int presetCount = animation == Animation::SuperResolution ? 4 : 3;
        for (int preset = 0; preset < presetCount; ++preset) {
            auto entries = Run(animation, panel, hwnd, windowResult, tracePath, preset);
            bool passed = !entries.empty() && entries.back().status == DiagnosticStatus::Pass;
            for (auto& entry : entries) results.push_back(std::move(entry));
            if (!passed || SampleRuntime::cancelled || SampleRuntime::unsafeRuntime) break;
        }
        SampleJournal::Write(DiagnosticReportSerializer::ToText(results));
        const bool complete = results.size() == size_t(presetCount) && results.back().status == DiagnosticStatus::Pass;
        SampleJournal::Close(complete ? L"PASS" : L"INCOMPLETE");
        return results;
    }
    SampleRuntime::window = hwnd;
    SampleRuntime::windowResult = windowResult;
    SampleRuntime::sdkCalls = SampleRuntime::generatedFrames = SampleRuntime::executeCalls =
        SampleRuntime::sleepCalls = SampleRuntime::failedGeneration = 0;
    SampleRuntime::warned = false;
    SampleRuntime::presentedFrames = 0;
    SampleRuntime::validLatencyReports = SampleRuntime::lastPresentDurationNs = 0;
    DiagnosticResult result;
    result.category = L"Intel SDK / Animated D3D12 samples";
    if (animation == Animation::Fsr3FrameGeneration) result.category = L"AMD FidelityFX / Animated D3D12 samples";
    if (animation == Animation::Fsr3Upscaling) result.category = L"AMD FidelityFX / Animated D3D12 samples";
    result.name = animation == Animation::FrameGeneration ? L"basic_sample_frame_generation" :
        animation == Animation::LowLatency ? L"basic_sample_low_latency" : L"basic_sample_super_resolution_dx12";
    if (animation == Animation::Fsr3FrameGeneration) {
        result.name = L"FSR3 Frame Generation 3.1.6";
        result.details = L"Presentation=generated-only composition; no FPS/scanout improvement inferred; GeneratedFrames counts successful dispatches after history warmup. ";
    }
    if (animation == Animation::Fsr3Upscaling) {
        result.name = L"FSR3 Upscaling 3.1.5 / " + std::wstring(SampleFsr3Upscale::BasicSample::PresetName(fsrPreset));
        result.details = L"Scene=SponzaNew; Renderer=UWP-native D3D12; Camera=animated PhysCamera003; UpscaleOutput=1280x720; PresetIndex=" + std::to_wstring(fsrPreset) + L"; ";
    }
    if (animation == Animation::SuperResolution) {
        result.name = L"XeSS-SR Sponza / " + std::wstring(SampleSR::BasicSampleD3D12::PresetName(fsrPreset));
        result.details = L"Scene=SponzaNew; Renderer=UWP-native D3D12; InputResolution=runtime optimal; Output=1280x720; ";
    }
    result.timestampUtc = DiagnosticReportSerializer::UtcNowIso8601();
    result.details += L"PersistentTrace=" + tracePath + L"; ";
    auto finish = [&results, animation, fsrPreset](DiagnosticResult&& entry) {
        auto status = DiagnosticReportSerializer::StatusName(entry.status);
        results.push_back(std::move(entry));
        SampleJournal::Write(DiagnosticReportSerializer::ToText(results));
        if ((animation != Animation::Fsr3Upscaling && animation != Animation::SuperResolution) || fsrPreset < 0) SampleJournal::Close(status.c_str());
    };
    std::unique_lock<std::mutex> runtimeLock(IntelSdkRuntimeMutex(), std::try_to_lock);
    if (!runtimeLock.owns_lock()) {
        result.status = DiagnosticStatus::Skipped; result.summary = L"Another XeLL/XeSS-FG probe is running.";
        finish(std::move(result)); return results;
    }
    SampleRuntime::readbacks = SampleRuntime::firstImageHash = SampleRuntime::lastImageHash = SampleRuntime::nonUniformPixels = 0;
    SampleRuntime::imageChanged = false;
    SampleRuntime::traceSdk = true;
    if (SampleRuntime::unsafeRuntime) {
        result.status = DiagnosticStatus::Blocked;
        result.summary = L"Previous native fault/GPU timeout/cleanup failure retained resources; restart the app before testing another sample.";
        finish(std::move(result)); return results;
    }
    if (SampleRuntime::cancelled) {
        result.status = DiagnosticStatus::Skipped;
        result.summary = L"Skipped because the animation sequence was cancelled.";
        finish(std::move(result)); return results;
    }
    auto started = Clock::now();
    std::unique_ptr<SampleBase> sample;
    bool attached = false, initialized = false, drained = false, destroyAttempted = false;
    uint64_t frames = 0; double seconds = 0;
    std::wstring stage = L"initialization";
    try {
        if (animation == Animation::Fsr3Upscaling) sample.reset(new SampleFsr3Upscale::BasicSample(fsrPreset));
        else if (animation == Animation::Fsr3FrameGeneration) sample.reset(new SampleFsr3::BasicSample());
        else if (animation == Animation::FrameGeneration) sample.reset(new SampleFG::BasicSample(1280, 720, L"XeSS-FG"));
        else if (animation == Animation::LowLatency) sample.reset(new SampleLL::BasicSample(1280, 720, L"XeLL"));
        else sample.reset(new SampleSR::BasicSampleD3D12(1280, 720, L"XeSS-SR Sponza", fsrPreset));
        SampleRuntime::RunStep(sample.get(), 0, "OnInit BEGIN"); initialized = true;
        SampleJournal::Write(L"OnInit END");
        {
            stage = L"XAML swapchain attachment";
            IDXGISwapChain* chain = sample->GetSwapChain();
            if (!chain) throw SampleRuntime::HrError(E_POINTER, "XAML attachment: sample swapchain missing");
            Attach(panel, chain); attached = true;
        }
        stage = L"10-second rendering";
        auto animationStart = Clock::now(), previous = animationStart;
        SampleRuntime::renderStart = animationStart;
        auto heartbeat = animationStart;
        while (Clock::now() - animationStart < std::chrono::seconds(10) && !SampleRuntime::cancelled) {
            auto now = Clock::now();
            sample->SetFrameTime(std::chrono::duration<float, std::milli>(now - previous).count()); previous = now;
            SampleRuntime::traceSdk = frames == 0;
            if (now >= heartbeat) {
                SampleJournal::Write(L"RENDER PROGRESS Frames=" + std::to_wstring(frames) +
                    L"; Seconds=" + std::to_wstring(std::chrono::duration<double>(now - animationStart).count()));
                heartbeat = now + std::chrono::seconds(1);
            }
            SampleRuntime::RunStep(sample.get(), 1, "OnSleep BEGIN");
            SampleRuntime::RunStep(sample.get(), 2, "OnUpdate BEGIN");
            SampleRuntime::RunStep(sample.get(), 3, "OnRender BEGIN"); ++frames;
            seconds = std::chrono::duration<double>(Clock::now() - animationStart).count();
        }
        stage = L"GPU fence / teardown";
        SampleRuntime::traceSdk = true;
        SampleRuntime::RunStep(sample.get(), 5, "GetDeviceRemovedReason");
        if (attached) { Attach(panel, nullptr); attached = false; }
        destroyAttempted = true;
        SampleRuntime::RunStep(sample.get(), 4, "OnDestroy BEGIN"); drained = true;
        SampleJournal::Write(L"OnDestroy END");
        bool functional = frames > 1 && seconds >= 10 && !SampleRuntime::cancelled &&
            SampleRuntime::readbacks >= 2 && SampleRuntime::imageChanged && SampleRuntime::nonUniformPixels > 100;
        if (animation == Animation::FrameGeneration) functional = functional && SampleRuntime::generatedFrames > 0 && SampleRuntime::failedGeneration == 0;
        if (animation == Animation::Fsr3FrameGeneration) functional = functional && SampleRuntime::generatedFrames > 1;
        if (animation == Animation::Fsr3Upscaling) functional = functional && SampleRuntime::executeCalls >= frames;
        if (animation == Animation::SuperResolution) functional = functional && SampleRuntime::executeCalls >= frames;
        if (animation == Animation::LowLatency) functional = functional && SampleRuntime::sleepCalls >= frames && SampleRuntime::validLatencyReports > 0;
        result.status = functional ? DiagnosticStatus::Pass : DiagnosticStatus::Inconclusive;
        result.summary = functional ? L"10-second animation completed; presentation and GPU fence succeeded." :
            L"Animation did not establish the requested functionality (cancelled, incomplete, or no generated frames).";
    }
    catch (const SampleRuntime::NativeFault& error) {
        result.status = DiagnosticStatus::Fail;
        wchar_t detail[256] = {};
        swprintf_s(detail, L"SEH=0x%08lX; Address=%p; AccessKind=%llu; AccessAddress=0x%llX. ",
            error.fault.code, error.fault.address, static_cast<unsigned long long>(error.fault.accessKind),
            static_cast<unsigned long long>(error.fault.accessAddress));
        result.details += detail;
        result.summary = L"Native exception during " + stage + L"; resources retained and further samples blocked until restart.";
    }
    catch (const SampleRuntime::HrError& error) {
        result.status = initialized ? DiagnosticStatus::Fail : DiagnosticStatus::Blocked;
        result.hasHresult = true; result.hresult = error.code;
        if (HRESULT_FACILITY(error.code) == FACILITY_WIN32) { result.hasWin32Error = true; result.win32Error = HRESULT_CODE(error.code); }
        std::string operation(error.what());
        result.summary = L"Windows/D3D12 failure during " + stage + L": " + std::wstring(operation.begin(), operation.end());
        if (error.code == DXGI_ERROR_DEVICE_REMOVED || error.code == DXGI_ERROR_DEVICE_RESET ||
            error.code == DXGI_ERROR_DEVICE_HUNG) SampleRuntime::unsafeRuntime = true;
    }
    catch (Platform::Exception^ error) {
        result.status = initialized ? DiagnosticStatus::Fail : DiagnosticStatus::Blocked;
        result.hasHresult = true; result.hresult = error->HResult;
        result.summary = L"UWP failure during " + stage + L": " + error->Message->Data();
    }
    catch (const std::exception& error) {
        result.status = initialized ? DiagnosticStatus::Fail : DiagnosticStatus::Blocked;
        std::string message(error.what());
        result.summary = L"SDK failure during " + stage + L": " + std::wstring(message.begin(), message.end());
    }
    catch (...) {
        result.status = DiagnosticStatus::Fail; result.summary = L"Unexpected exception during " + stage;
        SampleRuntime::unsafeRuntime = true;
    }
    SampleJournal::Write(result.summary);
    SampleRuntime::traceSdk = true;
    if (attached) { try { Attach(panel, nullptr); } catch (...) {} }
    if (sample && !drained && !destroyAttempted && !SampleRuntime::unsafeRuntime) {
        try { destroyAttempted = true; SampleRuntime::RunStep(sample.get(), 4, "Error cleanup BEGIN"); drained = true; }
        catch (const std::exception& error) {
            SampleJournal::WriteUtf8(error.what());
            result.details += L"Cleanup failed; GPU resources/runtime retained to avoid use-after-free. ";
            SampleRuntime::unsafeRuntime = true;
        }
        catch (...) { SampleJournal::Write(L"Error cleanup threw an exception."); SampleRuntime::unsafeRuntime = true; }
    }
    if (!drained && sample) {
        sample.release(); SampleRuntime::modules.clear(); SampleRuntime::cancelled = true;
        SampleRuntime::unsafeRuntime = true;
        // Keep owned module references loaded with the retained GPU/context resources.
    }
    else {
        SampleJournal::Write(L"Graphics object release BEGIN (SDK DLLs remain loaded for process lifetime)");
        sample.reset();
        SampleRuntime::modules.clear(); // aliases; module references are owned by the process cache
        SampleJournal::Write(L"Graphics object release END");
    }
    result.durationMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
    result.details += L"RequestedAnimationSeconds=10; AnimationSeconds=" + std::to_wstring(seconds) +
        L"; CompletedFrameIterations=" + std::to_wstring(frames) + L"; PresentedApplicationFrames=" + std::to_wstring(SampleRuntime::presentedFrames) + L"; SDKCalls=" + std::to_wstring(SampleRuntime::sdkCalls) +
        (animation == Animation::Fsr3Upscaling ? L"; FSR3UpscaleDispatches=" : L"; XeSSExecuteCalls=") + std::to_wstring(SampleRuntime::executeCalls) + L"; XeLLSleepCalls=" + std::to_wstring(SampleRuntime::sleepCalls) +
        L"; XeLLPeakValidReportWindow=" + std::to_wstring(SampleRuntime::validLatencyReports) + L"; XeLLLastPresentDurationNs=" + std::to_wstring(SampleRuntime::lastPresentDurationNs) +
        L"; GeneratedFrames=" + std::to_wstring(SampleRuntime::generatedFrames) + L"; FGUnsuccessfulStatusFrames=" +
        std::to_wstring(SampleRuntime::failedGeneration) + L"; SDKWarnings=" + (SampleRuntime::warned ? L"yes" : L"no") +
        L"; OutputReadbacks=" + std::to_wstring(SampleRuntime::readbacks) + L"; FirstImageHash=" + std::to_wstring(SampleRuntime::firstImageHash) +
        L"; LastImageHash=" + std::to_wstring(SampleRuntime::lastImageHash) + L"; ImageChanged=" + (SampleRuntime::imageChanged ? L"yes" : L"no") +
        L"; NonUniformPixels=" + std::to_wstring(SampleRuntime::nonUniformPixels) +
        L"; GPUDrained=" + (drained ? L"yes" : L"no") + L"; SDKModuleLifetime=process; output=1280x720; Visual correctness and latency improvement are not inferred from API success.";
    if (SampleJournal::Error()) {
        result.status = DiagnosticStatus::Fail; result.hasWin32Error = true; result.win32Error = SampleJournal::Error();
        result.details += L"; Persistent journal write failed.";
    }
    finish(std::move(result)); return results;
}
} }
