#pragma once
#include "LL/basic_sample.h"
#include "SponzaScene.h"
#include "../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include "../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h"
namespace SampleFsr3Upscale {
class BasicSample : public SampleLL::BasicSample {
    ffxContext context = nullptr;
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    ffxCreateBackendDX12Desc backend = {};
    ffxOverrideVersion version = {};
    ffxCreateContextDescUpscale creation = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> output;
    uint64_t frame = 0;
    float jitterX = 0, jitterY = 0;
    bool resetHistory = true;
    UINT renderWidth = 1280, renderHeight = 720;
    int presetIndex = 0;
    int32_t jitterPhases = 1;
    SponzaScene scene;
    double sceneSeconds = 0;
    static void Check(ffxReturnCode_t code, const char* op) {
        wchar_t line[256] = {}; swprintf_s(line,L"FSR3 SR %hs ReturnCode=%u",op,unsigned(code));
        if (SampleRuntime::traceSdk || code) Teste_HW::Samples::SampleJournal::Write(line);
        ++SampleRuntime::sdkCalls;
        if (code) throw std::runtime_error(std::string(op)+" FfxReturnCode="+std::to_string(code));
    }
    void RecordGeneratedFrame() override {
        SampleRuntime::Checkpoint("Sponza scene draw BEGIN");
        CD3DX12_RESOURCE_BARRIER sceneBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargetsVelocity[m_frameIndex].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_depthTargets[m_frameIndex].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE)};
        m_commandList->ResourceBarrier(3,sceneBarriers);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[]={
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex*RTCount+RT_Color,m_rtvDescriptorSize),
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex*RTCount+RT_Velocity,m_rtvDescriptorSize)};
        auto dsv=CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex,m_dsvDescriptorSize);
        m_commandList->OMSetRenderTargets(2,rtvs,FALSE,&dsv);
        const float background[]={.04f,.06f,.09f,1},zero[]={0,0,0,0};
        m_commandList->ClearRenderTargetView(rtvs[0],background,0,nullptr);
        m_commandList->ClearRenderTargetView(rtvs[1],zero,0,nullptr);
        m_commandList->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);
        m_commandList->RSSetViewports(1,&m_viewport);m_commandList->RSSetScissorRects(1,&m_scissorRect);
        scene.Render(m_commandList.Get(),renderWidth,renderHeight,jitterX,jitterY,sceneSeconds,resetHistory);
        for(D3D12_RESOURCE_BARRIER& barrier:sceneBarriers)std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        m_commandList->ResourceBarrier(3,sceneBarriers);
        Teste_HW::Samples::SampleJournal::SetOperation("FSR3 3.1.5 ffxDispatch upscale");
        ffxDispatchDescUpscale d = {}; d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = m_commandList.Get(); d.color = ffxApiGetResourceDX12(m_renderTargets[m_frameIndex].Get());
        d.depth = ffxApiGetResourceDX12(m_depthTargets[m_frameIndex].Get());
        d.motionVectors = ffxApiGetResourceDX12(m_renderTargetsVelocity[m_frameIndex].Get());
        d.output = ffxApiGetResourceDX12(output.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        d.renderSize = {renderWidth,renderHeight}; d.upscaleSize = {1280,720}; d.jitterOffset = {jitterX,jitterY};
        d.motionVectorScale = {float(renderWidth),float(renderHeight)}; d.frameTimeDelta = m_lastFrameTimeMS > 0 ? m_lastFrameTimeMS : 16.6667f;
        d.preExposure = 1; d.reset = resetHistory; d.cameraNear=.1f; d.cameraFar=10000;
        d.cameraFovAngleVertical=DirectX::XM_PIDIV4; d.viewSpaceToMetersFactor=1;
        d.flags=FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        d.enableSharpening=true; d.sharpness=.2f;
        Check(dispatch(&context,&d.header),"dispatch upscale"); ++SampleRuntime::executeCalls;
        auto color=m_presentRenderTargets[m_frameIndex].Get();
        CD3DX12_RESOURCE_BARRIER b[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE)};
        m_commandList->ResourceBarrier(2,b); m_commandList->CopyResource(color,output.Get());
        b[0]=CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
        b[1]=CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(2,b); ++frame;
    }
