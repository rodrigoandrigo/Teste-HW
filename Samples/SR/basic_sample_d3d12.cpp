#include "basic_sample_d3d12.h"
#include "utils.h"
#include "../RuntimeCalls.h"
namespace SampleSR {
using ::ThrowIfFailed;
//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// Copyright (C) 2021 Intel Corporation
//
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************












#define USE_LOWRES_MV 1

inline std::string XeSSResultToString(xess_result_t result)
{
    switch (result)
    {
    case XESS_RESULT_WARNING_NONEXISTING_FOLDER: return " XESS_RESULT_WARNING_NONEXISTING_FOLDER";
    case XESS_RESULT_WARNING_OLD_DRIVER: return "XESS_RESULT_WARNING_OLD_DRIVER";
    case XESS_RESULT_SUCCESS: return "XESS_RESULT_SUCCESS";
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return "XESS_RESULT_ERROR_UNSUPPORTED_DEVICE";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return "XESS_RESULT_ERROR_UNSUPPORTED_DRIVER";
    case XESS_RESULT_ERROR_UNINITIALIZED: return "XESS_RESULT_ERROR_UNINITIALIZED";
    case XESS_RESULT_ERROR_INVALID_ARGUMENT: return "XESS_RESULT_ERROR_INVALID_ARGUMENT";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY";
    case XESS_RESULT_ERROR_DEVICE: return "XESS_RESULT_ERROR_DEVICE";
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return "XESS_RESULT_ERROR_NOT_IMPLEMENTED";
    case XESS_RESULT_ERROR_INVALID_CONTEXT: return "XESS_RESULT_ERROR_INVALID_CONTEXT";
    case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "XESS_RESULT_ERROR_OPERATION_IN_PROGRESS";
    case XESS_RESULT_ERROR_UNSUPPORTED: return "XESS_RESULT_ERROR_UNSUPPORTED";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return "XESS_RESULT_ERROR_CANT_LOAD_LIBRARY";
    case XESS_RESULT_ERROR_WRONG_CALL_ORDER: return "XESS_RESULT_ERROR_WRONG_CALL_ORDER";
    case XESS_RESULT_ERROR_UNKNOWN: return "XESS_RESULT_ERROR_UNKNOWN";
    default: return "Unknown error code";
    }
}

inline void ThrowIfFailed(xess_result_t result, const std::string& err)
{
    if (result > XESS_RESULT_SUCCESS) // warnings
    {
        OutputDebugStringA(("XeSS-SR warning: " + XeSSResultToString(result)).c_str());
    }
    else if (result != XESS_RESULT_SUCCESS)
    {
        throw std::runtime_error(err + ". Error code: " + XeSSResultToString(result) + ".");
    }
}

inline void ThrowIfFailed(HRESULT result, const std::string& err)
{
    if (result != S_OK)
    {
        throw SampleRuntime::HrError(result, err);
    }
}

static void SetDebugMessageFilter(ID3D12Device* device)
{
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        return;
    }

    // Set the message filter to ignore warnings.
    D3D12_MESSAGE_ID hide[] =
    {
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,  // benign
        D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,  // benign
    };

    D3D12_INFO_QUEUE_FILTER filter = {};
    filter.DenyList.NumIDs = _countof(hide);
    filter.DenyList.pIDList = hide;

    if (FAILED(infoQueue->AddStorageFilterEntries(&filter)))
    {
        return;
    }
#else
    (void)device;
#endif
}

void BasicSampleD3D12::OnKeyUp(UINT8 key)
{
    switch (key)
    {
    case 0x31:  // Key 1
        m_outputIndex = DHI_Color;
        break;
    case 0x32:  // Key 2
        m_outputIndex = DHI_Velocity;
        break;
    case 0x33:  // Key 3
        m_outputIndex = DHI_XeSSOutputSRV;
        break;
    case 0x34:  // Key 4
    {
        xess_dump_parameters_t dump_params{"dump", 0, 200, XESS_DUMP_ALL_INPUTS};
        xessStartDump(m_xessContext, &dump_params);
        break;
    }
    case VK_SPACE:
        m_pause = !m_pause;
        break;
    }
}

