struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD;
};

cbuffer CameraParams : register(b0)
{ 
    float4x4 view; 
    float4x4 projection;
}

cbuffer Transform : register(b1)
{
    float4x4 model;
};

TextureCube environmentTexture : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;

    result.position = mul(model, float4(input.position, 1.0f));
    float4x4 viewWithoutTranslate = view;
    viewWithoutTranslate[0][3] = 0.0f;
    viewWithoutTranslate[1][3] = 0.0f;
    viewWithoutTranslate[2][3] = 0.0f;
    result.position = mul(viewWithoutTranslate,  result.position);
    result.position = mul(projection,  result.position);
    result.texcoord = input.position;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 color = environmentTexture.Sample(textureSampler, input.texcoord);
    // color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    // color = float4(input.texcoord, 1.0f);
    return color;
}