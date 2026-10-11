#include "../SponzaScene.h"
#include "../../../XeSS_SDK_3_0_2/samples/basic_sample_super_resolution_dx12/d3dx12.h"
#include "../../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include "../../../FidelityFX-SDK-2_3_0/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h"
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include "../../../XeSS_SDK_3_0_2/inc/xess/xess_d3d12.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D HRESULT="+std::to_string(unsigned(hr)));}
static void Ffx(ffxReturnCode_t rc){if(rc)throw std::runtime_error("FfxReturnCode="+std::to_string(rc));}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2&&argc!=3)return 1;
    const bool xess=argc==3&&wcscmp(argv[2],L"--xess")==0;
    try {
        std::wstring project=argv[1];
        ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q={};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("CreateEvent failed");UINT64 serial=0;
        auto submit=[&]{Check(list->Close());ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
            Check(queue->Signal(fence.Get(),++serial));Check(fence->SetEventOnCompletion(serial,event));
            if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");};
        auto reset=[&]{Check(allocator->Reset());Check(list->Reset(allocator.Get(),nullptr));};
        SponzaScene scene;scene.Init(device.Get(),list.Get(),project+L"/Assets/Sponza/scene.spz",
            project+L"/basic_sample_shaders/hlsl/sponza.hlsl.VSMain.cso",project+L"/basic_sample_shaders/hlsl/sponza.hlsl.PSMain.cso",
            DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
        submit();scene.ReleaseUploads();
        printf("Scene vertices=%u triangles=%u draws=%u textures=%u\n",scene.vertexCount,scene.triangleCount,scene.DrawCount(),scene.TextureCount());
        std::wstring dllPath=project+(xess?L"/x64/Debug/Teste-HW/libxess.dll":L"/../FidelityFX-SDK-2_3_0/Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll");
        HMODULE dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!dll)throw std::runtime_error("LoadLibraryEx Win32="+std::to_string(GetLastError()));
        auto create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(dll,"ffxCreateContext"));
        auto destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(dll,"ffxDestroyContext"));
        auto query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(dll,"ffxQuery"));
        auto dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(dll,"ffxDispatch"));
        if(!xess&&(!create||!destroy||!query||!dispatch))throw std::runtime_error("Missing FSR export");
        auto xc=reinterpret_cast<decltype(&xessD3D12CreateContext)>(GetProcAddress(dll,"xessD3D12CreateContext"));
        auto xi=reinterpret_cast<decltype(&xessD3D12Init)>(GetProcAddress(dll,"xessD3D12Init"));
        auto xr=reinterpret_cast<decltype(&xessGetOptimalInputResolution)>(GetProcAddress(dll,"xessGetOptimalInputResolution"));
        auto xv=reinterpret_cast<decltype(&xessSetVelocityScale)>(GetProcAddress(dll,"xessSetVelocityScale"));
        auto xe=reinterpret_cast<decltype(&xessD3D12Execute)>(GetProcAddress(dll,"xessD3D12Execute"));
        auto xd=reinterpret_cast<decltype(&xessDestroyContext)>(GetProcAddress(dll,"xessDestroyContext"));
        auto Xess=[](xess_result_t rc){if(rc<0)throw std::runtime_error("XeSS result="+std::to_string(int(rc)));};
        if(xess&&(!xc||!xi||!xr||!xv||!xe||!xd))throw std::runtime_error("Missing XeSS export");
        uint64_t provider=0;
        if(!xess) {
        ffxQueryDescGetVersions versions={};versions.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;versions.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;versions.device=device.Get();
        uint64_t count=0;versions.outputCount=&count;Ffx(query(nullptr,&versions.header));if(!count||count>64)throw std::runtime_error("Provider count invalid");
        std::vector<uint64_t> ids(count);std::vector<const char*> names(count);versions.versionIds=ids.data();versions.versionNames=names.data();Ffx(query(nullptr,&versions.header));
        for(size_t i=0;i<count;++i)if(names[i]&&strcmp(names[i],"3.1.5")==0)provider=ids[i];if(!provider)throw std::runtime_error("3.1.5 missing");
        }
        UINT widths[]={1280,853,752,0},heights[]={720,480,423,0};
        const xess_quality_settings_t modes[]={XESS_QUALITY_SETTING_AA,XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS,XESS_QUALITY_SETTING_QUALITY,XESS_QUALITY_SETTING_BALANCED};
        for(UINT preset=0;preset<(xess?4u:3u);++preset) {
            ffxOverrideVersion version={};version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;version.versionId=provider;
            ffxCreateBackendDX12Desc backend={};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=device.Get();backend.header.pNext=&version.header;
            ffxCreateContextDescUpscale desc={};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;desc.header.pNext=&backend.header;
            desc.flags=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE|FFX_UPSCALE_ENABLE_DEPTH_INVERTED;desc.maxRenderSize={widths[preset],heights[preset]};desc.maxUpscaleSize={1280,720};
            ffxContext context=nullptr;xess_context_handle_t xcontext=nullptr;
            if(xess) {
                Xess(xc(device.Get(),&xcontext));xess_d3d12_init_params_t init={};init.outputResolution={1280,720};init.qualitySetting=modes[preset];init.initFlags=XESS_INIT_FLAG_INVERTED_DEPTH|XESS_INIT_FLAG_LDR_INPUT_COLOR;Xess(xi(xcontext,&init));
                xess_2d_t optimal={},minimum={},maximum={},out={1280,720};Xess(xr(xcontext,&out,modes[preset],&optimal,&minimum,&maximum));widths[preset]=optimal.x;heights[preset]=optimal.y;
                Xess(xv(xcontext,float(optimal.x),float(optimal.y)));printf("XeSS mode=%d optimal=%ux%u\n",int(modes[preset]),optimal.x,optimal.y);
            }else Ffx(create(&context,&desc.header,nullptr));
            auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ComPtr<ID3D12Resource> color,depth,motion,output,readback;
            auto texture=[&](ComPtr<ID3D12Resource>& out,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,UINT w,UINT h) {
                auto rd=CD3DX12_RESOURCE_DESC::Tex2D(format,w,h);rd.MipLevels=1;rd.Flags=flags;
                Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out)));};
            texture(color,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,widths[preset],heights[preset]);
            texture(motion,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,widths[preset],heights[preset]);
            texture(depth,DXGI_FORMAT_R32G8X24_TYPELESS,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,widths[preset],heights[preset]);
            texture(output,xess?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,1280,720);
            ComPtr<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));
            hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
            D3D12_CPU_DESCRIPTOR_HANDLE handles[]={rtv->GetCPUDescriptorHandleForHeapStart(),CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv->GetCPUDescriptorHandleForHeapStart(),1,device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV))};
            device->CreateRenderTargetView(color.Get(),nullptr,handles[0]);device->CreateRenderTargetView(motion.Get(),nullptr,handles[1]);
            D3D12_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dd.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(depth.Get(),&dd,dsv->GetCPUDescriptorHandleForHeapStart());
            auto rd=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={};UINT64 total=0;device->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&total);
            auto readHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);auto rb=CD3DX12_RESOURCE_DESC::Buffer(total);Check(device->CreateCommittedResource(&readHeap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
            uint64_t first=0,last=0;
            for(UINT frame=0;frame<3;++frame) {
                reset();
                auto before=frame?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON;
                CD3DX12_RESOURCE_BARRIER barriers[]={CD3DX12_RESOURCE_BARRIER::Transition(color.Get(),before,D3D12_RESOURCE_STATE_RENDER_TARGET),CD3DX12_RESOURCE_BARRIER::Transition(motion.Get(),before,D3D12_RESOURCE_STATE_RENDER_TARGET),CD3DX12_RESOURCE_BARRIER::Transition(depth.Get(),before,D3D12_RESOURCE_STATE_DEPTH_WRITE)};
                list->ResourceBarrier(3,barriers);auto outBarrier=CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),frame?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);list->ResourceBarrier(1,&outBarrier);
                const float background[]={.04f,.06f,.09f,1},zero[]={0,0,0,0};list->ClearRenderTargetView(handles[0],background,0,nullptr);list->ClearRenderTargetView(handles[1],zero,0,nullptr);
                auto depthHandle=dsv->GetCPUDescriptorHandleForHeapStart();list->ClearDepthStencilView(depthHandle,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);list->OMSetRenderTargets(2,handles,FALSE,&depthHandle);
                CD3DX12_VIEWPORT viewport(0.f,0.f,float(widths[preset]),float(heights[preset]));CD3DX12_RECT rect(0,0,widths[preset],heights[preset]);list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
                float x=0,y=0;
                if(xess){auto halton=[](UINT n,UINT base){float value=0,weight=1;while(n){weight/=base;value+=weight*(n%base);n/=base;}return value-.5f;};x=halton(frame+1,2);y=-halton(frame+1,3);}
                else {int32_t phases=0;ffxQueryDescUpscaleGetJitterPhaseCount jq={};jq.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;jq.renderWidth=widths[preset];jq.displayWidth=1280;jq.pOutPhaseCount=&phases;Ffx(query(&context,&jq.header));
                ffxQueryDescUpscaleGetJitterOffset jo={};jo.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;jo.index=frame;jo.phaseCount=phases;jo.pOutX=&x;jo.pOutY=&y;Ffx(query(&context,&jo.header));}
                scene.Render(list.Get(),widths[preset],heights[preset],x,y,frame*2.0,!frame);
                for(D3D12_RESOURCE_BARRIER& b:barriers){b.Transition.StateBefore=b.Transition.StateAfter;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}list->ResourceBarrier(3,barriers);
                ffxDispatchDescUpscale d={};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;d.commandList=list.Get();d.color=ffxApiGetResourceDX12(color.Get());d.depth=ffxApiGetResourceDX12(depth.Get());d.motionVectors=ffxApiGetResourceDX12(motion.Get());d.output=ffxApiGetResourceDX12(output.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
                d.renderSize={widths[preset],heights[preset]};d.upscaleSize={1280,720};d.motionVectorScale={float(widths[preset]),float(heights[preset])};d.jitterOffset={x,y};d.frameTimeDelta=16.6667f;d.preExposure=1;d.reset=!frame;d.cameraNear=.1f;d.cameraFar=10000;d.cameraFovAngleVertical=DirectX::XM_PIDIV4;d.viewSpaceToMetersFactor=1;d.flags=FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;d.enableSharpening=true;d.sharpness=.2f;
                if(xess){xess_d3d12_execute_params_t ex={};ex.pColorTexture=color.Get();ex.pVelocityTexture=motion.Get();ex.pDepthTexture=depth.Get();ex.pOutputTexture=output.Get();ex.inputWidth=widths[preset];ex.inputHeight=heights[preset];ex.jitterOffsetX=x;ex.jitterOffsetY=y;ex.exposureScale=1;ex.resetHistory=!frame;Xess(xe(xcontext,list.Get(),&ex));}
                else Ffx(dispatch(&context,&d.header));
                outBarrier=CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&outBarrier);
                CD3DX12_TEXTURE_COPY_LOCATION src(output.Get(),0),dst(readback.Get(),fp);list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);submit();
                unsigned char* pixels=nullptr;D3D12_RANGE range={0,size_t(total)};Check(readback->Map(0,&range,reinterpret_cast<void**>(&pixels)));uint64_t hash=14695981039346656037ULL,nonuniform=0;
                FILE* image=nullptr;if(frame==2){std::wstring name=project+(xess?L"/Samples/tests/XeSS_Sponza_preset_":L"/Samples/tests/Sponza_preset_")+std::to_wstring(preset)+L".ppm";_wfopen_s(&image,name.c_str(),L"wb");if(image)fprintf(image,"P6\n1280 720\n255\n");}
                unsigned char firstPixel[3]={};
                for(UINT row=0;row<720;++row)for(UINT col=0;col<1280;++col){const auto* raw=pixels+fp.Offset+size_t(row)*fp.Footprint.RowPitch+col*(xess?8:4);unsigned char converted[3];for(int c=0;c<3;++c){if(xess){uint16_t half;memcpy(&half,raw+c*2,2);float f=DirectX::PackedVector::XMConvertHalfToFloat(half);converted[c]=static_cast<unsigned char>((std::max)(0.f,(std::min)(255.f,f*255.f)));}else converted[c]=raw[c];}const auto* p=converted;if(!row&&!col)memcpy(firstPixel,p,3);for(int c=0;c<3;++c){hash^=p[c];hash*=1099511628211ULL;}if(memcmp(p,firstPixel,3))++nonuniform;if(image)fwrite(p,1,3,image);}
                if(image)fclose(image);D3D12_RANGE empty={};readback->Unmap(0,&empty);if(!frame)first=hash;last=hash;
                printf("Preset=%u frame=%u hash=%llu NonUniform=%llu\n",preset,frame,hash,nonuniform);if(nonuniform<10000)throw std::runtime_error("No scene pixels");
            }
            if(first==last)throw std::runtime_error("Scene not animated");if(xess)Xess(xd(xcontext));else Ffx(destroy(&context,nullptr));printf("Preset=%u PASS\n",preset);
        }
        Check(device->GetDeviceRemovedReason());
        if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T n=0;messages->GetMessage(i,nullptr,&n);std::vector<unsigned char> buffer(n);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());Check(messages->GetMessage(i,msg,&n));if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){printf("D3D12 ERROR: %s\n",msg->pDescription);throw std::runtime_error("D3D12 validation error");}}
        CloseHandle(event);printf("Sponza + %s all presets PASS\n",xess?"XeSS":"FSR3");return 0;
    }catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 2;}
}
