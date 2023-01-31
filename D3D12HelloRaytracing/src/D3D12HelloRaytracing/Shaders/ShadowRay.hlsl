#include "Common.hlsl"

[shader("anyhit")]
void ShadowAnyHit(inout ShadowHitInfo payload, BuiltInTriangleIntersectionAttributes attributes)
{
    IgnoreHit();
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo hitInfo, Attributes attributes)
{
    hitInfo.isVisible = false;
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo hitInfo : SV_RayPayload)
{
    hitInfo.isVisible = true;
}

