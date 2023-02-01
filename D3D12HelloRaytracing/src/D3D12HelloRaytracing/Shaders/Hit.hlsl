#include "Common.hlsl"

struct STriVertex {
    float3 vertex;
    float3 normal;
    float2 textcoord;
    float4 color;
};

 struct InstanceProperties 
 { 
    float4x4 objectToWorld;
    int hasTexture;
    float3 padding;
};

StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices : register(t1);
StructuredBuffer<InstanceProperties> instanceProperties : register(t2);

// #DXR Extra - Another ray type
// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure SceneBVH : register(t3);

// TextureCube environmentTexture : register(t4);
Texture2D texture1 : register(t4);
Texture2D texture2 : register(t5);
Texture2D texture3 : register(t6);
Texture2D texture4 : register(t7);
SamplerState textureSampler1 : register(s0);
SamplerState textureSampler2 : register(s1);

// Logically, the ray assumes it is occluded unless the miss
// shader executes, when we definitively know the ray is unoccluded. This allows us to
// avoid execution of closest-hit shaders (RAY_FLAG_SKIP_CLOSEST_HIT_SHADER)
// and to stop after any hit where occlusion occurs (RAY_FLAG_ACCEPT_FIRST_HIT_
// AND_END_SEARCH).
bool ShadowRay(float3 origin, float3 direction, float minT, float maxT)
{
    RayDesc ray = { origin, minT, direction, maxT };
    ShadowHitInfo payload;
    payload.isVisible = false;

    // Trace the ray
    TraceRay(
        // Parameter name: AccelerationStructure
        // Acceleration structure
        SceneBVH,

        // Parameter name: RayFlags
        // Flags can be used to specify the behavior upon hitting a surface 
        (RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH),

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
        payload);
    return payload.isVisible;
}

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

    payload.normal = float3(BTriVertex[vertexId + 0].normal.xyz * barycentrics.x + 
                            BTriVertex[vertexId + 1].normal.xyz * barycentrics.y + 
                            BTriVertex[vertexId + 2].normal.xyz * barycentrics.z);

    float3 lightDirection = float3(-0.4f, -0.6f, 0.9f);

    float3 normal = normalize(payload.normal);

    float diffuse = max(0.0f, dot(normal, -lightDirection));

    float3 diffuseColor = hitColor * diffuse;

    payload.colorAndDistance = float4(diffuseColor, RayTCurrent());
    // payload.colorAndDistance = float4(normal, RayTCurrent());

    float3 finalColor;

    if (payload.depth < 1)
    {
        float3 lightPosition = float3(2.0f, 2.0f, -2.0f);

        // Find the world - space hit position
        float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

        lightDirection = normalize(lightPosition - worldOrigin);

        // Fire a shadow ray. The direction is hard-coded here, but can be fetched
        // from a constant buffer
        bool isVisible = ShadowRay(worldOrigin, lightDirection, 0.01f, 100000.0f);

        RayDesc reflectedRay;
        reflectedRay.Origin = worldOrigin;
        reflectedRay.Direction = reflect(WorldRayDirection(), normal);
        reflectedRay.TMin = 0.01f;
        reflectedRay.TMax = 100000.0f;
        
        HitInfo reflectionPayload;
        reflectionPayload.depth = payload.depth + 1;

        // Trace the reflection ray
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
            0,

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
            0,

            // Parameter name: Ray
            // Ray information to trace
            reflectedRay,

            // Parameter name: Payload
            // Payload associated to the ray, which will be used to communicate
            // between the hit/miss shaders and the raygen
            reflectionPayload);

        float factor = isVisible ? 1.0f : 0.3f;

        finalColor = diffuseColor * factor + reflectionPayload.colorAndDistance.rgb * 0.25f;
    }
    else
    {
        finalColor = diffuseColor;
    }

    payload.colorAndDistance = float4(finalColor, RayTCurrent());
}

[shader("closesthit")]
void ModelClosestHit(inout HitInfo payload, Attributes attributes)
{ 
    float3 barycentrics = float3(1.0f - attributes.bary.x - attributes.bary.y, attributes.bary.x, attributes.bary.y); 
    float3 hitColor = float3(0.9f, 0.9f, 0.9f);
    payload.colorAndDistance = float4(hitColor, RayTCurrent());

    uint vertexId = 3 * PrimitiveIndex();

    payload.normal = float3(BTriVertex[indices[vertexId + 0]].normal.xyz * barycentrics.x + 
                            BTriVertex[indices[vertexId + 1]].normal.xyz * barycentrics.y + 
                            BTriVertex[indices[vertexId + 2]].normal.xyz * barycentrics.z);

                            
    float3 position = float3(BTriVertex[indices[vertexId + 0]].vertex.xyz * barycentrics.x + 
                             BTriVertex[indices[vertexId + 1]].vertex.xyz * barycentrics.y + 
                             BTriVertex[indices[vertexId + 2]].vertex.xyz * barycentrics.z);

    float2 texcoord = float2(BTriVertex[indices[vertexId + 0]].textcoord.xy * barycentrics.x + 
                             BTriVertex[indices[vertexId + 1]].textcoord.xy * barycentrics.y + 
                             BTriVertex[indices[vertexId + 2]].textcoord.xy * barycentrics.z);

    texcoord *= 2.0f;

    float3 lightDirection = float3(-0.4f, -0.6f, 0.9f);

    float3 normal = payload.normal;

    normal = mul(instanceProperties[InstanceID()].objectToWorld, float4(normal, 0.0f)).xyz;

    normal = normalize(normal);

    float diffuse = max(0.0f, dot(normal.xyz, -lightDirection));

    float3 ambient = float3(0.1f, 0.1f, 0.1f);

    int2 coord = floor(texcoord * 512.0f);

    // float4 textureColor = environmentTexture.Sample(textureSampler, position);
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    if (instanceProperties[InstanceID()].hasTexture)
    {
       textureColor = texture2.SampleLevel(textureSampler2, texcoord, 0);
        // textureColor = texture2.Load(int3(coord, 0));
    }

    payload.colorAndDistance = float4(ambient * 0.1f + hitColor * diffuse * textureColor.rgb * 0.9f, RayTCurrent());
    // payload.colorAndDistance = float4(float3(texcoord, 0.0f), RayTCurrent());
}