BasicSampleD3D12::BasicSampleD3D12(UINT width, UINT height, std::wstring name, int preset)
    : DXSample(width, height, name)
    , m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
    , m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
    , m_fenceValues{}
    , m_rtvDescriptorSize(0)
    , m_constantBufferData{}
    , m_pCbvDataBegin(nullptr)
    , m_frameIndex(0)
    , m_desiredOutputResolution{width, height}

{
    if(preset<0||preset>3)throw std::invalid_argument("Invalid XeSS preset");
    m_preset=preset;
    const xess_quality_settings_t modes[]={XESS_QUALITY_SETTING_AA,XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS,
        XESS_QUALITY_SETTING_QUALITY,XESS_QUALITY_SETTING_BALANCED};
    m_quality=modes[preset];
    m_haltonPointSet = Utils::GenerateHalton(2, 3, 1, 32);
}

void BasicSampleD3D12::OnInit()
{
    SampleRuntime::Checkpoint("SR InitDx");
    InitDx();
    SampleRuntime::Checkpoint("SR InitXess");
    InitXess();
    SampleRuntime::Checkpoint("SR LoadPipeline");
    LoadPipeline();
    SampleRuntime::Checkpoint("SR LoadAssets");
    LoadAssets();
    SampleRuntime::Checkpoint("SR CreateFSQPipelines");
    CreateFSQPipelines();
    PopulateDescriptorHeap();
    SampleRuntime::Checkpoint("XeSS Sponza assets/upload BEGIN");
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(),nullptr));
    m_scene.Init(m_device.Get(),m_commandList.Get(),GetAssetFullPath(L"Assets/Sponza/scene.spz"),
        GetAssetFullPath(L"basic_sample_shaders/hlsl/sponza.hlsl.VSMain.cso"),
        GetAssetFullPath(L"basic_sample_shaders/hlsl/sponza.hlsl.PSMain.cso"),
        m_renderTargets[0]->GetDesc().Format,m_dsvTypedFormat);
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* commands[]={m_commandList.Get()};m_commandQueue->ExecuteCommandLists(1,commands);
    WaitForGpu();m_scene.ReleaseUploads();
    Teste_HW::Samples::SampleJournal::Write(L"PRESET BEGIN XeSS / "+std::wstring(PresetName(m_preset))+L"; Input="+
        std::to_wstring(m_renderResolution.x)+L"x"+std::to_wstring(m_renderResolution.y)+
        L"; Output="+std::to_wstring(m_desiredOutputResolution.x)+L"x"+std::to_wstring(m_desiredOutputResolution.y)+
        L"; Scene=SponzaNew; DurationSeconds=10; MotionVectors=low-resolution UV scaled to pixels; Depth=reversed; HistoryReset=yes");
}

void BasicSampleD3D12::InitDx()
{
    UINT dxgiFactoryFlags = SampleRuntime::DiagnoseGraphicsInitialization();

    auto get_adapter_description_wstrings = [](IDXGIAdapter* adapter, std::wstring& shortDesc, std::wstring& fullDesc)
    {
        if (!adapter) throw SampleRuntime::HrError(E_POINTER, "Cannot obtain adapter description: null adapter");
        DXGI_ADAPTER_DESC desc;
        ThrowIfFailed(adapter->GetDesc(&desc), "Cannot obtain adapter description");
        std::wostringstream sstream;
        sstream << std::hex << std::showbase
            << "VendorId=" << desc.VendorId
            << " DeviceId=" << desc.DeviceId
            << " AdapterLuid.LowPart=" << desc.AdapterLuid.LowPart
            << " AdapterLuid.HighPart=" << desc.AdapterLuid.HighPart
            << std::dec << std::noshowbase
            << " Revision=" << desc.Revision << " SubSysId=" << desc.SubSysId
            << " Desc=" << desc.Description << std::endl;
        shortDesc = desc.Description;
        fullDesc = sstream.str();
    };

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    std::wstring selectedAdapterShortDesc, selectedAdapterFullDesc;

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        get_adapter_description_wstrings(warpAdapter.Get(), selectedAdapterShortDesc, selectedAdapterFullDesc);

        ThrowIfFailed(
            D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    } else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        if (m_hardwareAdapterId != -1)
        {
            ThrowIfFailed(factory->EnumAdapters1((UINT)m_hardwareAdapterId, &hardwareAdapter), "Unabe to use hardware adapter with id " + std::to_string(m_hardwareAdapterId));
        }
        else
        {
            GetHardwareAdapter(factory.Get(), &hardwareAdapter);
        }

        get_adapter_description_wstrings(hardwareAdapter.Get(), selectedAdapterShortDesc, selectedAdapterFullDesc);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    SetDebugMessageFilter(m_device.Get());

    m_gpuName = selectedAdapterShortDesc;
    OutputDebugString((L"Selected adapter: " + selectedAdapterFullDesc).c_str());

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForComposition(
        m_commandQueue.Get(),  // Swap chain needs the queue so that it can force a flush on it.
        &swapChainDesc, nullptr, &swapChain));

    // This sample does not support fullscreen transitions.
    

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        AppDescriptorCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};

    ThrowIfFailed(m_device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&m_appDescriptorHeap)));
    m_appDescriptorHeap->SetName(L"app_descriptor_heap");
}

