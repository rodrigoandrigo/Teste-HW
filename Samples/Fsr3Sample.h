#pragma once
#include "LL/basic_sample.h"
#include "../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h"
#include "../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h"

namespace SampleFsr3 {
class BasicSample : public SampleLL::BasicSample {
    ffxContext context = nullptr;
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    ffxCreateBackendDX12Desc backend = {};
    ffxOverrideVersion overrideVersion = {};
    ffxCreateContextDescFrameGeneration creation = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> output;
    uint64_t frame = 0;
    bool resetHistory = true;
    static void Check(ffxReturnCode_t code, const char* operation) {
        wchar_t line[256] = {};
        swprintf_s(line, L"FSR3 %hs ReturnCode=%u", operation, static_cast<unsigned>(code));
        if (SampleRuntime::traceSdk || code != FFX_API_RETURN_OK) Teste_HW::Samples::SampleJournal::Write(line);
        ++SampleRuntime::sdkCalls;
        if (code != FFX_API_RETURN_OK) throw std::runtime_error(std::string(operation) + " FfxReturnCode=" + std::to_string(code));
    }
    void RecordGeneratedFrame() override {
        using Teste_HW::Samples::SampleJournal;
        SampleJournal::SetOperation("FSR3 configure/prepare/dispatch");
        ffxConfigureDescFrameGeneration config = {};
        config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        config.frameGenerationEnabled = true;
        config.frameID = frame;
        config.generationRect = { 0, 0, int32_t(m_width), int32_t(m_height) };
        SampleJournal::SetOperation("FSR3 ffxConfigure");
        Check(configure(&context, &config.header), "configure no swapchain");
        ffxDispatchDescFrameGenerationPrepareV2 prepare = {};
        prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prepare.commandList = m_commandList.Get();
        prepare.frameID = frame;
        prepare.renderSize = { m_width, m_height };
        prepare.motionVectorScale = { float(m_width), float(m_height) };
        prepare.frameTimeDelta = m_lastFrameTimeMS > 0 ? m_lastFrameTimeMS : 16.6667f;
        prepare.reset = resetHistory;
        prepare.cameraNear = 0.1f; prepare.cameraFar = 100.f;
        prepare.cameraFovAngleVertical = DirectX::XM_PIDIV2;
        prepare.viewSpaceToMetersFactor = 1.f;
        prepare.cameraUp[1] = 1; prepare.cameraRight[0] = 1; prepare.cameraForward[2] = 1;
        prepare.depth = ffxApiGetResourceDX12(m_depthTargets[m_frameIndex].Get());
        prepare.motionVectors = ffxApiGetResourceDX12(m_renderTargetsVelocity[m_frameIndex].Get());
        SampleJournal::SetOperation("FSR3 ffxDispatch prepare");
        Check(dispatch(&context, &prepare.header), "prepare");
        auto color = m_presentRenderTargets[m_frameIndex].Get();
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(color, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_commandList->ResourceBarrier(1, &barrier);
        ffxDispatchDescFrameGeneration generate = {};
        generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
        generate.commandList = m_commandList.Get(); generate.frameID = frame;
        generate.presentColor = ffxApiGetResourceDX12(color);
        generate.outputs[0] = ffxApiGetResourceDX12(output.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        generate.numGeneratedFrames = 1; generate.reset = resetHistory;
        generate.generationRect = { 0, 0, int32_t(m_width), int32_t(m_height) };
        generate.minMaxLuminance[0] = 0; generate.minMaxLuminance[1] = 1000;
        generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        SampleJournal::SetOperation("FSR3 ffxDispatch generate");
        Check(dispatch(&context, &generate.header), "generate");
        // Display only the generated result; this test does not claim doubled scanout/FPS.
        CD3DX12_RESOURCE_BARRIER copies[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        m_commandList->ResourceBarrier(2, copies);
        m_commandList->CopyResource(color, output.Get());
        copies[0] = CD3DX12_RESOURCE_BARRIER::Transition(color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        copies[1] = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(2, copies);
        if (frame && !resetHistory) ++SampleRuntime::generatedFrames;
        ++frame;
    }
public:
    BasicSample() : SampleLL::BasicSample(1280, 720, L"FSR3 FG 3.1.6") { m_useLatency = false; }
    void OnInit() override {
        SampleLL::BasicSample::OnInit();
        D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_4 };
        ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)));
        if (sm.HighestShaderModel < D3D_SHADER_MODEL_6_2) throw SampleRuntime::HrError(DXGI_ERROR_UNSUPPORTED, "FSR3 requires SM6.2");
        Teste_HW::Samples::SampleJournal::Write(L"FSR3: binary provider 3.1.6; app shader-model query capped at 6.4; generated-only presentation, no AMD proxy swapchain or XeLL.");
        const wchar_t* dll = L"amd_fidelityfx_framegeneration_dx12.dll";
        create = SampleRuntime::Resolve<PfnFfxCreateContext>(dll, "ffxCreateContext");
        destroy = SampleRuntime::Resolve<PfnFfxDestroyContext>(dll, "ffxDestroyContext");
        query = SampleRuntime::Resolve<PfnFfxQuery>(dll, "ffxQuery");
        configure = SampleRuntime::Resolve<PfnFfxConfigure>(dll, "ffxConfigure");
        dispatch = SampleRuntime::Resolve<PfnFfxDispatch>(dll, "ffxDispatch");
        ffxQueryDescGetVersions versions = {};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versions.device = m_device.Get();
        uint64_t count = 0; versions.outputCount = &count;
        Check(query(nullptr, &versions.header), "query provider count");
        if (!count || count > 64) throw std::runtime_error("FSR3 invalid provider count");
        std::vector<uint64_t> ids(count); std::vector<const char*> names(count);
        versions.versionIds = ids.data(); versions.versionNames = names.data();
        Check(query(nullptr, &versions.header), "query providers");
        uint64_t selected = 0;
        for (size_t i = 0; i < count && i < ids.size(); ++i) {
            if (!names[i]) continue;
            Teste_HW::Samples::SampleJournal::WriteUtf8(names[i]);
            if (std::string(names[i]).find("3.1.6") != std::string::npos) selected = ids[i];
        }
        if (!selected) throw std::runtime_error("FSR3 provider 3.1.6 unavailable; no ML fallback permitted");
        overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; overrideVersion.versionId = selected;
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device = m_device.Get();
        backend.header.pNext = &overrideVersion.header;
        creation.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; creation.header.pNext = &backend.header;
        creation.flags = FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
        creation.displaySize = creation.maxRenderSize = { m_width, m_height };
        creation.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        SampleRuntime::Checkpoint("FSR3 create provider 3.1.6 BEGIN");
        Check(create(&context, &creation.header, nullptr), "create provider 3.1.6");
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height);
        desc.MipLevels = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)));
    }
    void OnUpdate() override {
        float old = m_constantBufferData.offset.x;
        SampleLL::BasicSample::OnUpdate();
        float delta = m_constantBufferData.offset.x - old;
        resetHistory = frame == 0 || delta < 0;
        m_constantBufferData.velocity = { resetHistory ? 0.f : -delta * .5f, 0, 0, 0 };
        memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
    }
    void OnDestroy() override {
        SampleLL::BasicSample::OnDestroy(); // Drains GPU before destroying FidelityFX resources.
        if (context) { Check(destroy(&context, nullptr), "destroy"); context = nullptr; }
        output.Reset();
    }
};
}
