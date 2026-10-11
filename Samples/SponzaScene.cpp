#include "SponzaScene.h"
#include "../../XeSS_SDK_3_0_2/samples/basic_sample_super_resolution_dx12/d3dx12.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <cfloat>
#include <climits>

namespace {
void Check(HRESULT hr, const char* operation) {
    if(FAILED(hr)) { char text[192]; sprintf_s(text,"%s HRESULT=0x%08X",operation,unsigned(hr));throw std::runtime_error(text); }
}
std::vector<unsigned char> Read(const std::wstring& path) {
    HANDLE file=CreateFile2(path.c_str(),GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,nullptr);
    if(file==INVALID_HANDLE_VALUE)Check(HRESULT_FROM_WIN32(GetLastError()),"Sponza CreateFile2");
    LARGE_INTEGER size={};
    if(!GetFileSizeEx(file,&size)){DWORD e=GetLastError();CloseHandle(file);Check(HRESULT_FROM_WIN32(e),"Sponza GetFileSizeEx");}
    if(size.QuadPart<=0 || size.QuadPart>1024LL*1024*1024){CloseHandle(file);throw std::runtime_error("Sponza invalid file size");}
    std::vector<unsigned char> bytes;
    try { bytes.resize(size_t(size.QuadPart)); } catch(...) { CloseHandle(file);throw; }
    size_t offset=0;
    while(offset<bytes.size()) {
        DWORD count=0, requested=DWORD((std::min)(size_t(1024*1024),bytes.size()-offset));
        if(!ReadFile(file,bytes.data()+offset,requested,&count,nullptr)||count!=requested) {
            DWORD e=GetLastError();CloseHandle(file);Check(HRESULT_FROM_WIN32(e?e:ERROR_HANDLE_EOF),"Sponza ReadFile");
        }
        offset+=count;
    }
    CloseHandle(file);return bytes;
}
struct Reader {
    const std::vector<unsigned char>& bytes;size_t offset=0;
    const unsigned char* Take(size_t count) {
        if(count>bytes.size()-offset)throw std::runtime_error("Truncated Sponza asset");
        const auto* ptr=bytes.data()+offset;offset+=count;return ptr;
    }
    UINT Uint(){UINT value;memcpy(&value,Take(4),4);return value;}
};
}

