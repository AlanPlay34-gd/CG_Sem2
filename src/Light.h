#pragma once

#include <array>
#include <DirectXMath.h>

using namespace DirectX;

enum class LightType : unsigned int {
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct LightData {
    XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    float Range = 25.0f;

    XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f};
    float SpotAngle = 0.7f;

    XMFLOAT3 Color = {1.0f, 1.0f, 1.0f};
    float Intensity = 1.0f;

    unsigned int Type = static_cast<unsigned int>(LightType::Point);
    XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
};

static constexpr unsigned int kMaxLights = 64;

struct LightingConstants {
    unsigned int LightCount = 0;
    unsigned int EnableAmbient = 1;
    XMFLOAT2 Padding = {0.0f, 0.0f};
    std::array<LightData, kMaxLights> Lights{};
};
