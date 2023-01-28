#include "Common.hlsl"

// #DXR Extra - Another ray type
struct ShadowHitInfo
{
    bool isHit;
    float3 color;
};

struct STriVertex {
    float3 vertex;
    float3 normal;
    float2 textcoord;
    float4 color;
};

 struct InstanceProperties 
 { 
    float4x4 objectToWorld;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<InstanceProperties> instanceProperties : register(t2);

// #DXR Extra - Another ray type
// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t3);

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attributes) 
{
    float3 barycentrics = float3(
    1.0f - attributes.bary.x - attributes.bary.y,
    attributes.bary.x, attributes.bary.y);

    uint vertexId = 3 * PrimitiveIndex();

    float4 hitColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (InstanceID() < 3) 
    {
       hitColor = BTriVertex[vertexId + 0].color * barycentrics.x + 
                  BTriVertex[vertexId + 1].color * barycentrics.y + 
                  BTriVertex[vertexId + 2].color * barycentrics.z;
    }

    hitColor.w = RayTCurrent();
    payload.colorAndDistance = hitColor;
}

// #DXR Extra: Per-Instance Data
[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attributes)
{ 
    float3 barycentrics = float3(1.0f - attributes.bary.x - attributes.bary.y, attributes.bary.x, attributes.bary.y); 
    float3 hitColor = float3(0.9f, 0.9f, 0.9f);
    payload.colorAndDistance = float4(hitColor, RayTCurrent());

    uint vertexId = 3 * PrimitiveIndex();

    payload.normal = float4(BTriVertex[vertexId + 0].normal.xyz * barycentrics.x + 
                            BTriVertex[vertexId + 1].normal.xyz * barycentrics.y + 
                            BTriVertex[vertexId + 2].normal.xyz * barycentrics.z,
                            0.0f);

    float3 lightDirection = float3(-0.4f, -0.6f, -0.9f);

    float3 normal = normalize(payload.normal.xyz);

    float diffuse = max(0.0f, dot(normal, -lightDirection));

    payload.colorAndDistance = float4(hitColor * diffuse, RayTCurrent());
    // payload.colorAndDistance = float4(normal, RayTCurrent());

    float3 lightPosition = float3(2.0f, 2.0f, -2.0f);

    // Find the world - space hit position
    float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    lightDirection = normalize(lightPosition - worldOrigin);

    // Fire a shadow ray. The direction is hard-coded here, but can be fetched
    // from a constant buffer
    RayDesc ray;
    ray.Origin = worldOrigin;
    ray.Direction = lightDirection;
    ray.TMin = 0.01f;
    ray.TMax = 100000.0f;

    bool hit = true;

    // Initialize the ray payload
    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = false;

    // Trace the ray
    TraceRay(
        // Parameter name: AccelerationStructure
        // Acceleration structure
        SceneBVH,

        // Parameter name: RayFlags
        // Flags can be used to specify the behavior upon hitting a surface 
        RAY_FLAG_NONE,

        // Parameter name: InstanceInclusionMask
        // Instance inclusion mask, which can be used to mask out some geometry to 
        // this ray by and-ing the mask with a geometry mask. The 0xFF flag then 
        // indicates no geometry will be masked
        0xFF,

        // Parameter name: RayContributionToHitGroupIndex
        // Depending on the type of ray, a given object can have several hit 
        // groups attached (ie. what to do when hitting to compute regular 
        // shading, and what to do when hitting to compute shadows). Those hit 
        // groups are specified sequentially in the SBT, so the value below 
        // indicates which offset (on 4 bits) to apply to the hit groups for this 
        // ray. In this sample we only have one hit group per object, hence an 
        // offset of 0. 
        1,

        // Parameter name: MultiplierForGeometryContributionToHitGroupIndex
        // The offsets in the SBT can be computed from the object ID, its instance 
        // ID, but also simply by the order the objects have been pushed in the 
        // acceleration structure. This allows the application to group shaders in 
        // the SBT in the same order as they are added in the AS, in which case 
        // the value below represents the stride (4 bits representing the number 
        // of hit groups) between two consecutive objects. 
        0,

        // Parameter name: MissShaderIndex
        // Index of the miss shader to use in case several consecutive miss 
        // shaders are present in the SBT. This allows to change the behavior of 
        // the program when no geometry have been hit, for example one to return a 
        // sky color for regular rendering, and another returning a full 
        // visibility value for shadow rays. This sample has only one miss shader, 
        // hence an index 0
        1,

        // Ray information to trace 
        ray,
         
        // Payload associated to the ray, which will be used to communicate 
        // between the hit/miss shaders and the raygen
        shadowPayload);

        float factor = shadowPayload.isHit ? 0.3f : 1.0f;

        payload.colorAndDistance.rgb *= factor;
        // payload.colorAndDistance.rgb = shadowPayload.color;
}

[shader("closesthit")]
void ModelClosestHit(inout HitInfo payload, Attributes attributes)
{ 
    float3 barycentrics = float3(1.0f - attributes.bary.x - attributes.bary.y, attributes.bary.x, attributes.bary.y); 
    float3 hitColor = float3(0.9f, 0.9f, 0.9f);
    payload.colorAndDistance = float4(hitColor, RayTCurrent());

    uint vertexId = 3 * PrimitiveIndex();

    payload.normal = float4(BTriVertex[indices[vertexId + 0]].normal.xyz * barycentrics.x + 
                            BTriVertex[indices[vertexId + 1]].normal.xyz * barycentrics.y + 
                            BTriVertex[indices[vertexId + 2]].normal.xyz * barycentrics.z,
                            0.0f);

    float3 lightDirection = float3(-0.4f, -0.6f, -0.9f);

    float4 normal = float4(payload.normal.xyz, 0.0f);

    normal = mul(instanceProperties[InstanceID()].objectToWorld, normal);

    normal = normalize(normal);

    float diffuse = max(0.0f, dot(normal.xyz, -lightDirection));

    float3 ambient = float3(0.1f, 0.1f, 0.1f);

    payload.colorAndDistance = float4(ambient + hitColor * diffuse, RayTCurrent());
    // payload.colorAndDistance = float4(normal, RayTCurrent());
}