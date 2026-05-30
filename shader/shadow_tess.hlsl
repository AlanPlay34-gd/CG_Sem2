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

cbuffer PassConstants : register(b2)
{
    float4x4 gView;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
};

Texture2D gDisplacementMap : register(t0);
SamplerState gLinearWrap : register(s0);

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 TexC : TEXCOORD;
};

struct ControlPoint
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

ControlPoint VS_ControlPoint(VSInput vin)
{
    ControlPoint cp;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    cp.PosW = posW.xyz;

    float3x3 world3x3 = (float3x3)gWorld;
    cp.NormalW = normalize(mul(vin.NormalL, world3x3));

    float4 tex = mul(float4(vin.TexC, 0.0f, 1.0f), gTextureTransform);
    cp.TexC = tex.xy;

    return cp;
}

struct HSConstants
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

HSConstants HS_Constants(InputPatch<ControlPoint, 3> patch, uint patchId : SV_PrimitiveID)
{
    HSConstants hsc;

    float3 patchCenter = (patch[0].PosW + patch[1].PosW + patch[2].PosW) / 3.0f;
    float distanceToCamera = length(patchCenter - gEyePosW);

    const float nearDist = 0.1f;
    const float farDist = 5.0f;
    float t = saturate((distanceToCamera - nearDist) / (farDist - nearDist));
    float tessFactor = lerp(14.0f, 1.0f, t);
    tessFactor = clamp(tessFactor, 1.0f, 14.0f);

    hsc.EdgeTess[0] = tessFactor;
    hsc.EdgeTess[1] = tessFactor;
    hsc.EdgeTess[2] = tessFactor;
    hsc.InsideTess = tessFactor;

    return hsc;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HS_Constants")]
ControlPoint HS_Main(InputPatch<ControlPoint, 3> patch, uint cpId : SV_OutputControlPointID)
{
    return patch[cpId];
}

struct DSOut
{
    float4 PosH : SV_POSITION;
};

[domain("tri")]
DSOut DS_Shadow(HSConstants hsc, float3 bary : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch)
{
    DSOut outV;

    float3 posW = bary.x * patch[0].PosW + bary.y * patch[1].PosW + bary.z * patch[2].PosW;
    float3 normalW = normalize(bary.x * patch[0].NormalW + bary.y * patch[1].NormalW + bary.z * patch[2].NormalW);
    float2 texC = bary.x * patch[0].TexC + bary.y * patch[1].TexC + bary.z * patch[2].TexC;

    float height = gDisplacementMap.SampleLevel(gLinearWrap, texC, 0.0f).r;
    float displacement = (height - 0.5f) * 2.0f;
    float dispStrength = (gObjectPadding.y > 0.0f) ? gObjectPadding.y : 0.2f;

    if (gWaveParams.w > 0.5f)
    {
        float3 sphereDir = normalize(posW - gObjectCenter.xyz);
        float verticalCoord = 1.0f - saturate(sphereDir.y * 0.5f + 0.5f);
        float distToWave = abs(verticalCoord - gWaveParams.x);
        float waveBand = 1.0f - smoothstep(0.0f, max(gWaveParams.z, 1e-4f), distToWave);
        displacement += waveBand * gWaveParams.y;
    }

    posW += normalW * (displacement * dispStrength);
    outV.PosH = mul(float4(posW, 1.0f), gLightViewProj);
    return outV;
}
