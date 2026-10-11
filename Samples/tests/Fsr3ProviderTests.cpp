#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include "../../../XeSS_SDK_3_0_2/samples/basic_sample_super_resolution_dx12/d3dx12.h"
#include "../../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h"
#include "../../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include "../../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h"
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 4) return 1;
    bool upscale = argc >= 3 && wcscmp(argv[2], L"--upscale") == 0;
    UINT inputWidth=640,inputHeight=360;
    if(argc==4) {
        if(!upscale)return 1;
        if(wcscmp(argv[3],L"native")==0){inputWidth=1280;inputHeight=720;}
        else if(wcscmp(argv[3],L"quality")==0){inputWidth=853;inputHeight=480;}
        else if(wcscmp(argv[3],L"balanced")==0){inputWidth=752;inputHeight=423;}
        else return 1;
    }
    HMODULE dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { printf("Load Win32=%lu\n", GetLastError()); return 2; }
    auto query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(dll, "ffxQuery"));
    auto create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(dll, "ffxCreateContext"));
    auto destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(dll, "ffxDestroyContext"));
    if (!query || !create || !destroy) return 3;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    printf("Native D3D12 HRESULT=0x%08X\n", unsigned(hr)); if (FAILED(hr)) return 4;
    ffxQueryDescGetVersions v = {}; v.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    v.createDescType = upscale ? FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE : FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; v.device = device.Get();
    uint64_t count = 0; v.outputCount = &count;
    auto rc = query(nullptr, &v.header); printf("Query count rc=%u count=%llu\n", rc, count);
    if (rc || !count || count > 64) return 5;
    std::vector<uint64_t> ids(count); std::vector<const char*> names(count);
    v.versionIds = ids.data(); v.versionNames = names.data();
    rc = query(nullptr, &v.header); if (rc) return 6;
    uint64_t id = 0;
    for (size_t i = 0; i < count && i < names.size(); ++i) {
        printf("Provider=%s ID=%llX\n", names[i] ? names[i] : "null", ids[i]);
        if (names[i] && strcmp(names[i], upscale ? "3.1.5" : "3.1.6") == 0) id = ids[i];
    }
    if (!id) return 7;
    ffxOverrideVersion overrideVersion = {}; overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; overrideVersion.versionId = id;
    ffxCreateBackendDX12Desc backend = {}; backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device = device.Get(); backend.header.pNext = &overrideVersion.header;
    ffxCreateContextDescFrameGeneration desc = {}; desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; desc.header.pNext = &backend.header;
    desc.flags = FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    desc.displaySize = desc.maxRenderSize = {1280,720}; desc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ffxCreateContextDescUpscale upscaleDesc = {}; upscaleDesc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;upscaleDesc.header.pNext=&backend.header;
    upscaleDesc.flags=FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;upscaleDesc.maxRenderSize={inputWidth,inputHeight};upscaleDesc.maxUpscaleSize={1280,720};
    ffxContext context = nullptr; rc = create(&context, upscale ? &upscaleDesc.header : &desc.header, nullptr); printf("Create selected provider rc=%u\n", rc); if (rc) return 8;
    ffxConfigureDescFrameGeneration config = {}; config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY; config.frameGenerationEnabled = true;
    config.generationRect = {0,0,1280,720};
    if (!upscale) rc = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(dll,"ffxConfigure"))(&context,&config.header);
    printf("Configure without swapchain rc=%u\n",rc);
    // Native GPU smoke: two synthetic frames, standalone prepare/generate, fence and output readback.
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC q = {};
    if (FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))) return 10;
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator));
    device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list));
    ComPtr<ID3D12DescriptorHeap> rtv; D3D12_DESCRIPTOR_HEAP_DESC hd = {}; hd.NumDescriptors=3; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv));
    ComPtr<ID3D12Resource> inputs[3], output, readback;
    DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (int i=0;i<3;++i) {
        auto rd=CD3DX12_RESOURCE_DESC::Tex2D(formats[i],upscale?inputWidth:1280,upscale?inputHeight:720); rd.MipLevels=1; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&inputs[i])); if(FAILED(hr)) return 11;
        auto h=CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv->GetCPUDescriptorHandleForHeapStart(),i,stride); device->CreateRenderTargetView(inputs[i].Get(),nullptr,h);
    }
    auto rd=CD3DX12_RESOURCE_DESC::Tex2D(formats[0],1280,720); rd.MipLevels=1; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)); if(FAILED(hr))return 12;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint={}; UINT rows=0; UINT64 rowSize=0,total=0;
    device->GetCopyableFootprints(&rd,0,1,0,&footprint,&rows,&rowSize,&total);
    auto rh=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK); auto rb=CD3DX12_RESOURCE_DESC::Buffer(total);
    device->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));
    ComPtr<ID3D12Fence> fence; device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    auto dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(dll,"ffxDispatch"));
    for(UINT64 frame=0;frame<2;++frame) {
        if(frame) {allocator->Reset(); list->Reset(allocator.Get(),nullptr);}
        for(int i=0;i<3;++i) {
            if(frame) {auto b=CD3DX12_RESOURCE_BARRIER::Transition(inputs[i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET); list->ResourceBarrier(1,&b);}
            float clear[4]={0,0,0,1}; if(i==0) {clear[0]=frame?0.8f:0.2f;clear[1]=.3f;} if(i==1)clear[0]=.5f;
            auto h=CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv->GetCPUDescriptorHandleForHeapStart(),i,stride); list->ClearRenderTargetView(h,clear,0,nullptr);
            auto b=CD3DX12_RESOURCE_BARRIER::Transition(inputs[i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); list->ResourceBarrier(1,&b);
        }
        if (upscale) {
            ffxDispatchDescUpscale d={};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;d.commandList=list.Get();
            d.color=ffxApiGetResourceDX12(inputs[0].Get());d.depth=ffxApiGetResourceDX12(inputs[1].Get());d.motionVectors=ffxApiGetResourceDX12(inputs[2].Get());
            d.output=ffxApiGetResourceDX12(output.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);d.renderSize={inputWidth,inputHeight};d.upscaleSize={1280,720};d.motionVectorScale={float(inputWidth),float(inputHeight)};
            d.frameTimeDelta=16.6667f;d.preExposure=1;d.reset=!frame;d.cameraNear=.1f;d.cameraFar=100;d.cameraFovAngleVertical=1.5708f;d.viewSpaceToMetersFactor=1;
            rc=dispatch(&context,&d.header);printf("Upscale %ux%u -> 1280x720 frame=%llu rc=%u\n",inputWidth,inputHeight,frame,rc);if(rc)return 18;
        } else {
        ffxDispatchDescFrameGenerationPrepareV2 prep={}; prep.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        prep.commandList=list.Get();prep.frameID=frame;prep.renderSize={1280,720};prep.motionVectorScale={1280,720};prep.frameTimeDelta=16.6667f;prep.reset=!frame;
        prep.cameraNear=.1f;prep.cameraFar=100;prep.cameraFovAngleVertical=1.5708f;prep.viewSpaceToMetersFactor=1;prep.cameraUp[1]=prep.cameraRight[0]=prep.cameraForward[2]=1;
        prep.depth=ffxApiGetResourceDX12(inputs[1].Get());prep.motionVectors=ffxApiGetResourceDX12(inputs[2].Get());
        rc=dispatch(&context,&prep.header);printf("Prepare frame=%llu rc=%u\n",frame,rc);if(rc)return 13;
        ffxDispatchDescFrameGeneration gen={};gen.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;gen.commandList=list.Get();gen.frameID=frame;gen.reset=!frame;
        gen.presentColor=ffxApiGetResourceDX12(inputs[0].Get());gen.outputs[0]=ffxApiGetResourceDX12(output.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        gen.numGeneratedFrames=1;gen.generationRect={0,0,1280,720};gen.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;gen.minMaxLuminance[1]=1000;
        rc=dispatch(&context,&gen.header);printf("Generate frame=%llu rc=%u\n",frame,rc);if(rc)return 14;
        }
        if(frame) {auto b=CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
            auto dst=CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(),footprint);auto src=CD3DX12_TEXTURE_COPY_LOCATION(output.Get(),0);list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
        hr=list->Close();if(FAILED(hr))return 15; ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        queue->Signal(fence.Get(),frame+1);fence->SetEventOnCompletion(frame+1,event);if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 16;
    }
    BYTE* pixels=nullptr;D3D12_RANGE range={0,size_t(total)};hr=readback->Map(0,&range,reinterpret_cast<void**>(&pixels));if(FAILED(hr))return 17;
    UINT64 hash=1469598103934665603ull;for(UINT y=0;y<rows;++y)for(UINT64 x=0;x<rowSize;++x)hash=(hash^pixels[y*footprint.Footprint.RowPitch+x])*1099511628211ull;
    printf("GPU readback hash=%llu RemovedReason=0x%08X\n",hash,unsigned(device->GetDeviceRemovedReason()));readback->Unmap(0,nullptr);CloseHandle(event);
    auto teardown = destroy(&context, nullptr); printf("Destroy rc=%u\n",teardown);
    return rc || teardown ? 9 : 0;
}