void BasicSampleD3D12::InitXess()
{
    ThrowIfFailed(xessD3D12CreateContext(m_device.Get(), &m_xessContext), "Unable to create XeSS context");

    if (XESS_RESULT_WARNING_OLD_DRIVER == xessIsOptimalDriver(m_xessContext))
    {
        SampleRuntime::warned = true;
    }

    xess_properties_t props;
    ThrowIfFailed(xessGetProperties(m_xessContext, &m_desiredOutputResolution, &props), "Unable to get XeSS properties");

    xess_version_t xefx_version;
    ThrowIfFailed(xessGetIntelXeFXVersion(m_xessContext, &xefx_version), "Unable to get XeFX version");

    D3D12_HEAP_DESC textures_heap_desc{props.tempTextureHeapSize,
        {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0},
        0, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES};

    ThrowIfFailed(m_device->CreateHeap(&textures_heap_desc, IID_PPV_ARGS(&m_texturesHeap)));
    m_texturesHeap->SetName(L"xess_textures_heap");

    m_uavDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    xess_d3d12_init_params_t params = {
        /* Output width and height. */
        m_desiredOutputResolution,
        /* Quality setting */
        m_quality,
        /* Initialization flags. */
        #if defined(USE_LOWRES_MV)
        XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_LDR_INPUT_COLOR,
        #else
        XESS_INIT_FLAG_HIGH_RES_MV,
        #endif
        /* Specfies the node mask for internally created resources on
         * multi-adapter systems. */
        0,
        /* Specfies the node visibility mask for internally created resources
         * on multi-adapter systems. */
        0,
        /* Optional externally allocated buffers storage for XeSS-SR. If NULL the
         * storage is allocated internally. If allocated, the heap type must be
         * D3D12_HEAP_TYPE_DEFAULT. This heap is not accessed by the CPU. */
        nullptr,
        /* Offset in the externally allocated heap for temporary buffers storage. */
        0,
        /* Optional externally allocated textures storage for XeSS-SR. If NULL the
         * storage is allocated internally. If allocated, the heap type must be
         * D3D12_HEAP_TYPE_DEFAULT. This heap is not accessed by the CPU. */
        m_texturesHeap.Get(),
        /* Offset in the externally allocated heap for temporary textures storage. */
        0,
        /* No pipeline library */
        NULL
    };

    ThrowIfFailed(xessD3D12Init(m_xessContext, &params), "Unable to initialize XeSS context");

    // Get optimal input resolution
    xess_2d_t minimum={},maximum={};
    ThrowIfFailed(xessGetOptimalInputResolution(m_xessContext,&m_desiredOutputResolution,m_quality,
        &m_renderResolution,&minimum,&maximum),"Unable to get optimal input resolution");
    if(!m_renderResolution.x||!m_renderResolution.y)throw std::runtime_error("Invalid XeSS input resolution");
    ThrowIfFailed(xessSetVelocityScale(m_xessContext,float(m_renderResolution.x),float(m_renderResolution.y)),"Unable to set UV velocity scale");
}

