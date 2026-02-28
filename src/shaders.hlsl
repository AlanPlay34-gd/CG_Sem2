Texture2D gDiffuseMap : register(t0);
Texture2D gSecondaryMap : register(t1);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorldViewProj;
    float4 mUVTransform;  // x = scaleU, y = scaleV, z = offsetU, w = offsetV
    float4 mChessboardParams;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
    float3 WorldPos : POSITION;
};

PSInput VS(VSInput vin)
{
    PSInput vout;
    // Getting screen coords
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);
    // UV transform
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    vout.WorldPos = vin.Pos;

    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float tileSize = mChessboardParams.x;

    float4 finalColor = gDiffuseMap.Sample(gSampler, pin.TexC);
    float4 tex2 = gSecondaryMap.Sample(gSampler,pin.TexC);

    float2 chessPos = pin.TexC / tileSize;
    int2 cell = floor(chessPos);
    int isEven = (cell.x + cell.y) % 2;


    if (isEven == 1)
    {
        finalColor = tex2;
    }

    return finalColor;
}