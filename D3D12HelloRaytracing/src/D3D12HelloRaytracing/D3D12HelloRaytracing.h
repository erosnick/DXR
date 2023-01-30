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

#pragma once

#include "DXSample.h"

#include <dxcapi.h>
#include <vector>
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

#include "Model.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

#define ROUND_UP(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) &~ (B - 1)))

struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 texcoord;
	XMFLOAT4 color;
};

struct ConstantBuffer
{
    XMFLOAT4X4 model;
};

struct InstanceProperties
{
    XMMATRIX objectToWorld;
};

class D3D12HelloRaytracing : public DXSample
{
public:
    D3D12HelloRaytracing(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

    virtual void OnKeyUp(uint8_t key) override;

private:
    static const UINT FrameCount = 2;

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device5> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList4> m_commandList;
    UINT m_rtvDescriptorSize;

    // App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    uint32_t frameCount;

    bool m_raster = true;

    void LoadPipeline();
    void LoadAssets();
    void PopulateCommandList();
    void WaitForPreviousFrame();
    void CheckRayTracingSupport();

    // #DXR 
private:

    struct AccelerationStructureBuffers
    {
        // Scratch memory for AS builder
        ComPtr<ID3D12Resource> scratch;

        // Where the AS is
        ComPtr<ID3D12Resource> result;
    
        // Hold the matrices of the instances
        ComPtr<ID3D12Resource> instanceDesc;
    };

	/// Create the acceleration structure of an instance
    ///
    /// \param vVertexBuffers : pair of buffer and vertex count
    /// \return AccelerationStructureBuffers for TLAS
    AccelerationStructureBuffers CreateBottomLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& vertexBuffers);

	/// Create the acceleration structure of an instance
    ///
    /// \param     vVertexBuffers : pair of buffer and vertex count
    /// \return    AccelerationStructureBuffers for TLAS
	AccelerationStructureBuffers CreateBottomLevelAS(
		const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& vertexBuffers,
		const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& indexBuffers);

	/// Create the main acceleration structure that holds
    /// all instances of the scene
    /// \param instances : pair of BLAS and transform
    void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances);

    /// Create all acceleration structures, bottom and top
    void CreateAccelerationStructures();

    ComPtr<ID3D12RootSignature> CreateRayGenRootSignature();
    ComPtr<ID3D12RootSignature> CreateMissRootSignature();
    ComPtr<ID3D12RootSignature> CreateHitRootSignature();

    void CreateRayTracingPipeline();
	void CreateRayTracingOutputBuffer();
	void CreateShaderResourceHeap();
	void CreateShaderBindingTable();

    // Storage for the bottom level AS
    ComPtr<ID3D12Resource> m_bottomLevelAS;
    nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
    AccelerationStructureBuffers m_topLevelASBuffers;
    std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

    ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;

    // Ray Tracing pipeline state
    ComPtr<ID3D12StateObject> m_rayTracingStateObject;

    // Ray Tracing pipeline state properties, retaining the shader identifiers
    // to use in the Shader Binding Table
    ComPtr<ID3D12StateObjectProperties> m_rayTracingStateObjectProperties;

    ComPtr<ID3D12RootSignature> m_rayGenRootSignature;
    ComPtr<ID3D12RootSignature> m_missRootSignature;
    ComPtr<ID3D12RootSignature> m_hitRootSignature;
	ComPtr<ID3D12Resource> m_outputResource;
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;

	// #DXR Extra: Per-Instance Data
    void CreatePlaneVB();
	ComPtr<ID3D12Resource> m_planeVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_planeVertexBufferView;

	// #DXR Extra: Per-Instance Data
	void CreateGlobalConstantBuffer();
	ComPtr<ID3D12Resource> m_globalConstantBuffer;

	// #DXR Extra: Perspective Camera
	void CreateCameraBuffer();
	void UpdateCameraBuffer();
	ComPtr< ID3D12Resource > m_cameraBuffer;
	ComPtr< ID3D12DescriptorHeap > m_constantbufferHeap;
	uint32_t m_cameraBufferSize = 0;
    uint8_t* m_cameraBufferData = nullptr;

	void CreateConstantBuffer();
	void UpdateConstantBuffer();
	std::vector<ComPtr<ID3D12Resource>> m_constantBuffers;
	uint32_t m_constantBufferSize = 0;
    std::vector<XMMATRIX> transforms;

    std::vector<ConstantBuffer*> constantBufferDatas;

    void createModelVertexBuffer();
    void createModelIndexBuffer();
	ComPtr<ID3D12Resource> m_modelVertexBuffer;
	ComPtr<ID3D12Resource> m_modelIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_modelVertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_modelIndexBufferView;
    DXModel model;

	// #DXR Extra: Depth Buffering
	void CreateDepthBuffer();
	ComPtr< ID3D12DescriptorHeap > m_dsvHeap;
	ComPtr< ID3D12Resource > m_depthStencil;

	void CreateInstancePropertiesBuffer();
	void UpdateInstancePropertiesBuffer();
    ComPtr<ID3D12Resource> m_instanceProperties;
	InstanceProperties* m_instancePropertiesBufferData;

	// #DXR Extra - Another ray type
	ComPtr<IDxcBlob> m_shadowLibrary;
	ComPtr<ID3D12RootSignature> m_shadowRootSignature;

    void loadSkyboxTexture();
    void createSkyboxDescriptorHeap();
    void createSkyboxShaderResourceView();
    void createSkyboxSamplerDescriptorHeap();
    void createSkyboxSampler();
	ComPtr<ID3D12Heap> m_skyboxUploadHeap;
	ComPtr<ID3D12Resource> m_skyboxTexture;
	ComPtr<ID3D12Resource> m_skyboxTextureUploadBuffer;
	ComPtr<ID3D12DescriptorHeap> m_skyboxDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_skyboxSamplerDescriptorHeap;
};