// Load the rendering pipeline dependencies.
void BasicSampleD3D12::LoadPipeline()
{
    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount * RTCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    {
        // Describe and create a depth stencil view (DSV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_dsvDescriptorSize =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    constexpr int depth_case = 2; // used to test various depth formats with xess*Dump calls.
    switch (depth_case)
    {
    case 0:
        m_dsvFormat = DXGI_FORMAT_D32_FLOAT;
        m_dsvTypedFormat = DXGI_FORMAT_D32_FLOAT;
        break;
    case 1:
        m_dsvFormat = DXGI_FORMAT_R32_TYPELESS;
        m_dsvTypedFormat = DXGI_FORMAT_D32_FLOAT;
        break;
    case 2:
        m_dsvFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
        m_dsvTypedFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        break;
    case 3:
        m_dsvFormat = DXGI_FORMAT_R24G8_TYPELESS;
        m_dsvTypedFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        break;
    case 4:
        m_dsvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        m_dsvTypedFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        break;
    case 5:
        m_dsvFormat = DXGI_FORMAT_R16_TYPELESS;
        m_dsvTypedFormat = DXGI_FORMAT_D16_UNORM;
        break;
    case 6:
        m_dsvFormat = DXGI_FORMAT_R16_UINT;
        m_dsvTypedFormat = DXGI_FORMAT_D16_UNORM;
        break;
    default:
        throw std::runtime_error("Please select depth format case!");
        break;
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create render targets and xes output for each frame
        for (UINT n = 0; n < FrameCount; n++)
        {
            D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
            D3D12_RESOURCE_DESC tex_desc =
                CD3DX12_RESOURCE_DESC::Tex2D(fmt, m_renderResolution.x, m_renderResolution.y);
            tex_desc.MipLevels = 1;

            // Get swapchain
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_presentRenderTargets[n])));

            D3D12_CLEAR_VALUE clear_value;
            clear_value.Color[0] = m_clearColor[0];
            clear_value.Color[1] = m_clearColor[1];
            clear_value.Color[2] = m_clearColor[2];
            clear_value.Color[3] = m_clearColor[3];
            clear_value.Format = fmt;

            // Render target
            tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                &tex_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value,
                IID_PPV_ARGS(&m_renderTargets[n])));

            tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            tex_desc.Format = m_dsvFormat;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                &tex_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&m_depthTargets[n])));

            // XeSS output
            tex_desc = CD3DX12_RESOURCE_DESC::Tex2D(
                fmt, m_desiredOutputResolution.x, m_desiredOutputResolution.y);
            tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                &tex_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                IID_PPV_ARGS(&m_xessOutput[n])));

            // Velocity
            fmt = DXGI_FORMAT_R16G16_FLOAT;
            tex_desc = CD3DX12_RESOURCE_DESC::Tex2D(
                fmt, m_renderResolution.x, m_renderResolution.y);
            tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            clear_value.Format = fmt;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                &tex_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value,
                IID_PPV_ARGS(&m_renderTargetsVelocity[n])));

            // Create RTVs
            // RT_Present
            m_device->CreateRenderTargetView(m_presentRenderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            // RT_Color
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            // RT_Velocity
            m_device->CreateRenderTargetView(m_renderTargetsVelocity[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            // RT_Depth
            auto dsv_desc = D3D12_DEPTH_STENCIL_VIEW_DESC{};
            dsv_desc.Format = m_dsvTypedFormat;
            dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
            dsv_desc.Texture2D = D3D12_TEX2D_DSV{};
            dsv_desc.Texture2D.MipSlice = 0;
            m_device->CreateDepthStencilView(m_depthTargets[n].Get(), &dsv_desc, dsvHandle);
            dsvHandle.Offset(1, m_dsvDescriptorSize);

            m_presentRenderTargets[n]->SetName(
                (std::wstring(L"Present") + std::to_wstring(n)).c_str());
            m_renderTargets[n]->SetName((std::wstring(L"Color") + std::to_wstring(n)).c_str());
            m_depthTargets[n]->SetName((std::wstring(L"Depth") + std::to_wstring(n)).c_str());
            m_xessOutput[n]->SetName((std::wstring(L"XeSSOutput") + std::to_wstring(n)).c_str());
            m_renderTargetsVelocity[n]->SetName(
                (std::wstring(L"Velocity") + std::to_wstring(n)).c_str());
        }

        // Create a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }
}

// Compile the shader from given hlsl file (path).
void BasicSampleD3D12::CompileFromFile(LPCWSTR path, const char* entryPoint, const char* shaderModel, UINT compileFlags, ComPtr<ID3DBlob>& shader)
{
    ThrowIfFailed(SampleRuntime::LoadShader(GetAssetFullPath(path).c_str(), nullptr, nullptr, entryPoint, shaderModel, compileFlags, 0, &shader, nullptr));
}

// Load the sample assets.
void BasicSampleD3D12::LoadAssets()
{
    // Create a root signature consisting of a descriptor table with a single CBV.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the
        // HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];

        ranges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

        // Allow input layout and deny uneccessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(
            _countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
            &rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "VSMainColor", "vs_5_0", compileFlags, vertexShader);
        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "PSMainColor", "ps_5_0", compileFlags, pixelShader);

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        psoDesc.DepthStencilState.StencilEnable = TRUE;
        psoDesc.DepthStencilState.FrontFace = psoDesc.DepthStencilState.BackFace =
            D3D12_DEPTH_STENCILOP_DESC{D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        psoDesc.DepthStencilState.StencilReadMask = (UINT8)0xff;
        psoDesc.DepthStencilState.StencilWriteMask = (UINT8)0xff;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.DSVFormat = m_dsvTypedFormat;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(
            &psoDesc, IID_PPV_ARGS(&m_pipelineStateColorPass)));
    }

    // Create velocity pass
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "VSMainVelocity", "vs_5_0", compileFlags, vertexShader);
        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "PSMainVelocity", "ps_5_0", compileFlags, pixelShader);

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(
            &psoDesc, IID_PPV_ARGS(&m_pipelineStateVelocityPass)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[m_frameIndex].Get(), m_pipelineStateColorPass.Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        Vertex triangleVertices[] = {
            {{0.0f, 0.25f * m_aspectRatio, 0.01f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{0.25f, -0.25f * m_aspectRatio, 0.5f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{-0.25f, -0.25f * m_aspectRatio, 0.2f}, {0.0f, 0.0f, 1.0f, 1.0f}}};

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not
        // recommended. Every time the GPU needs it, the upload heap will be marshalled
        // over. Please read up on Default Heap usage. An upload heap is used here for
        // code simplicity and because there are very few verts to actually transfer.
        auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        ThrowIfFailed(
            m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);  // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(
            m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create the constant buffer.
    {
        const UINT constantBufferSize =
            sizeof(SceneConstantBuffer);  // CB size is required to be 256-byte aligned.

        auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);
        ThrowIfFailed(
            m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;
        m_device->CreateConstantBufferView(
            &cbvDesc, m_appDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);  // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(
            m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForGpu();
    }
}

void BasicSampleD3D12::PopulateDescriptorHeap()
{
    auto addTexture =
        [&](std::uint32_t index, ID3D12Resource* resource, DXGI_FORMAT fmt, bool is_uav = false)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescHandle(
            m_appDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), (INT)index, m_uavDescriptorSize);

        if (is_uav)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.Format = fmt;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
            uavDesc.Texture2D.PlaneSlice = 0;

            m_device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuDescHandle);
        } else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = fmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.PlaneSlice = 0;
            m_device->CreateShaderResourceView(resource, &srvDesc, cpuDescHandle);
        }
    };

    for (UINT n = 0; n < FrameCount; ++n)
    {
        addTexture(DescriptorsPerFrame * n + DHI_Color, m_renderTargets[n].Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        addTexture(DescriptorsPerFrame * n + DHI_Velocity,
            m_renderTargetsVelocity[n].Get(), DXGI_FORMAT_R16G16_FLOAT);
        addTexture(DescriptorsPerFrame * n + DHI_XeSSOutputUAV, m_xessOutput[n].Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        addTexture(DescriptorsPerFrame * n + DHI_XeSSOutputSRV, m_xessOutput[n].Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT);
    }
}

void BasicSampleD3D12::CreateFSQPipelines()
{
    // Create a root signature
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the
        // HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
            &rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureFSQ)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShaderCopy;
        ComPtr<ID3DBlob> pixelShaderScaleVelocity;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "VSMainFSQ", "vs_5_0", compileFlags, vertexShader);
        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "PSMainCopyFSQ", "ps_5_0", compileFlags, pixelShaderCopy);
        CompileFromFile(L"basic_sample_shaders/hlsl/shader_xess_sr_d3d12.hlsl", "PSMainVelocityFSQ", "ps_5_0", compileFlags, pixelShaderScaleVelocity);

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {};
        psoDesc.pRootSignature = m_rootSignatureFSQ.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderCopy.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(
            m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFSQPass)));

        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderScaleVelocity.Get());
        ThrowIfFailed(
            m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFSQVelocityPass)));
    }
}

