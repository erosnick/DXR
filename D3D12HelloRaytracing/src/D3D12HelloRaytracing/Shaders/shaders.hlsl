//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// #DXR Extra: Perspective Camera
cbuffer CameraParams : register(b0)
{ 
    float4x4 view; 
    float4x4 projection;
}

cbuffer Transform : register(b1)
{ 
    float4x4 model;
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput result;

    // #DXR Extra: Perspective Camera 
    float4 pos = float4(input.position, 1.0f);
    pos = mul(model, pos);
    pos = mul(view, pos);
    pos = mul(projection, pos);
    result.position = pos;
    result.normal = mul(model, float4(input.normal, 0.0f));
    result.color = input.color; 
    
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 ambient = float3(0.1f, 0.1f, 0.1f);
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(float3(-0.4f, -0.6f, 0.9f));

    float diffuse = max(0.0f, dot(normal, -lightDirection));

    return float4(ambient * 0.1f + input.color.rgb * diffuse * 0.9f, 1.0f);
    // return float4(float3(normal), 1.0f);
}
