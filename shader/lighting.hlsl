Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gDepthTex : register(t2);
Texture2DArray gShadowTex : register(t3);
Texture2D gShadowPatternTex : register(t4);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowCmp : register(s1);
SamplerState gLinearWrap : register(s2);

cbuffer PassConstants : register(b0)
{
    float4x4 gView;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
};

struct LightData
{
    float3 Position;
    float Range;
    float3 Direction;
    float SpotAngle;
    float3 Color;
    float Intensity;
    uint Type;
    float3 Padding;
};

cbuffer LightConstants : register(b1)
{
    LightData gLight;
    uint gEnableAmbient;
    float3 gDummy;
};

cbuffer ShadowConstants : register(b2)
{
    float4x4 gShadowViewProj[4];
    float4 gCascadeSplits;
    float3 gShadowLightDir;
    float gShadowStrength;
    float gShadowDepthBias;
    float gShadowNormalBias;
    float2 gShadowPadding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS_Fullscreen(uint vid : SV_VertexID)
{
    VSOut vout;
    float2 pos[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    float2 uv[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
    vout.PosH = float4(pos[vid], 0, 1);
    vout.TexC = uv[vid];
    return vout;
}

float3 ReconstructWorldPos(float2 uv, float depth, float4x4 invViewProj)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(clip, invViewProj);
    return world.xyz / world.w;
}

float3 ComputeDirectional(float3 N, float3 albedo, LightData light)
{
    float3 L = normalize(-light.Direction);
    float ndotl = saturate(dot(N, L));
    return albedo * light.Color * (light.Intensity * ndotl);
}

float3 ComputePoint(float3 P, float3 N, float3 albedo, LightData light)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 L = toLight / max(dist, 1e-4f);
    float ndotl = saturate(dot(N, L));
    float atten = saturate(1.0f - dist / max(light.Range, 1e-4f));
    atten *= atten;

    return albedo * light.Color * (light.Intensity * ndotl * atten);
}

float3 ComputeSpot(float3 P, float3 N, float3 albedo, LightData light)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 L = toLight / max(dist, 1e-4f);
    float cone = dot(normalize(-light.Direction), L);
    float spot = smoothstep(light.SpotAngle, light.SpotAngle + 0.08f, cone);

    float ndotl = saturate(dot(N, L));
    float atten = saturate(1.0f - dist / max(light.Range, 1e-4f));
    atten *= atten;

    return albedo * light.Color * (light.Intensity * ndotl * atten * spot);
}

float SampleShadowCascade(uint cascade, float3 worldPos, float3 normalW, out float valid)
{
    float4 lightClip = mul(float4(worldPos, 1.0f), gShadowViewProj[cascade]);
    if (lightClip.w <= 1e-5f)
    {
        valid = 0.0f;
        return 1.0f;
    }

    float3 lightNdc = lightClip.xyz / lightClip.w;
    float2 shadowUv = lightNdc.xy * float2(0.5f, -0.5f) + 0.5f;

    if (shadowUv.x <= 0.0f || shadowUv.x >= 1.0f || shadowUv.y <= 0.0f || shadowUv.y >= 1.0f)
    {
        valid = 0.0f;
        return 1.0f;
    }

    if (lightNdc.z <= 0.0f || lightNdc.z >= 1.0f)
    {
        valid = 0.0f;
        return 1.0f;
    }

    float3 L = normalize(-gShadowLightDir);
    float ndotl = saturate(dot(normalW, L));
    float cascadeScale = 1.0f + 0.18f * (float)cascade;
    float bias = (gShadowDepthBias + (1.0f - ndotl) * gShadowNormalBias) * cascadeScale;
    float compareDepth = saturate(lightNdc.z - bias);

    const float texelSize = 1.0f / 2048.0f;
    float visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2((float)x, (float)y) * texelSize;
            visibility += gShadowTex.SampleCmpLevelZero(
                gShadowCmp,
                float3(shadowUv + offset, (float)cascade),
                compareDepth);
        }
    }

    valid = 1.0f;
    return visibility / 9.0f;
}