// Update frame-based values.
void BasicSampleD3D12::OnUpdate()
{
    m_sceneSeconds+=m_lastFrameTimeMS/1000.0;
    if (last_time.time_since_epoch().count() == 0)
    {
        last_time = std::chrono::high_resolution_clock::now();
    }

    auto current_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = current_time - last_time;
    last_time = current_time;

    const double speed = 1. / 2; //4 second for screen
    float translationSpeed = m_pause ? 0.f : (float)(speed * elapsed_seconds.count());
    const float offsetBounds = 1.25f;

    m_constantBufferData.offset.x += translationSpeed;
    if (m_constantBufferData.offset.x > offsetBounds)
    {
        m_constantBufferData.offset.x = -offsetBounds;
    }

    auto haltonValue = m_haltonPointSet[m_haltonIndex];
    m_haltonIndex = (m_haltonIndex + 1) % m_haltonPointSet.size();

    jitter[0] = haltonValue.first;
    jitter[1] = haltonValue.second;

    m_constantBufferData.offset.z = jitter[0];
    m_constantBufferData.offset.w = jitter[1];

    m_constantBufferData.resolution.x = (float)m_renderResolution.x;
    m_constantBufferData.resolution.y = (float)m_renderResolution.y;

    m_constantBufferData.velocity.x = -translationSpeed * ((float)m_desiredOutputResolution.x / 2.f);

    memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));

}

