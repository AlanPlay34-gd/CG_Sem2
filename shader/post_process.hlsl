Texture2D gSceneColor : register(t0);
Texture2D gAlbedoTex : register(t1);
Texture2D gNormalTex : register(t2);
Texture2D gDepthTex : register(t3);
SamplerState gLinearClamp : register(s0);

cbuffer PostProcessConstants : register(b0)
{
    float gGamma;
    float gExposure;
    float gEnableChromaticAberration;
    float gEnableSobelEdges;

    float2 gScreenSize;
    float gChromaticStrength;
    float gEdgeStrength;

    float gEdgeThreshold;
    float gDepthEdgeStrength;
    float gDepthEdgeThreshold;
    float gPadding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS_Fullscreen(uint vid : SV_VertexID)
{
    VSOut vout;
    float2 pos[3] = { float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f) };
    float2 uv[3] = { float2(0.0f, 1.0f), float2(0.0f, -1.0f), float2(2.0f, 1.0f) };
    vout.PosH = float4(pos[vid], 0.0f, 1.0f);
    vout.TexC = uv[vid];
    return vout;
}

float3 ToneMapAndGamma(float3 hdrColor)
{
    float3 mapped = 1.0f - exp(-max(hdrColor, 0.0f) * max(gExposure, 0.0001f));
    return pow(saturate(mapped), 1.0f / max(gGamma, 0.0001f));
}

float3 SampleSceneChromatic(float2 uv)
{
    float3 color = gSceneColor.Sample(gLinearClamp, uv).rgb;

    float2 fromCenter = uv - 0.5f;
    float dist = length(fromCenter);
    float2 dir = (dist > 1e-4f) ? (fromCenter / dist) : float2(0.0f, 0.0f);
    float2 texel = 1.0f / max(gScreenSize, float2(1.0f, 1.0f));
    float radialAmount = saturate(dist * 2.8f);
    float2 offset = dir * texel * gChromaticStrength * radialAmount;

    float r = gSceneColor.Sample(gLinearClamp, uv + offset).r;
    float g = gSceneColor.Sample(gLinearClamp, uv).g;
    float b = gSceneColor.Sample(gLinearClamp, uv - offset).b;
    return (gEnableChromaticAberration > 0.5f) ? float3(r, g, b) : color;
}

float DisplayLuma(float2 uv)
{
    float3 color = ToneMapAndGamma(gSceneColor.Sample(gLinearClamp, uv).rgb);
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float SobelEdge(float2 uv)
{
    float2 texel = 1.0f / max(gScreenSize, float2(1.0f, 1.0f));

    float tl = DisplayLuma(uv + texel * float2(-1.0f, -1.0f));
    float tc = DisplayLuma(uv + texel * float2( 0.0f, -1.0f));
    float tr = DisplayLuma(uv + texel * float2( 1.0f, -1.0f));
    float ml = DisplayLuma(uv + texel * float2(-1.0f,  0.0f));
    float mr = DisplayLuma(uv + texel * float2( 1.0f,  0.0f));
    float bl = DisplayLuma(uv + texel * float2(-1.0f,  1.0f));
    float bc = DisplayLuma(uv + texel * float2( 0.0f,  1.0f));
    float br = DisplayLuma(uv + texel * float2( 1.0f,  1.0f));

    float gx = -tl - 2.0f * ml - bl + tr + 2.0f * mr + br;
    float gy = -tl - 2.0f * tc - tr + bl + 2.0f * bc + br;
    float edge = length(float2(gx, gy));
    return smoothstep(gEdgeThreshold, gEdgeThreshold + 0.12f, edge);
}

float SobelDepthEdge(float2 uv)
{
    float2 texel = 1.0f / max(gScreenSize, float2(1.0f, 1.0f));

    float tl = gDepthTex.Sample(gLinearClamp, uv + texel * float2(-1.0f, -1.0f)).r;
    float tc = gDepthTex.Sample(gLinearClamp, uv + texel * float2( 0.0f, -1.0f)).r;
    float tr = gDepthTex.Sample(gLinearClamp, uv + texel * float2( 1.0f, -1.0f)).r;
    float ml = gDepthTex.Sample(gLinearClamp, uv + texel * float2(-1.0f,  0.0f)).r;
    float mr = gDepthTex.Sample(gLinearClamp, uv + texel * float2( 1.0f,  0.0f)).r;
    float bl = gDepthTex.Sample(gLinearClamp, uv + texel * float2(-1.0f,  1.0f)).r;
    float bc = gDepthTex.Sample(gLinearClamp, uv + texel * float2( 0.0f,  1.0f)).r;
    float br = gDepthTex.Sample(gLinearClamp, uv + texel * float2( 1.0f,  1.0f)).r;

    float gx = -tl - 2.0f * ml - bl + tr + 2.0f * mr + br;
    float gy = -tl - 2.0f * tc - tr + bl + 2.0f * bc + br;
    float edge = length(float2(gx, gy));
    return smoothstep(gDepthEdgeThreshold, gDepthEdgeThreshold * 4.0f, edge);
}

float4 PS_PostProcess(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float3 hdrColor = SampleSceneChromatic(uv);
    float3 color = ToneMapAndGamma(hdrColor);

    if (gEnableSobelEdges > 0.5f)
    {
        float colorEdge = SobelEdge(uv);
        float depthEdge = SobelDepthEdge(uv) * gDepthEdgeStrength;
        float edge = max(colorEdge, depthEdge);
        float3 ink = float3(0.02f, 0.025f, 0.03f);
        color = lerp(color, ink, edge * gEdgeStrength);
    }

    return float4(color, 1.0f);
}
