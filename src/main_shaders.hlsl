//=====================================================================
// main_shader.hlsl - Geometry pass with tessellation and displacement
//=====================================================================

// Textures
Texture2D gDiffuseMap      : register(t0);
Texture2D gNormalMap       : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gSampler      : register(s0);

// Constant buffers
cbuffer cbObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4x4 gTextureTransform;
    float    gTotalTime;
    float3   gPadding;
};

cbuffer cbPass : register(b1)
{
    float4x4 gInvViewProj;
    float3   gEyePosW;
    float    gPadding1;
    float4   gAmbientColor;
};

// Vertex shader (used for non-tessellated meshes, also as control point input)
struct VS_IN
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL: TANGENT;
    float3 BinormalL: BINORMAL;
    float2 TexC    : TEXCOORD;
};

struct VS_OUT
{
    float3 PosW     : POSITION;
    float3 NormalW  : NORMAL;
    float3 TangentW : TANGENT;
    float3 BinormalW: BINORMAL;
    float2 TexC     : TEXCOORD;
};

VS_OUT VS(VS_IN vin)
{
    VS_OUT vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3)gWorld));
    vout.TangentW = normalize(mul(vin.TangentL, (float3x3)gWorld));
    vout.BinormalW = normalize(mul(vin.BinormalL, (float3x3)gWorld));
    float2 transformedTex = vin.TexC * gTextureTransform[0].xy + gTextureTransform[2].xy;
    vout.TexC = transformedTex;
    return vout;
}

// Hull Shader (tessellation factors)
struct HS_CONSTANT_OUT
{
    float EdgeTessFactor[3] : SV_TessFactor;
    float InsideTessFactor  : SV_InsideTessFactor;
};

struct HS_OUT
{
    float3 PosW     : POSITION;
    float3 NormalW  : NORMAL;
    float3 TangentW : TANGENT;
    float3 BinormalW: BINORMAL;
    float2 TexC     : TEXCOORD;
};

// Distance adaptive tessellation
#define MAX_TESS_FACTOR 8.0f
#define MIN_TESS_FACTOR 1.0f
#define MAX_TESS_DISTANCE 50.0f
#define MIN_TESS_DISTANCE 5.0f

HS_CONSTANT_OUT HSConst(InputPatch<VS_OUT, 3> ip, uint pid : SV_PrimitiveID)
{
    HS_CONSTANT_OUT output;

    // Compute center of patch in world space
    float3 center = (ip[0].PosW + ip[1].PosW + ip[2].PosW) / 3.0f;
    float distToCamera = length(center - gEyePosW);

    // Tessellation factor based on distance
    float t = saturate((distToCamera - MIN_TESS_DISTANCE) / (MAX_TESS_DISTANCE - MIN_TESS_DISTANCE));
    float tessFactor = lerp(MAX_TESS_FACTOR, MIN_TESS_FACTOR, t);

    output.EdgeTessFactor[0] = tessFactor;
    output.EdgeTessFactor[1] = tessFactor;
    output.EdgeTessFactor[2] = tessFactor;
    output.InsideTessFactor = tessFactor;

    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HSConst")]
HS_OUT HS(InputPatch<VS_OUT, 3> ip, uint i : SV_OutputControlPointID)
{
    return ip[i];
}

// Domain Shader
struct DS_OUT
{
    float4 PosH     : SV_POSITION;
    float3 PosW     : POSITION;
    float3 NormalW  : NORMAL;
    float3 TangentW : TANGENT;
    float3 BinormalW: BINORMAL;
    float2 TexC     : TEXCOORD;
};

[domain("tri")]
DS_OUT DS(HS_CONSTANT_OUT input, float3 bary : SV_DomainLocation, const OutputPatch<HS_OUT, 3> patch)
{
    DS_OUT output;

    // Interpolate attributes
    float3 posW = bary.x * patch[0].PosW + bary.y * patch[1].PosW + bary.z * patch[2].PosW;
    float3 normalW = bary.x * patch[0].NormalW + bary.y * patch[1].NormalW + bary.z * patch[2].NormalW;
    float3 tangentW = bary.x * patch[0].TangentW + bary.y * patch[1].TangentW + bary.z * patch[2].TangentW;
    float3 binormalW = bary.x * patch[0].BinormalW + bary.y * patch[1].BinormalW + bary.z * patch[2].BinormalW;
    float2 texC = bary.x * patch[0].TexC + bary.y * patch[1].TexC + bary.z * patch[2].TexC;

    // Sample displacement map
    float displacement = gDisplacementMap.SampleLevel(gSampler, texC, 0).r;

    // Scale and bias displacement
    float dispStrength = 0.4f; // можно вынести в константы, здесь для примера
    float offset = displacement * dispStrength;

    // Displace along normal
    posW += normalW * ((displacement - 0.5f) * 4.0f);


    // Transform to clip space
    output.PosH = mul(float4(posW, 1.0f), gWorldViewProj);
    output.PosW = posW;
    output.NormalW = normalize(normalW);
    output.TangentW = normalize(tangentW);
    output.BinormalW = normalize(binormalW);
    output.TexC = texC;

    return output;
}

// Pixel Shader (output to G-buffer)
struct PS_OUT
{
    float4 Albedo  : SV_Target0;
    float4 Normal  : SV_Target1;
    float  Depth   : SV_Target2;
};

PS_OUT PS(DS_OUT pin)
{
    PS_OUT pout;

    float4 albedo = gDiffuseMap.Sample(gSampler, pin.TexC);
    float4 normalFromMap = gNormalMap.Sample(gSampler, pin.TexC);
    float3 tangentNormal = normalFromMap.xyz * 2.0f - 1.0f;

    // Transform tangent space normal to world space
    float3x3 TBN = float3x3(pin.TangentW, pin.BinormalW, pin.NormalW);
    float3 worldNormal = normalize(mul(tangentNormal, TBN));

    pout.Albedo = albedo;
    pout.Normal = float4(worldNormal, 1.0f);
    pout.Depth = pin.PosH.z / pin.PosH.w; // NDC depth

    return pout;
}