// Render the scene.
void BasicSampleD3D12::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    SampleRuntime::ObserveFrame(m_device.Get(), m_commandQueue.Get(), m_presentRenderTargets[m_frameIndex].Get());
    ThrowIfFailed(SampleRuntime::Present(m_swapChain.Get(), 1, 0));

    MoveToNextFrame();
}

void BasicSampleD3D12::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    if (m_xessContext) { ThrowIfFailed(xessDestroyContext(m_xessContext), "Unable to destroy XeSS context"); m_xessContext = nullptr; }

    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}

// Fill the command list with all the render commands and dependent state.
void BasicSampleD3D12::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineStateColorPass.Get()));

    // Run Color pass
    {
        // Set necessary state.
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        ID3D12DescriptorHeap* ppHeaps[] = {m_appDescriptorHeap.Get()};
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        CD3DX12_VIEWPORT render_res_viewport(
            0.f, 0.f, (float)m_renderResolution.x, (float)m_renderResolution.y);
        CD3DX12_RECT render_res_scissors(0, 0, (LONG)m_renderResolution.x, (LONG)m_renderResolution.y);

        m_commandList->SetGraphicsRootDescriptorTable(
            0, m_appDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        m_commandList->RSSetViewports(1, &render_res_viewport);
        m_commandList->RSSetScissorRects(1, &render_res_scissors);

        // Transition Color buffer to render target
        std::vector<CD3DX12_RESOURCE_BARRIER> transition =
        { CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_depthTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),

        };
        m_commandList->ResourceBarrier((UINT)transition.size(), transition.data());

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                (INT)(m_frameIndex * RTCount + RT_Color), m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle =
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
                (INT)(m_frameIndex), m_dsvDescriptorSize);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        // Record commands.
        m_commandList->ClearRenderTargetView(rtvHandle, m_clearColor, 0, nullptr);
        m_commandList->ClearDepthStencilView(
            dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0, 0, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // Run velocity pass
    {
        m_commandList->SetPipelineState(m_pipelineStateVelocityPass.Get());

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        // Indicate that the back buffer will be used as a render target.
        CD3DX12_RESOURCE_BARRIER transition =
            CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargetsVelocity[m_frameIndex].Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &transition);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                (INT)(m_frameIndex * RTCount + RT_Velocity), m_rtvDescriptorSize);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Record commands.
        m_commandList->ClearRenderTargetView(rtvHandle, m_clearColor, 0, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // Replace the legacy triangle passes with the real Sponza color/depth/motion.
    {
        SampleRuntime::Checkpoint("XeSS Sponza draw BEGIN");
        D3D12_CPU_DESCRIPTOR_HANDLE targets[]={
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex*RTCount+RT_Color,m_rtvDescriptorSize),
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex*RTCount+RT_Velocity,m_rtvDescriptorSize)};
        auto depth=CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),m_frameIndex,m_dsvDescriptorSize);
        const float background[]={.04f,.06f,.09f,1},zero[]={0,0,0,0};
        m_commandList->ClearRenderTargetView(targets[0],background,0,nullptr);
        m_commandList->ClearRenderTargetView(targets[1],zero,0,nullptr);
        m_commandList->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);
        m_commandList->OMSetRenderTargets(2,targets,FALSE,&depth);
        CD3DX12_VIEWPORT viewport(0.f,0.f,float(m_renderResolution.x),float(m_renderResolution.y));
        CD3DX12_RECT rect(0,0,m_renderResolution.x,m_renderResolution.y);
        m_commandList->RSSetViewports(1,&viewport);m_commandList->RSSetScissorRects(1,&rect);
        m_scene.Render(m_commandList.Get(),m_renderResolution.x,m_renderResolution.y,jitter[0],-jitter[1],m_sceneSeconds,m_sceneFrame==0);
    }
    // Transition render targets D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE for XeSS
    std::vector<CD3DX12_RESOURCE_BARRIER> transitions = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargetsVelocity[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_depthTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    m_commandList->ResourceBarrier((UINT)transitions.size(), transitions.data());

    // Run XeSS
    {
        xess_d3d12_execute_params_t exec_params{};
        exec_params.inputWidth = m_renderResolution.x;
        exec_params.inputHeight = m_renderResolution.y;
        exec_params.jitterOffsetX = jitter[0];
        exec_params.jitterOffsetY = -jitter[1];
        exec_params.exposureScale = 1.0f;
        exec_params.resetHistory = m_sceneFrame == 0;

        exec_params.pColorTexture = m_renderTargets[m_frameIndex].Get();
        exec_params.pVelocityTexture = m_renderTargetsVelocity[m_frameIndex].Get();
        exec_params.pOutputTexture = m_xessOutput[m_frameIndex].Get();
        #if defined(USE_LOWRES_MV)
        exec_params.pDepthTexture = m_depthTargets[m_frameIndex].Get();
        #else
        exec_params.pDepthTexture = nullptr;
        #endif
        exec_params.pExposureScaleTexture = 0;
        ThrowIfFailed(xessD3D12Execute(m_xessContext, m_commandList.Get(), &exec_params), "Unable to run XeSS");
        ++m_sceneFrame;
    }

    // Render XeSS output using full screen quad
    {
        ID3D12DescriptorHeap* ppHeaps[] = {m_appDescriptorHeap.Get()};
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        // Transition XeSS output is in UAV so we need to transition it to SRV
        CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
            m_xessOutput[m_frameIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_commandList->ResourceBarrier(1, &transition);

        if (m_outputIndex == DHI_Velocity)
        {
            m_commandList->SetPipelineState(m_pipelineStateFSQVelocityPass.Get());

        }
        else
        {
            m_commandList->SetPipelineState(m_pipelineStateFSQPass.Get());
        }

        m_commandList->SetGraphicsRootSignature(m_rootSignatureFSQ.Get());

        // Use selected output (DHI_XeSSOutputSRV by default)
        CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescHandle(
            m_appDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
            (INT)((DescriptorsPerFrame * m_frameIndex) + m_outputIndex), m_uavDescriptorSize);
        m_commandList->SetGraphicsRootDescriptorTable(0, gpuDescHandle);
        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        // Indicate that the back buffer will be used as a render target.
        transition =
            CD3DX12_RESOURCE_BARRIER::Transition(m_presentRenderTargets[m_frameIndex].Get(),
                D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &transition);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                (INT)(m_frameIndex * RTCount), m_rtvDescriptorSize);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Record commands.
        m_commandList->ClearRenderTargetView(rtvHandle, m_clearColor, 0, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        // Transition RT to present
        transition =
            CD3DX12_RESOURCE_BARRIER::Transition(m_presentRenderTargets[m_frameIndex].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &transition);

        // Transition XeSS output to UAV for future use
        transition = CD3DX12_RESOURCE_BARRIER::Transition(m_xessOutput[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &transition);
    }

    ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void BasicSampleD3D12::WaitForGpu()
{
    if (!m_fence || !m_commandQueue) return;
    if (!m_fenceEvent) throw SampleRuntime::HrError(E_HANDLE, "GPU fence event missing during cleanup");
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    SampleRuntime::Wait(m_fenceEvent);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void BasicSampleD3D12::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        SampleRuntime::Wait(m_fenceEvent);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

}
