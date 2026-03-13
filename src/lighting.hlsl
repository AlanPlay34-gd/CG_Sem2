Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gPositionMap : register(t2);
SamplerState gSampler : register(s0);

cbuffer cbLighting : register(b0)
{
    float3 gLightPos;
    float gLightIntensity;
    float3 gLightColor;
    float gLightRange;
    float3 gLightDir;
    float gSpotAngle;
    float3 gAmbientColor;
    int gLightType;
    float3 gCameraPos;
    float padding;
};

// Константы для типов света
static const int LIGHT_AMBIENT = 0;
static const int LIGHT_DIRECTIONAL = 1;
static const int LIGHT_POINT = 2;
static const int LIGHT_SPOT = 3;

struct VSInput
{
    uint vertexId : SV_VertexID;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

PSInput VS(VSInput vin)
{
    PSInput vout;
    float2 texCoord = float2((vin.vertexId << 1) & 2, vin.vertexId & 2);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 albedo = gAlbedoMap.Sample(gSampler, pin.TexC);
    float4 normalData = gNormalMap.Sample(gSampler, pin.TexC);
    float4 positionData = gPositionMap.Sample(gSampler, pin.TexC);

    float3 worldPos = positionData.xyz;
    float3 normal = normalize(normalData.xyz);

    // Если позиция нулевая (фон), ничего не добавляем
    if (length(worldPos) < 0.001f)
        return float4(0, 0, 0, 0);

    float3 result = float3(0, 0, 0);

    // Расчет освещения в зависимости от типа
    if (gLightType == LIGHT_AMBIENT)
    {
        result = albedo.rgb * gAmbientColor;
    }
    if (gLightType == LIGHT_DIRECTIONAL)
    {
        float3 lightDir = normalize(-gLightDir);
        float diff = max(dot(normal, lightDir), 0.0f);
        result = diff * gLightColor * gLightIntensity * albedo.rgb;
    }
    if (gLightType == LIGHT_POINT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float attenuation = 1.0f - saturate(distance / gLightRange);
        attenuation = attenuation * attenuation;

        float diff = max(dot(normal, lightDir), 0.0f);
        result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation;
    }
    if (gLightType == LIGHT_SPOT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);

        float3 spotDir = normalize(gLightDir);
        float cosAngle = dot(lightDir, spotDir);
        float cosCone = cos(gSpotAngle / 2.0f);

        if (cosAngle > cosCone)
        {
            float attenuation = 1.0f - saturate(distance / gLightRange);
            attenuation = attenuation * attenuation;

            float spotFactor = saturate((cosAngle - cosCone) / (1.0f - cosCone));
            float diff = max(dot(normal, lightDir), 0.0f);
            result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation * spotFactor;
        }
    }

    return float4(result, 0.0f);
}