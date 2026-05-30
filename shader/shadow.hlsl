cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4x4 gTextureTransform;
    float gTotalTime;
    float3 gObjectPadding;
    float4 gWaveParams;
    float4 gObjectCenter;
};

cbuffer ShadowPassConstants : register(b1)
{
    float4x4 gLightViewProj;
};

struct VSInput
{
    float3 PosL : POSITION;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
};

VSOut VS_Shadow(VSInput vin)
{
    VSOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gLightViewProj);
    return vout;
}
