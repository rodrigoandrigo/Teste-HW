#pragma once
#include <windows.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <string>

// UWP-native scene renderer, not a dependency on the desktop Cauldron host.
class SponzaScene {
    using Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Draw { UINT first, count, material; };
    struct Material { float factor[4], cutoff; UINT texture; };
    struct Constants {
        DirectX::XMFLOAT4X4 current, previous;
        DirectX::XMFLOAT4 jitter, factor, parameters;
    };
    Resource vertices, indices;
    std::vector<Resource> textures, uploads;
    std::vector<Draw> draws;
    std::vector<Material> materials;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    D3D12_VERTEX_BUFFER_VIEW vertexView = {};
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    DirectX::XMFLOAT3 eye = {}, direction = {}, up = {};
    DirectX::XMFLOAT4X4 previous = {};
    UINT descriptorStride = 0;
    float sceneSpan = 1;
    bool history = false;
public:
    UINT vertexCount = 0, triangleCount = 0;
    void Init(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
              const std::wstring& asset, const std::wstring& vertexShader,
              const std::wstring& pixelShader, DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat);
    void ReleaseUploads() { uploads.clear(); }
    void Render(ID3D12GraphicsCommandList* commands, UINT width, UINT height,
                float jitterX, float jitterY, double seconds, bool reset);
    UINT DrawCount() const { return UINT(draws.size()); }
    UINT TextureCount() const { return UINT(textures.size()); }
};