public:
    static const wchar_t* PresetName(int preset) {
        return preset == 0 ? L"Native AA" : preset == 1 ? L"Quality" : L"Balanced";
    }
    explicit BasicSample(int preset = 0) : SampleLL::BasicSample(1280,720,L"FSR3 Upscaling 3.1.5"), presetIndex(preset) {
        if (preset < 0 || preset > 2) throw std::invalid_argument("Invalid FSR preset");
        m_useLatency=false;
        const float ratio = preset == 0 ? 1.f : preset == 1 ? 1.5f : 1.7f;
        renderWidth=UINT(1280.f/ratio); renderHeight=UINT(720.f/ratio);
    }
    void OnInit() override {
        SampleLL::BasicSample::OnInit();
        D3D12_FEATURE_DATA_SHADER_MODEL sm={D3D_SHADER_MODEL_6_4};
        ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof(sm)));
        if(sm.HighestShaderModel<D3D_SHADER_MODEL_6_2)throw SampleRuntime::HrError(DXGI_ERROR_UNSUPPORTED,"FSR3 SM6.2 required");
        const wchar_t* dll=L"amd_fidelityfx_upscaler_dx12.dll";
        create=SampleRuntime::Resolve<PfnFfxCreateContext>(dll,"ffxCreateContext");
        destroy=SampleRuntime::Resolve<PfnFfxDestroyContext>(dll,"ffxDestroyContext");
        query=SampleRuntime::Resolve<PfnFfxQuery>(dll,"ffxQuery");
        dispatch=SampleRuntime::Resolve<PfnFfxDispatch>(dll,"ffxDispatch");
        ffxQueryDescGetVersions q={}; q.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        q.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;q.device=m_device.Get();
        uint64_t count=0;q.outputCount=&count;Check(query(nullptr,&q.header),"query provider count");
        if(!count||count>64)throw std::runtime_error("FSR3 invalid provider count");
        std::vector<uint64_t> ids(count);std::vector<const char*> names(count);
        q.versionIds=ids.data();q.versionNames=names.data();Check(query(nullptr,&q.header),"query providers");
        uint64_t selected=0;
        for(size_t i=0;i<count&&i<ids.size();++i)if(names[i]) {
            Teste_HW::Samples::SampleJournal::WriteUtf8(names[i]);
            if(strcmp(names[i],"3.1.5")==0)selected=ids[i];
        }
        if(!selected)throw std::runtime_error("FSR3 3.1.5 provider absent; no FSR2/ML fallback");
        version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;version.versionId=selected;
        backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=m_device.Get();backend.header.pNext=&version.header;
        creation.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;creation.header.pNext=&backend.header;
        creation.flags=FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        creation.maxRenderSize={renderWidth,renderHeight};creation.maxUpscaleSize={1280,720};
        SampleRuntime::Checkpoint("FSR3 create upscale provider 3.1.5 BEGIN");Check(create(&context,&creation.header,nullptr),"create 3.1.5");
        auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        for(UINT i=0;i<FrameCount;++i) {
            auto resize=[&](Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
                auto rd=resource->GetDesc();rd.Width=renderWidth;rd.Height=renderHeight;rd.MipLevels=1;resource.Reset();
                ThrowIfFailed(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&resource)));
            };
            resize(m_renderTargets[i]);resize(m_depthTargets[i]);resize(m_renderTargetsVelocity[i]);
            auto rtv=CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),i*RTCount+RT_Color,m_rtvDescriptorSize);
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(),nullptr,rtv);
            rtv.Offset(1,m_rtvDescriptorSize);m_device->CreateRenderTargetView(m_renderTargetsVelocity[i].Get(),nullptr,rtv);
            D3D12_DEPTH_STENCIL_VIEW_DESC depth={};depth.Format=m_dsvTypedFormat;depth.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
            auto dsv=CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),i,m_dsvDescriptorSize);
            m_device->CreateDepthStencilView(m_depthTargets[i].Get(),&depth,dsv);
        }
        PopulateDescriptorHeap();m_viewport=CD3DX12_VIEWPORT(0.f,0.f,float(renderWidth),float(renderHeight));m_scissorRect=CD3DX12_RECT(0,0,renderWidth,renderHeight);
        auto rd=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,1280,720);rd.MipLevels=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ThrowIfFailed(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)));
        ffxQueryDescUpscaleGetJitterPhaseCount phase={};phase.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        phase.renderWidth=renderWidth;phase.displayWidth=1280;phase.pOutPhaseCount=&jitterPhases;
        Check(query(&context,&phase.header),"jitter phase count");
        if(jitterPhases<=0)throw std::runtime_error("Invalid jitter phase count");
        SampleRuntime::Checkpoint("Sponza assets/upload BEGIN");
        ThrowIfFailed(m_commandAllocator[m_frameIndex]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocator[m_frameIndex].Get(),nullptr));
        scene.Init(m_device.Get(),m_commandList.Get(),GetAssetFullPath(L"Assets/Sponza/scene.spz"),
            GetAssetFullPath(L"basic_sample_shaders/hlsl/sponza.hlsl.VSMain.cso"),
            GetAssetFullPath(L"basic_sample_shaders/hlsl/sponza.hlsl.PSMain.cso"),
            m_renderTargets[0]->GetDesc().Format,m_dsvTypedFormat);
        ThrowIfFailed(m_commandList->Close());
        ID3D12CommandList* lists[]={m_commandList.Get()};m_commandQueue->ExecuteCommandLists(1,lists);
        WaitForGpu();scene.ReleaseUploads();
        Teste_HW::Samples::SampleJournal::Write(L"Scene=SponzaNew; Renderer=UWP-native D3D12 (not desktop Cauldron); Vertices="+
            std::to_wstring(scene.vertexCount)+L"; Triangles="+std::to_wstring(scene.triangleCount)+L"; Draws="+
            std::to_wstring(scene.DrawCount())+L"; Textures="+std::to_wstring(scene.TextureCount())+L"; Camera=PhysCamera003 animated; MotionVectors=previous-minus-current UV; Depth=reversed");
        Teste_HW::Samples::SampleJournal::Write(L"PRESET BEGIN " + std::wstring(PresetName(presetIndex)) +
            L"; Input="+std::to_wstring(renderWidth)+L"x"+std::to_wstring(renderHeight)+
            L"; Output=1280x720; DurationSeconds=10; Provider=3.1.5; HistoryReset=yes");
    }
    void OnUpdate() override {
        resetHistory=frame==0;
        sceneSeconds+=m_lastFrameTimeMS/1000.0;
        ffxQueryDescUpscaleGetJitterOffset q={};q.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
        q.index=int32_t(frame%jitterPhases);q.phaseCount=jitterPhases;q.pOutX=&jitterX;q.pOutY=&jitterY;
        Check(query(&context,&q.header),"jitter offset");
    }
    void OnDestroy() override {
        SampleLL::BasicSample::OnDestroy();
        if(context){Check(destroy(&context,nullptr),"destroy upscale");context=nullptr;}output.Reset();
    }
};
}
