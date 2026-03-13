// Текстуры для материалов
Texture2D gDiffuseMap : register(t0);
Texture2D gSecondaryMap : register(t1);
SamplerState gSampler : register(s0);

// Constant buffer
cbuffer cbPerObject : register(b0)
{
    float4x4 mWorldViewProj;
    float4 mUVTransform;
    float4 mChessboardParams;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

// Выход из вершинного шейдера (передается в пиксельный)
struct VSOutput
{
    float4 PosH : SV_POSITION;      // Позиция в экранных координатах
    float3 WorldPos : POSITION0;     // Мировая позиция
    float3 Normal : NORMAL0;          // Нормаль
    float2 TexC : TEXCOORD0;          // UV координаты
};

// Выход из пиксельного шейдера (в G-буфер)
struct PSOutput
{
    float4 Albedo : SV_Target0;      // Альбедо
    float4 Normal : SV_Target1;       // Нормали
    float4 Position : SV_Target2;     // Позиция
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    // Позиция в экранных координатах
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);

    // Мировая позиция (для G-буфера)
    vout.WorldPos = vin.Pos;

    // Нормаль
    vout.Normal = normalize(vin.Normal);

    // UV с трансформацией
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

PSOutput PS(VSOutput pin) : SV_Target
{
    PSOutput pout;

    // Сэмплируем альбедо текстуру
    float4 albedo = gDiffuseMap.Sample(gSampler, pin.TexC);

    // Шахматный режим (как в вашем оригинале)
    float tileSize = mChessboardParams.x;
    float2 chessPos = pin.TexC / tileSize;
    int2 cell = floor(chessPos);
    int isEven = (cell.x + cell.y) % 2;

    float4 tex2 = gSecondaryMap.Sample(gSampler, pin.TexC);

    // Заполняем G-буфер
    pout.Albedo = (isEven == 1) ? tex2 : albedo;
    pout.Normal = float4(pin.Normal, 1.0f);
    pout.Position = float4(pin.WorldPos, 1.0f);

    return pout;
}