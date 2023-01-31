#include "Common.hlsl"

TextureCube environmentTexture : register(t0);
SamplerState textureSampler : register(s0);

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dimentions = float2(DispatchRaysDimensions().xy);

    float ramp = launchIndex.y / dimentions.y;

    float3 textureColor = environmentTexture.SampleLevel(textureSampler, WorldRayDirection(), 0).rgb;

    // payload.colorAndDistance = float4(0.0f, 0.2f, 0.7f - 0.3f * ramp, -1.f);
    payload.colorAndDistance = float4(textureColor, 1.0f);
}