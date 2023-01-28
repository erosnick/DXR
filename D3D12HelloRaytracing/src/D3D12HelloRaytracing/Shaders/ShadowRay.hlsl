// #DXR Extra - Another ray type
// Ray payload for the shadow rays
struct ShadowHitInfo
{
    bool isHit;
    float3 color;
};

struct Attributes
{
    float2 uv;
};

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo hitInfo, Attributes attributes)
{
    hitInfo.isHit = true;
    hitInfo.color = float3(0.0f, 1.0f, 0.0f);
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo hitInfo : SV_RayPayload)
{
    hitInfo.isHit = false;
    hitInfo.color = float3(1.0f, 0.0f, 0.0f);
}

