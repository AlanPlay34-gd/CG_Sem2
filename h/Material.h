#pragma once
#include <string>
#include <wrl/client.h>
#include <d3d12.h>

// Эта структура может быть не нужна, если вы храните всё в SubmeshMaterial.
// Оставлю на случай, если она используется в других местах.
struct Material
{
    std::string Name;
    std::string DiffuseMap;
    std::string NormalMap;
    std::string DisplacementMap;
    UINT DiffuseSrvIndex = 0;
    UINT NormalSrvIndex = 0;
    UINT DisplacementSrvIndex = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> NormalTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> DisplacementTexture;
};