void SponzaScene::Init(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
    const std::wstring& asset, const std::wstring& vsPath, const std::wstring& psPath,
    DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat) {
    auto bytes=Read(asset);Reader r{bytes};
    if(memcmp(r.Take(4),"SPZ1",4))throw std::runtime_error("Invalid Sponza format");
    vertexCount=r.Uint();UINT indexCount=r.Uint(),drawCount=r.Uint(),materialCount=r.Uint(),textureCount=r.Uint();
    if(!vertexCount||vertexCount>10000000||!indexCount||indexCount>30000000||indexCount%3||
       !drawCount||drawCount>100000||!materialCount||materialCount>4096||!textureCount||textureCount>4096)
        throw std::runtime_error("Sponza counts outside limits");
    triangleCount=indexCount/3;
    memcpy(&eye,r.Take(12),12);memcpy(&direction,r.Take(12),12);memcpy(&up,r.Take(12),12);
    const auto* vertexData=r.Take(size_t(vertexCount)*32);
    const auto* indexData=r.Take(size_t(indexCount)*4);
    draws.resize(drawCount);memcpy(draws.data(),r.Take(size_t(drawCount)*sizeof(Draw)),size_t(drawCount)*sizeof(Draw));
    materials.resize(materialCount);memcpy(materials.data(),r.Take(size_t(materialCount)*sizeof(Material)),size_t(materialCount)*sizeof(Material));
    for(const auto& d:draws)if(d.material>=materialCount||d.first>indexCount||d.count>indexCount-d.first)
        throw std::runtime_error("Invalid Sponza draw");
    for(const auto& m:materials)if(m.texture>=textureCount)throw std::runtime_error("Invalid Sponza material");
    for(UINT i=0;i<indexCount;++i){UINT idx;memcpy(&idx,indexData+size_t(i)*4,4);if(idx>=vertexCount)throw std::runtime_error("Invalid Sponza index");}
    float minX=FLT_MAX,maxX=-FLT_MAX;
    for(UINT i=0;i<vertexCount;++i){float x;memcpy(&x,vertexData+size_t(i)*32,4);minX=(std::min)(x,minX);maxX=(std::max)(x,maxX);}
    sceneSpan=(std::max)(1.f,maxX-minX);
    auto uploadHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto defaultHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto buffer=[&](Resource& out,const void* data,UINT count,D3D12_RESOURCE_STATES finalState) {
        auto desc=CD3DX12_RESOURCE_DESC::Buffer(count);Resource staging;
        Check(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&staging)),"Sponza upload buffer");
        void* mapped=nullptr;D3D12_RANGE empty={};Check(staging->Map(0,&empty,&mapped),"Sponza Map");memcpy(mapped,data,count);staging->Unmap(0,nullptr);
        Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)),"Sponza geometry buffer");
        commands->CopyBufferRegion(out.Get(),0,staging.Get(),0,count);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(out.Get(),D3D12_RESOURCE_STATE_COPY_DEST,finalState);commands->ResourceBarrier(1,&barrier);
        uploads.push_back(staging);
    };
    buffer(vertices,vertexData,vertexCount*32,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    buffer(indices,indexData,indexCount*4,D3D12_RESOURCE_STATE_INDEX_BUFFER);
    vertexView={vertices->GetGPUVirtualAddress(),vertexCount*32,32};indexView={indices->GetGPUVirtualAddress(),indexCount*4,DXGI_FORMAT_R32_UINT};
    D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=textureCount;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Sponza descriptor heap");
    descriptorStride=device->GetDescriptorHandleIncrementSize(hd.Type);textures.resize(textureCount);
    for(UINT t=0;t<textureCount;++t) {
        UINT width=r.Uint(),height=r.Uint(),mips=r.Uint();
        if(!width||!height||width>1024||height>1024||!mips||mips>11)throw std::runtime_error("Sponza invalid texture");
        UINT expected=1;for(UINT extent=(std::max)(width,height);extent>1;extent/=2)++expected;
        if(mips!=expected)throw std::runtime_error("Sponza incomplete mip chain");
        auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,UINT16(mips));
        Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&textures[t])),"Sponza texture");
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mips);UINT64 total=0;
        device->GetCopyableFootprints(&desc,0,mips,0,footprints.data(),nullptr,nullptr,&total);
        auto ud=CD3DX12_RESOURCE_DESC::Buffer(total);Resource staging;
        Check(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&ud,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&staging)),"Sponza texture upload");
        unsigned char* mapped=nullptr;D3D12_RANGE empty={};Check(staging->Map(0,&empty,reinterpret_cast<void**>(&mapped)),"Sponza texture Map");
        for(UINT mip=0;mip<mips;++mip) {
            const auto& fp=footprints[mip];const auto* data=r.Take(size_t(fp.Footprint.Width)*fp.Footprint.Height*4);
            for(UINT row=0;row<fp.Footprint.Height;++row)memcpy(mapped+fp.Offset+size_t(row)*fp.Footprint.RowPitch,data+size_t(row)*fp.Footprint.Width*4,size_t(fp.Footprint.Width)*4);
        }
        staging->Unmap(0,nullptr);
        for(UINT mip=0;mip<mips;++mip) {
            CD3DX12_TEXTURE_COPY_LOCATION dst(textures[t].Get(),mip),src(staging.Get(),footprints[mip]);commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        }
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(textures[t].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);commands->ResourceBarrier(1,&barrier);
        uploads.push_back(staging);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv={};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=mips;
        device->CreateShaderResourceView(textures[t].Get(),&srv,CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(),t,descriptorStride));
    }
    if(r.offset!=bytes.size())throw std::runtime_error("Sponza trailing data");
    CD3DX12_DESCRIPTOR_RANGE range;range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
    CD3DX12_ROOT_PARAMETER params[2];params[0].InitAsConstants(sizeof(Constants)/4,0);params[1].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_ANISOTROPIC);sampler.MaxAnisotropy=8;
    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Sponza serialize root signature");
    Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Sponza root signature");
    auto vs=Read(vsPath),ps=Read(psPath);
    D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso={};pso.pRootSignature=root.Get();pso.VS={vs.data(),vs.size()};pso.PS={ps.data(),ps.size()};
    pso.InputLayout={layout,3};pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    pso.SampleMask=UINT_MAX;pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.NumRenderTargets=2;
    pso.RTVFormats[0]=colorFormat;pso.RTVFormats[1]=DXGI_FORMAT_R16G16_FLOAT;pso.DSVFormat=depthFormat;pso.SampleDesc.Count=1;
    Check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline)),"Sponza graphics PSO");
}

void SponzaScene::Render(ID3D12GraphicsCommandList* commands,UINT width,UINT height,
    float jitterX,float jitterY,double seconds,bool reset) {
    using namespace DirectX;
    auto forward=XMVector3Normalize(XMLoadFloat3(&direction));auto vertical=XMVector3Normalize(XMLoadFloat3(&up));
    auto right=XMVector3Normalize(XMVector3Cross(forward,vertical));
    auto position=XMLoadFloat3(&eye)+right*(sceneSpan*.015f*float(sin(seconds*.5)));
    auto look=XMVector3Normalize(forward+right*(.035f*float(sin(seconds*.35))));
    auto view=XMMatrixLookToRH(position,look,vertical);
    auto projection=XMMatrixPerspectiveFovRH(XM_PIDIV4,1280.f/720.f,10000.f,.1f);
    Constants cb={};XMStoreFloat4x4(&cb.current,XMMatrixTranspose(view*projection));
    cb.previous=history&&!reset?previous:cb.current;previous=cb.current;history=true;
    cb.jitter={2*jitterX/width,-2*jitterY/height,0,0};
    commands->SetPipelineState(pipeline.Get());commands->SetGraphicsRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[]={heap.Get()};commands->SetDescriptorHeaps(1,heaps);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->IASetVertexBuffers(0,1,&vertexView);commands->IASetIndexBuffer(&indexView);
    for(const auto& draw:draws) {
        const auto& mat=materials[draw.material];memcpy(&cb.factor,mat.factor,16);cb.parameters={mat.cutoff,0,0,0};
        commands->SetGraphicsRoot32BitConstants(0,sizeof(cb)/4,&cb,0);
        commands->SetGraphicsRootDescriptorTable(1,CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(),mat.texture,descriptorStride));
        commands->DrawIndexedInstanced(draw.count,1,draw.first,0,0);
    }
}