float ComputeShadowFactor(float3 worldPos, float3 normalW)
{
    float3 viewPos = mul(float4(worldPos, 1.0f), gView).xyz;
    float viewDepth = viewPos.z;

    if (viewDepth <= 0.0f || viewDepth > gCascadeSplits.w)
    {
        return 1.0f;
    }

    uint cascade = 0u;
    if (viewDepth > gCascadeSplits.x) cascade = 1u;
    if (viewDepth > gCascadeSplits.y) cascade = 2u;
    if (viewDepth > gCascadeSplits.z) cascade = 3u;

    float validCurrent = 0.0f;
    float shadow = SampleShadowCascade(cascade, worldPos, normalW, validCurrent);

    if (cascade < 3u)
    {
        float cascadeStart = (cascade == 0u) ? 0.0f : gCascadeSplits[cascade - 1u];
        float cascadeEnd = gCascadeSplits[cascade];
        float blendRange = max(0.9f, (cascadeEnd - cascadeStart) * 0.35f);
        float blendStart = cascadeEnd - blendRange;
        float blendFactor = saturate((viewDepth - blendStart) / blendRange);
        if (blendFactor > 0.0f)
        {
            float validNext = 0.0f;
            float nextShadow = SampleShadowCascade(cascade + 1u, worldPos, normalW, validNext);

            if (validCurrent < 0.5f && validNext > 0.5f)
            {
                shadow = nextShadow;
            }
            else if (validCurrent > 0.5f && validNext < 0.5f)
            {
                // keep current cascade shadow
            }
            else if (validCurrent > 0.5f && validNext > 0.5f)
            {
                shadow = lerp(shadow, nextShadow, blendFactor);
            }
        }
    }

    return shadow;
}

float3 ApplyShadowPattern(float3 litColor, float3 albedo, float3 worldPos, float shadowFactor)
{
    float shadowAmount = saturate((1.0f - shadowFactor) * gShadowStrength);
    if (shadowAmount <= 1e-4f)
    {
        return litColor;
    }

    // Distinct world-space UV for shadow overlay so it does not merge with floor UVs.
    float2 patternUv = worldPos.xz * 0.19f + float2(0.31f, 0.17f);
    float3 pattern = gShadowPatternTex.Sample(gLinearWrap, patternUv).rgb;
    float patternLuma = dot(pattern, float3(0.299f, 0.587f, 0.114f));

    // High contrast textured tint that remains semi-transparent.
    float3 patternColor = lerp(albedo * 0.08f, albedo * 0.98f, patternLuma);
    float patternAlpha = saturate(0.01f + shadowAmount * 0.20f);
    return lerp(litColor, patternColor, patternAlpha);
}

float4 PS_Lighting(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float3 albedo = gAlbedoTex.Sample(gLinearClamp, uv).rgb;
    float3 normalPacked = gNormalTex.Sample(gLinearClamp, uv).rgb;
    float depth = gDepthTex.Sample(gLinearClamp, uv).r;

    float3 normalW = normalize(normalPacked * 2.0f - 1.0f);
    float3 worldPos = ReconstructWorldPos(uv, depth, gInvViewProj);

    float3 color = float3(0.0f, 0.0f, 0.0f);

    if (gEnableAmbient != 0)
    {
        color = gAmbientColor.rgb * albedo;
    }
    else
    {
        if (gLight.Type == 0)
        {
            float3 direct = ComputeDirectional(normalW, albedo, gLight);
            float shadow = ComputeShadowFactor(worldPos, normalW);
            float3 lit = lerp(direct * (1.0f - gShadowStrength), direct, shadow);
            color = ApplyShadowPattern(lit, albedo, worldPos, shadow);
        }
        else if (gLight.Type == 1)
        {
            float3 lit = ComputePoint(worldPos, normalW, albedo, gLight);
            // Scene 5 has zero ambient by design: keep shadow pattern visible there
            // even when a fill point light is enabled.
            if ((gAmbientColor.x + gAmbientColor.y + gAmbientColor.z) < 1e-4f)
            {
                float shadow = ComputeShadowFactor(worldPos, normalW);
                lit = ApplyShadowPattern(lit, albedo, worldPos, shadow);
            }
            color = lit;
        }
        else if (gLight.Type == 2)
        {
            float3 lit = ComputeSpot(worldPos, normalW, albedo, gLight);
            if ((gAmbientColor.x + gAmbientColor.y + gAmbientColor.z) < 1e-4f)
            {
                float shadow = ComputeShadowFactor(worldPos, normalW);
                lit = ApplyShadowPattern(lit, albedo, worldPos, shadow);
            }
            color = lit;
        }
    }

    return float4(color, 0.0f);
}


