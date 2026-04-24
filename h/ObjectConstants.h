#pragma once
#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj;
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 TextureTransform;
    float TotalTime;
    DirectX::XMFLOAT3 Padding;

    ObjectConstants()
    {
        DirectX::XMStoreFloat4x4(&WorldViewProj, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&World, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&TextureTransform, DirectX::XMMatrixIdentity());
        TotalTime = 0.0f;
        Padding = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    }
};