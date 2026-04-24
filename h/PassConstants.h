#pragma once
#include <DirectXMath.h>

struct PassConstants
{
    DirectX::XMFLOAT4X4 InvViewProj;
    DirectX::XMFLOAT3 EyePosW;
    float Padding;
    DirectX::XMFLOAT4 AmbientColor;
    
    PassConstants()
    {
        DirectX::XMStoreFloat4x4(&InvViewProj, DirectX::XMMatrixIdentity());
        EyePosW = DirectX::XMFLOAT3(0,0,0);
        Padding = 0.0f;
        AmbientColor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    }
};