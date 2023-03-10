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

#include "stdafx.h"
#include "D3D12HelloRaytracing.h"

#include "DDSTextureLoader12.h"

#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"

#include <chrono>

D3D12HelloRaytracing::D3D12HelloRaytracing(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0), frameCount(0),
    m_fenceEvent(nullptr),
    m_fenceValue(0),
    m_vertexBufferView{}
{
}

void D3D12HelloRaytracing::OnInit()
{
    LoadPipeline();
    LoadAssets();

    // Check the ray tracing capabilities of the device 
    CheckRayTracingSupport();

    // Setup the acceleration structures (AS) for ray tracing. When setting up 
    // geometry, each bottom-level AS has its own transform matrix. 
    CreateAccelerationStructures();

	// Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

	// Create the ray tracing pipeline, associating the shader code to symbol names
    // and to their root signatures, and defining the amount of memory carried by
    // rays (ray payload)
	CreateRayTracingPipeline(); // #DXR

	// #DXR Extra: Per-Instance Data
    // Create a constant buffers, with a color for each vertex of the triangle, for each
    // triangle instance
	//CreateGlobalConstantBuffer();

	// Allocate the buffer storing the ray tracing output, with the same dimensions
    // as the target image
	CreateRayTracingOutputBuffer(); // #DXR

	// #DXR Extra: Perspective Camera
    // Create a buffer to store the model view and perspective camera matrices
	CreateCameraBuffer();
    
    CreateConstantBuffer();

    CreateInstancePropertiesBuffer();

	// Create the buffer containing the ray tracing result (always output in a
	// UAV), and create the heap referencing the resources used by the ray tracing,
	// such as the acceleration structure
	CreateShaderResourceHeap(); // #DXR

	// Create the shader binding table and indicating which shaders
    // are invoked for each instance in the AS
    CreateShaderBindingTable();
}

// Load the rendering pipeline dependencies.
void D3D12HelloRaytracing::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    m_SRVCBVUAVDescriptorHandleIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support full screen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// #DXR Extra: Depth Buffering
    // The original sample does not support depth buffering, so we need to allocate a depth buffer,
    // and later bind it before rasterization
	CreateDepthBuffer();
}

// Load the sample assets.
void D3D12HelloRaytracing::LoadAssets()
{
	// #DXR Extra: Perspective Camera
    // The root signature describes which data is accessed by the shader. The camera matrices are held
    // in a constant buffer, itself referenced the heap. To do this we reference a range in the heap,
    // and use that range as the sole parameter of the shader. The camera buffer is associated in the
    // index 0, making it accessible in the shader in the b0 register.
    CD3DX12_ROOT_PARAMETER constantParameters[4]{};
    CD3DX12_DESCRIPTOR_RANGE ranges[4]{};

	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0);
	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0);

	constantParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);

    // Create an empty root signature.
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(_countof(constantParameters), constantParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"Shaders/Shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"Shaders/Shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

		// #DXR Extra: Depth Buffering
        // Add support for depth testing, using a 32-bit floating-point depth buffer
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Create the vertex buffer.
    {
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
            { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }

	// #DXR - Per Instance
    // Create a vertex buffer for a ground plane, similarly to the triangle definition above
	CreatePlaneVB();

    //model.load("Models/bunny.obj");
	//model.load("Models/dragon.obj");
	model.load("Models/cube.obj");
    skybox.load("Models/cube.obj");

    createModelVertexBuffer(model, m_modelVertexBufferView);
	createModelIndexBuffer(model, m_modelIndexBufferView);

	createModelVertexBuffer(skybox, m_skyboxVertexBufferView);
	createModelIndexBuffer(skybox, m_skyboxIndexBufferView);

	loadDDSTexture(L"Textures/WoodCrate01.dds", m_ModelTexture1);
	loadDDSTexture(L"Textures/bricks1.dds", m_ModelTexture2);
	loadDDSTexture(L"Textures/bricks2.dds", m_ModelTexture3);
	loadDDSTexture(L"Textures/bricks3.dds", m_ModelTexture4);
	loadDDSTexture(L"Textures/Day_1024.dds", m_skyboxTexture);

    createSkyboxSamplerDescriptorHeap();
    createSkyboxSampler();
    CreateSkyboxGraphicsPipelineState();
    CreateGlobalRootSignature();
}

// Update frame-based values.
void D3D12HelloRaytracing::OnUpdate()
{
    // #DXR Extra: Perspective Camera 
    UpdateCameraMovement(m_frameTime);
	UpdateCameraBuffer();
	UpdateConstantBuffer();
    UpdateInstancePropertiesBuffer();
}

// Render the scene.
void D3D12HelloRaytracing::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(0, 0));

    WaitForPreviousFrame();
}

void D3D12HelloRaytracing::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloRaytracing::OnKeyUp(uint8_t key)
{
    if (key == VK_SPACE)
    {
        m_raster = !m_raster;

        if (m_raster)
        {
            extraInfo = L" Rasterizer";
        }
        else
        {
            extraInfo = L" Raytracer";
        }
    }
}

void D3D12HelloRaytracing::OnMouseMove(WPARAM buttonState, float x, float y)
{
	auto xoffset = x - m_lastMousePosition.x;
	auto yoffset = m_lastMousePosition.y - y;

    if ((buttonState & MK_RBUTTON) != 0)
    {
        xoffset *= m_mouseSensitivity;
        yoffset *= m_mouseSensitivity;

        m_yaw += xoffset;
        m_pitch += yoffset;

        // make sure that when pitch is out of bounds, screen doesn't get flipped
        if (m_constrainPitch)
        {
            if (m_pitch > 89.0f)
                m_pitch = 89.0f;
            if (m_pitch < -89.0f)
                m_pitch = -89.0f;
        }
    }

    m_lastMousePosition.x = x;
    m_lastMousePosition.y = y;
}

void D3D12HelloRaytracing::OnMouseWheel(float offset)
{

}

void D3D12HelloRaytracing::OnMouseButtonDown(WPARAM buttonState, float x, float y)
{
	m_lastMousePosition.x = x;
	m_lastMousePosition.y = y;
}

void D3D12HelloRaytracing::OnMouseButtonUp(WPARAM buttonState, float x, float y)
{

}

void D3D12HelloRaytracing::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	
    // #DXR Extra: Depth Buffering
	// Bind the depth buffer as a render target
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // #DXR
    if (m_raster)
    {
        // #DXR Extra: Perspective Camera 
        std::vector<ID3D12DescriptorHeap*> heaps = { m_constantbufferHeap.Get(), m_skyboxSamplerDescriptorHeap.Get() };
        m_commandList->SetDescriptorHeaps(static_cast<uint32_t>(heaps.size()), heaps.data()); 
        
        // set the root descriptor table 0 to the constant buffer descriptor heap
        // b0 - Camera matrices
        m_commandList->SetGraphicsRootDescriptorTable( 0, m_constantbufferHeap->GetGPUDescriptorHandleForHeapStart());
	
        // Record commands.
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };

		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        // #DXR Extra: Depth Buffering 
        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Draw the skybox
		m_commandList->SetPipelineState(m_skyboxGraphicsPipelineState.Get());

		m_commandList->IASetVertexBuffers(0, 1, &m_skyboxVertexBufferView);
		m_commandList->IASetIndexBuffer(&m_skyboxIndexBufferView);

		auto skyboxDescriptorHandle = m_constantbufferHeap->GetGPUDescriptorHandleForHeapStart();

		skyboxDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize * 6;

		// ????????
		m_commandList->SetGraphicsRootDescriptorTable(1, skyboxDescriptorHandle);

		skyboxDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		// ????
		m_commandList->SetGraphicsRootDescriptorTable(2, skyboxDescriptorHandle);

		skyboxDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		auto skyboxSamplerDescriptorHandle = m_skyboxSamplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

		// ??????
		m_commandList->SetGraphicsRootDescriptorTable(3, skyboxSamplerDescriptorHandle);

		m_commandList->DrawIndexedInstanced(model.mesh.indexCount, 1, 0, 0, 0);

        m_commandList->SetPipelineState(m_pipelineState.Get());

		// #DXR Extra: Per-Instance Data
        // In a way similar to triangle rendering, rasterize the plane
		m_commandList->IASetVertexBuffers(0, 1, &m_planeVertexBufferView);

		auto constantBufferDescriptorHandle = m_constantbufferHeap->GetGPUDescriptorHandleForHeapStart();

        auto planeDescriptorHandle = constantBufferDescriptorHandle;

        // b1 - model matrix
        planeDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_commandList->SetGraphicsRootDescriptorTable(1, planeDescriptorHandle);

		m_commandList->DrawInstanced(6, 1, 0, 0);

		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        // Draw three triangles
		for (auto i = 0; i < transforms.size() - 2; i++)
		{
			constantBufferDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

			m_commandList->SetGraphicsRootDescriptorTable(1, constantBufferDescriptorHandle);

			m_commandList->DrawInstanced(3, 1, 0, 0);
		}

        // Draw loaded obj model
		m_commandList->IASetVertexBuffers(0, 1, &m_modelVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_modelIndexBufferView);

		constantBufferDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_commandList->SetGraphicsRootDescriptorTable(1, constantBufferDescriptorHandle);

		m_commandList->DrawIndexedInstanced(model.mesh.indexCount, 1, 0, 0, 0);
    }
    else
    {
		// Bind the descriptor heap giving access to the top-level acceleration
        // structure, as well as the ray tracing output
		std::vector<ID3D12DescriptorHeap*> heaps = { m_srvUavHeap.Get(), m_skyboxSamplerDescriptorHeap.Get() };
		m_commandList->SetDescriptorHeaps(static_cast<uint32_t>(heaps.size()), heaps.data());

		m_commandList->SetComputeRootSignature(m_globalRootSignature.Get());

		auto shaderResourceViewDescriptorHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

		shaderResourceViewDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize * 4;

		m_commandList->SetComputeRootDescriptorTable(0, shaderResourceViewDescriptorHandle);

		shaderResourceViewDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_commandList->SetComputeRootDescriptorTable(1, shaderResourceViewDescriptorHandle);

		shaderResourceViewDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_commandList->SetComputeRootDescriptorTable(2, shaderResourceViewDescriptorHandle);

		shaderResourceViewDescriptorHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_commandList->SetComputeRootDescriptorTable(3, shaderResourceViewDescriptorHandle);

		auto samplerDescriptorHandle = m_skyboxSamplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

		m_commandList->SetComputeRootDescriptorTable(4, samplerDescriptorHandle);

		// On the last frame, the ray tracing output was used as a copy source, to
        // copy its contents into the render target. Now we need to transition it to
        // a UAV so that the shaders can write in it.
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
                                                    m_outputResource.Get(), 
			                                       D3D12_RESOURCE_STATE_COPY_SOURCE,
			                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		m_commandList->ResourceBarrier(1, &transition);

		// Setup the ray tracing task
		D3D12_DISPATCH_RAYS_DESC raysDesc = {};

		// The layout of the SBT is as follows: ray generation shader, miss
		// shaders, hit groups. As described in the CreateShaderBindingTable method,
		// all SBT entries of a given type have the same size to allow a fixed stride.
		// The ray generation shaders are always at the beginning of the SBT.
		uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
        raysDesc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
        raysDesc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

		// The miss shaders are in the second SBT section, right after the ray
        // generation shader. We have one miss shader for the camera rays and one
        // for the shadow rays, so this section has a size of 2*m_sbtEntrySize. We
        // also indicate the stride between the two miss shaders, which is the size
        // of a SBT entry
		uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
        raysDesc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
                                            rayGenerationSectionSizeInBytes;
        raysDesc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
        raysDesc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

		// The hit groups section start after the miss shaders. In this sample we
        // have one 1 hit group for the triangle
		uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
        raysDesc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
                                          rayGenerationSectionSizeInBytes + 
                                          missSectionSizeInBytes;

        raysDesc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
        raysDesc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

		// Dimensions of the image to render, identical to a kernel launch dimension
        raysDesc.Width = GetWidth();
        raysDesc.Height = GetHeight();
        raysDesc.Depth = 1;

		// Bind the ray tracing pipeline
		m_commandList->SetPipelineState1(m_rayTracingStateObject.Get());

		// Dispatch the rays and write to the ray tracing output
		m_commandList->DispatchRays(&raysDesc);

		// The ray tracing output needs to be copied to the actual render target used
        // for display. For this, we need to transition the ray tracing output from a
        // UAV to a copy source, and the render target buffer to a copy destination.
        // We can then do the actual copy, before transitioning the render target
        // buffer into a render target, that will be then used to display the image
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
            m_outputResource.Get(), 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
             D3D12_RESOURCE_STATE_COPY_SOURCE);

		m_commandList->ResourceBarrier(1, &transition);

		transition = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(), 
            D3D12_RESOURCE_STATE_RENDER_TARGET, 
             D3D12_RESOURCE_STATE_COPY_DEST);

		m_commandList->ResourceBarrier(1, &transition);

		m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), m_outputResource.Get());

		transition = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(), 
          D3D12_RESOURCE_STATE_COPY_DEST, 
           D3D12_RESOURCE_STATE_RENDER_TARGET);

		m_commandList->ResourceBarrier(1, &transition);
    }

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloRaytracing::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12HelloRaytracing::CheckRayTracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));

	if (options.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
	{
		throw std::runtime_error("Ray Tracing not supported on device.");
	}
}

//-----------------------------------------------------------------------------
//
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: gathering the geometry, computing the sizes of the required
// buffers, and building the actual AS
//
D3D12HelloRaytracing::AccelerationStructureBuffers D3D12HelloRaytracing::CreateBottomLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& vertexBuffers)
{
    nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

    // Adding all vertex buffers and not transforming their position. 
    for (const auto &buffer : vertexBuffers) 
    {
        bottomLevelAS.AddVertexBuffer(buffer.first.Get(), 
                                    0, 
                                    buffer.second, 
                                    sizeof(Vertex),
                                    0, 
                                    0);
    } 
    
    // The AS build requires some scratch space to store temporary information. 
    // The amount of scratch memory is dependent on the scene complexity.
    UINT64 scratchSizeInBytes = 0; 
    
    // The final AS also needs to be stored in addition to the existing vertex 
    // buffers. It size is also dependent on the scene complexity. 
    UINT64 resultSizeInBytes = 0; 
    bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes); 
    
    // Once the sizes are obtained, the application is responsible for allocating 
    // the necessary buffers. Since the entire generation will be done on the GPU, 
    // we can directly allocate those on the default heap 
    AccelerationStructureBuffers buffers; 
    buffers.scratch = nv_helpers_dx12::CreateBuffer( m_device.Get(), 
                                                    scratchSizeInBytes, 
                                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
                                           D3D12_RESOURCE_STATE_COMMON,
                                                    nv_helpers_dx12::kDefaultHeapProps); 

    buffers.result = nv_helpers_dx12::CreateBuffer( m_device.Get(), 
                                                    resultSizeInBytes, 
                                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
                                                    nv_helpers_dx12::kDefaultHeapProps); 
    
    // Build the acceleration structure. Note that this call integrates a barrier
    // on the generated AS, so that it can be used to compute a top-level AS right 
    // after this method. 
    bottomLevelAS.Generate(m_commandList.Get(), 
               buffers.scratch.Get(), 
                buffers.result.Get(), 
                 false,
              nullptr);
    
    return buffers;
}

D3D12HelloRaytracing::AccelerationStructureBuffers D3D12HelloRaytracing::CreateBottomLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& vertexBuffers,
                                                                                         const std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>>& indexBuffers /*= {}*/)
{
    nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

    // Adding all vertex buffers and not transforming their position.
    for (size_t i = 0; i < vertexBuffers.size(); i++) 
    {
        // for (const auto &buffer : vVertexBuffers) {
        if (i < indexBuffers.size() && indexBuffers[i].second > 0)
        {

            bottomLevelAS.AddVertexBuffer(vertexBuffers[i].first.Get(), 0,
                vertexBuffers[i].second, sizeof(Vertex),
                indexBuffers[i].first.Get(), 0,
                indexBuffers[i].second, nullptr, 0, true);
        }
        else
        {
            bottomLevelAS.AddVertexBuffer(vertexBuffers[i].first.Get(), 0,
                vertexBuffers[i].second, sizeof(Vertex), 0,
                0);
        }
    }

	// The AS build requires some scratch space to store temporary information. 
  // The amount of scratch memory is dependent on the scene complexity.
	UINT64 scratchSizeInBytes = 0;

	// The final AS also needs to be stored in addition to the existing vertex 
	// buffers. It size is also dependent on the scene complexity. 
	UINT64 resultSizeInBytes = 0;
	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

	// Once the sizes are obtained, the application is responsible for allocating 
	// the necessary buffers. Since the entire generation will be done on the GPU, 
	// we can directly allocate those on the default heap 
	AccelerationStructureBuffers buffers;
	buffers.scratch = nv_helpers_dx12::CreateBuffer(m_device.Get(),
		scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		nv_helpers_dx12::kDefaultHeapProps);

	buffers.result = nv_helpers_dx12::CreateBuffer(m_device.Get(),
		resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	// Build the acceleration structure. Note that this call integrates a barrier
	// on the generated AS, so that it can be used to compute a top-level AS right 
	// after this method. 
	bottomLevelAS.Generate(m_commandList.Get(),
		buffers.scratch.Get(),
		buffers.result.Get(),
		false,
		nullptr);

	return buffers;
}

//-----------------------------------------------------------------------------
// Create the main acceleration structure that holds all instances of the scene.
// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
// the instances, computing the memory requirements for the AS, and building the
// AS itself
//
// pair of bottom level AS and matrix of the instance
void D3D12HelloRaytracing::CreateTopLevelAS(const std::vector<std::tuple<ComPtr<ID3D12Resource>, DirectX::XMMATRIX, bool>>& instances)
{
    // Gather all the instances into the builder helper
    // AddInstance??????????instanceID??????Hit??????????InstanceID????????
    // ??????????????????InstanceProperties??????????
    // ??????????????????????HitGroup??????????????????????HitGroup??????????????instanceID??
    // ????????????????????????HitGroup(????????ShadowGroup)????????????????????????2
	
    // Triangles
    for (size_t i = 0; i < 3; i++) 
    {
        m_topLevelASGenerator.AddInstance(std::get<0>(instances[i]).Get(), 
                                             std::get<1>(instances[i]), 
                                            static_cast<uint32_t>(i),
                                          0);
    }
    
    // Plane
	m_topLevelASGenerator.AddInstance(std::get<0>(instances[3]).Get(),
                                         std::get<1>(instances[3]),
		                                static_cast<uint32_t>(3),
		                              2);

    // Model
	m_topLevelASGenerator.AddInstance(std::get<0>(instances[instances.size() - 1]).Get(),
                                         std::get<1>(instances[instances.size() - 1]),
		                                static_cast<uint32_t>(instances.size() - 1),
                                      4);
    
    // As for the bottom-level AS, the building the AS requires some scratch space 
    // to store temporary data in addition to the actual AS. In the case of the 
    // top-level AS, the instance descriptors also need to be stored in GPU 
    // memory. This call outputs the memory requirements for each (scratch, 
    // results, instance descriptors) so that the application can allocate the 
    // corresponding memory 
    UINT64 scratchSize, resultSize, instanceDescsSize;
    m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(),  
                                     true, 
                                &scratchSize, 
                                &resultSize, 
                            &instanceDescsSize);

    // Create the scratch and result buffers. Since the build is all done on GPU, 
    // those can be allocated on the default heap 
    m_topLevelASBuffers.scratch = nv_helpers_dx12::CreateBuffer( m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nv_helpers_dx12::kDefaultHeapProps); 

    m_topLevelASBuffers.result = nv_helpers_dx12::CreateBuffer( m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
                                                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nv_helpers_dx12::kDefaultHeapProps); 
    
    // The buffer describing the instances: ID, shader binding information,
    // matrices ... Those will be copied into the buffer by the helper through 
    // mapping, so the buffer has to be allocated on the upload heap. 
    m_topLevelASBuffers.instanceDesc = nv_helpers_dx12::CreateBuffer( m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, 
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 

    // After all the buffers are allocated, or if only an update is required, we 
    // can build the acceleration structure. Note that in the case of the update 
    // we also pass the existing AS as the 'previous' AS, so that it can be 
    // refitted in place. 
    m_topLevelASGenerator.Generate(m_commandList.Get(), m_topLevelASBuffers.scratch.Get(), 
                        m_topLevelASBuffers.result.Get(), m_topLevelASBuffers.instanceDesc.Get());
}

//-----------------------------------------------------------------------------
//
// Combine the BLAS and TLAS builds to construct the entire acceleration
// structure required to ray trace the scene
//
void D3D12HelloRaytracing::CreateAccelerationStructures()
{
    // Build the bottom AS from the Triangle vertex buffer 
    AccelerationStructureBuffers bottomLevelBuffers = CreateBottomLevelAS({{m_vertexBuffer.Get(), 3}}); 

	// #DXR Extra: Per-Instance Data
	AccelerationStructureBuffers planeBottomLevelBuffers = CreateBottomLevelAS({ {m_planeVertexBuffer.Get(), 6} });

	AccelerationStructureBuffers modelBottomLevelBuffers = CreateBottomLevelAS(
        { {m_modelVertexBuffer.Get(), model.mesh.vertexCount } },
         { {m_modelIndexBuffer.Get(), model.mesh.indexCount} });

	//auto translation = XMMatrixTranslation(0.0f, -0.75f, 0.3f);
	auto translation = XMMatrixTranslation(0.0f, -0.5f, -0.3f);
    auto rotation = XMMatrixRotationY(0.0f);
	//auto scale = XMMatrixScaling(0.25f, 0.25f, 0.25f);
    auto scaleFactor = 0.25f;
	auto scale = XMMatrixScaling(scaleFactor, scaleFactor, scaleFactor);
	
    auto transform = scale * rotation * translation;

    transforms = 
    {
        XMMatrixIdentity(),                                             // ????
        XMMatrixTranslation(-0.6f, 0.0f, 0.0f),   // ??????
		XMMatrixTranslation(0.6f, 0.0f, 0.0f),    // ??????
		XMMatrixTranslation(0.0f, 0.0f, 0.0f),    // ??????
        transform,                                                      // obj????
        XMMatrixTranslation(0.0f, 0.0f, 0.0f),   // skybox
    };

    // Just one instance for now 
    m_instances = { {bottomLevelBuffers.result, transforms[0], false},
                    {bottomLevelBuffers.result, transforms[1], false},
                    {bottomLevelBuffers.result, transforms[2], false},
                    // #DXR Extra: Per-Instance Data
                    {planeBottomLevelBuffers.result, transforms[3], false},
                    {modelBottomLevelBuffers.result, transform, model.mesh.hasTexture} };

    CreateTopLevelAS(m_instances); 
    
    // Flush the command list and wait for it to finish 
    m_commandList->Close(); 
    
    ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()}; 
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists); 

    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue); 
    m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE); 
    
    // Once the command list is finished executing, reset it to be reused for rendering 
    ThrowIfFailed( m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get())); 
    
    // Store the AS buffers. The rest of the buffers will be released once we exit 
    // the function
    m_bottomLevelAS = bottomLevelBuffers.result;
}

//-----------------------------------------------------------------------------
// The ray generation shader needs to access 2 resources: the ray tracing output
// and the top-level acceleration structure
//
ComPtr<ID3D12RootSignature> D3D12HelloRaytracing::CreateRayGenRootSignature()
{
    nv_helpers_dx12::RootSignatureGenerator rootSignatureGenerator;
    rootSignatureGenerator.AddHeapRangesParameter({ {0 /*u0*/,
                                  1 /*1 descriptor */, 
                                  0 /*use the implicit register space 0*/, 
                                  D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/, 
                                  0 /*heap slot where the UAV is defined*/}, 
                                  {0 /*t0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/, 1},
                                  {0 /*b0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Camera parameters*/, 2}});

    return rootSignatureGenerator.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The hit shader communicates only through the ray payload, and therefore does
// not require any resources
//
ComPtr<ID3D12RootSignature> D3D12HelloRaytracing::CreateMissRootSignature()
{
    nv_helpers_dx12::RootSignatureGenerator rootSignatureGenerator;

	rootSignatureGenerator.AddHeapRangesParameter({
	{
        0 /*t0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        8 /*8th slot of the heap*/},
	});

    // ??????????????????
    rootSignatureGenerator.AddHeapRangesParameter({
    {   
        0 /*s0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
        0 /*1st slot of the heap*/}
    });

	return rootSignatureGenerator.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The miss shader communicates only through the ray payload, and therefore
// does not require any resources
//
//ComPtr<ID3D12RootSignature> D3D12HelloRaytracing::CreateHitRootSignature()
//{
//    // ????????Shader????????DXR??Shader??????????????????????????????SetGraphicsRootDescriptorTable()
//    // DXR??Shader?????????????????? Shader Binding Table ????????
//    nv_helpers_dx12::RootSignatureGenerator rootSignatureGenerator;
//
//    // ??????????????????????Shader??????StructuredBuffer??StructuredBuffer ?????? cbuffer ????
//    // ???? D3D12_ROOT_PARAMETER_TYPE_SRV ?????????????????????? Shader Resource View
//	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0);
//	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1);
//	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 2);
//
//	// #DXR Extra - Another ray type
//    // Add a single range pointing to the TLAS in the heap
//    rootSignatureGenerator.AddHeapRangesParameter(
//    {
//	    {
//            3 /*t3*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
//		    1 /*2nd slot of the heap*/
//        },
//		{
//		    4 /*t4*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
//		    4 /*5th slot of the heap*/
//        },
//		{
//		    5 /*t5*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
//		    5 /*6th slot of the heap*/
//        },
//		{
//		    6 /*t6*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
//		    6 /*7th slot of the heap*/
//        },
//		{
//		    7 /*t7*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
//		    7 /*8th slot of the heap*/
//        },
//	});
//
//    // ????????????????
//	rootSignatureGenerator.AddHeapRangesParameter({
//    {   0 /*s0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
//        0 /*1st slot of the heap*/},
//	{   1 /*s1*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
//		1 /*2nd slot of the heap*/},
//	});
//
//    return rootSignatureGenerator.Generate(m_device.Get(), true);
//}

ComPtr<ID3D12RootSignature> D3D12HelloRaytracing::CreateHitRootSignature()
{
	// ????????Shader????????DXR??Shader??????????????????????????????SetGraphicsRootDescriptorTable()
	// DXR??Shader?????????????????? Shader Binding Table ????????
	nv_helpers_dx12::RootSignatureGenerator rootSignatureGenerator;

	// ??????????????????????Shader??????StructuredBuffer??StructuredBuffer ?????? cbuffer ????
	// ???? D3D12_ROOT_PARAMETER_TYPE_SRV ?????????????????????? Shader Resource View
	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0);
	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1);
	rootSignatureGenerator.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 2);

	// #DXR Extra - Another ray type
	// Add a single range pointing to the TLAS in the heap
	rootSignatureGenerator.AddHeapRangesParameter({
	{
		3 /*t3*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1 /*2nd slot of the heap*/},
	});

	return rootSignatureGenerator.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
//
// The ray tracing pipeline binds the shader code, root signatures and pipeline
// characteristics in a single structure used by DXR to invoke the shaders and
// manage temporary memory during ray tracing
//
void D3D12HelloRaytracing::CreateRayTracingPipeline()
{
    nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get()); 

    // The pipeline contains the DXIL code of all the shaders potentially executed 
    // during the ray tracing process. This section compiles the HLSL code into a 
    // set of DXIL libraries. We chose to separate the code in several libraries 
    // by semantic (ray generation, hit, miss) for clarity. Any code layout can be 
    // used. 
    m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders/RayGen.hlsl"); 
    m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders/Miss.hlsl"); 
    m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders/Hit.hlsl");

    // DXR Extra - Another ray type
    m_shadowLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders/ShadowRay.hlsl");

	pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });

    // ??????????????(????)??????????????????
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit", L"PlaneClosestHit", L"ModelClosestHit" });     

	// DXR Extra - Another ray type
	pipeline.AddLibrary(m_shadowLibrary.Get(), { L"ShadowClosestHit", L"ShadowMiss" });

    // As described at the beginning of this section, to each shader corresponds a
    // root signature defining its external inputs.
    // To be used, each DX12 shader needs a root signature defining which 
    // parameters and buffers will be accessed. 
    m_rayGenRootSignature = CreateRayGenRootSignature();
    m_missRootSignature = CreateMissRootSignature(); 
    m_hitRootSignature = CreateHitRootSignature();

    // #DXR Extra - Another ray type
    m_shadowRootSignature = CreateHitRootSignature();

    // ??????????????????????0??1??2??????????????????TopLevelASGenerator::AddInstance??
    // ????????????????hitGroupIndex????????????????????????????????????HitGroup????????
    // ??????????????????????????Hit????
	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");               // HitGroup??Hit????????????ClosestHit????
	pipeline.AddHitGroup(L"PlaneHitGroup", L"PlaneClosestHit");     // PlaneHitGroup??Hit????????????PlaneClosestHit????     
	pipeline.AddHitGroup(L"ModelHitGroup", L"ModelClosestHit");     // ModelHitGroup??Hit????????????ModelClosestHit????
   
    // DXR Extra - Another ray type
    pipeline.AddHitGroup(L"ShadowHitGroup", L"ShadowClosestHit");

    // To be used, each shader needs to be associated to its root signature.
    // A shaders imported from the DXIL libraries needs to be associated with 
    // exactly one root signature.The shaders comprising the hit groups need 
    // to share the same root signature, which is associated to the hit group
    // (and not to the shaders themselves).Note that a shader does not have to 
    // actually access all the resources declared in its root signature, as long 
    // as the root signature defines a superset of the resources the shader needs.
    // The following section associates the root signature to each shader. Note 
    // that we can explicitly show that some shaders share the same root signature 
    // (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred 
    // to as hit groups, meaning that the underlying intersection, any-hit and 
    // closest-hit shaders share the same root signature. 
    pipeline.AddRootSignatureAssociation(m_rayGenRootSignature.Get(), {L"RayGen"});

    pipeline.AddRootSignatureAssociation(m_missRootSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitRootSignature.Get(), { L"HitGroup" });
    pipeline.AddRootSignatureAssociation(m_hitRootSignature.Get(), { L"HitGroup", L"PlaneHitGroup", L"ModelHitGroup" });

	// #DXR Extra - Another ray type
	pipeline.AddRootSignatureAssociation(m_missRootSignature.Get(), { L"Miss", L"ShadowMiss" });

	// #DXR Extra - Another ray type
	pipeline.AddRootSignatureAssociation(m_shadowRootSignature.Get(), { L"ShadowHitGroup" });

	// The payload size defines the maximum size of the data carried by the rays,
    // ie. the the data
    // exchanged between shaders, such as the HitInfo structure in the HLSL code.
    // It is important to keep this value as low as possible as a too high value
    // would result in unnecessary memory consumption and cache trashing.
	pipeline.SetMaxPayloadSize(8 * sizeof(float)); // RGB + distance + depth

	// Upon hitting a surface, DXR can provide several attributes to the hit. In
	// our sample we just use the barycentric coordinates defined by the weights
	// u,v of the last two vertices of the triangle. The actual barycentrics can
	// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	// The ray tracing process can shoot rays from existing hit points, resulting
    // in nested TraceRay calls. Our sample code traces only primary rays, which
    // then requires a trace depth of 1. Note that this recursion depth should be
    // kept to a minimum for best performance. Path tracing algorithms can be
    // easily flattened into a simple loop in the ray generation.

    // #DXR Extra - Another ray type
	pipeline.SetMaxRecursionDepth(2);

	m_rayTracingStateObject = pipeline.Generate(m_globalRootSignature.GetAddressOf());
	ThrowIfFailed(m_rayTracingStateObject->QueryInterface(IID_PPV_ARGS(&m_rayTracingStateObjectProperties)));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//
// Allocate the buffer holding the ray tracing output, with the same size as the
// output image
//
void D3D12HelloRaytracing::CreateRayTracingOutputBuffer()
{
    D3D12_RESOURCE_DESC resDesc = {}; resDesc.DepthOrArraySize = 1; 
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; 
    
    // The back buffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB 
    // formats cannot be used with UAVs. For accuracy we should convert to sRGB 
    // ourselves in the shader 
    resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; 
    resDesc.Width = GetWidth(); 
    resDesc.Height = GetHeight(); 
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; 
    resDesc.MipLevels = 1; 
    resDesc.SampleDesc.Count = 1; 
    
    ThrowIfFailed(m_device->CreateCommittedResource( 
        &nv_helpers_dx12::kDefaultHeapProps, 
        D3D12_HEAP_FLAG_NONE, 
        &resDesc, 
        D3D12_RESOURCE_STATE_COPY_SOURCE, 
        nullptr, 
        IID_PPV_ARGS(&m_outputResource)));
}

//-----------------------------------------------------------------------------
//
// Create the main heap used by the shaders, which will give access to the
// ray tracing output and the top-level acceleration structure
//
void D3D12HelloRaytracing::CreateShaderResourceHeap()
{
	// #DXR Extra: Perspective Camera
	// Create a SRV/UAV/CBV descriptor heap. We need 3 entries - 1 SRV for the TLAS, 1 UAV for the
	// ray tracing output and 1 CBV for the camera matrices 4 SRV for model 1 SRV for skybox
    m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), 9, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true); 
    
    m_srvUavHeap->SetName(L"m_srvUavHeap");

	// Slot0 - ????RayGen.hlsl????RWTexture2D< float4 > gOutput : register(u0);
    // Get a handle to the heap memory on the CPU side, to be able to write the 
    // descriptors directly 
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
    
    // Create the UAV. Based on the root signature we created it is the first +
    // entry. The Create*View methods write the view information directly into 
    // srvHandle 
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; 
    m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc, srvHandle); 
    
	// Slot1 - ????RayGen.hlsl??Hit.hlsl??????
	// Ray tracing acceleration structure, accessed as a SRV
	// RaytracingAccelerationStructure SceneBVH : register(t0);
    // Add the Top Level AS SRV right after the ray tracing output buffer 
    srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc; srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; 
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; 
    srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.result->GetGPUVirtualAddress();
    
    // Write the acceleration structure view in the heap 
    m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	// Slot2 - ????RayGen.hlsl????
	// #DXR Extra: Perspective Camera
	// cbuffer CameraParams : register(b0)
	// {
	//     float4x4 view;
	//     float4x4 projection;
	//	   float4x4 viewInverse;
	//	   float4x4 projectionInverse;
	// }
	// #DXR Extra: Perspective Camera
    // Add the constant buffer for the camera after the TLAS
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

	// Describe and create a constant buffer view for the camera
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;
	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);

	// Slot3 - ????Hit.hlsl??????
    // StructuredBuffer<InstanceProperties> instanceProperties : register(t2);
	//# DXR Extra - Simple Lighting
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<uint32_t>(m_instances.size());
	srvDesc.Buffer.StructureByteStride = sizeof(InstanceProperties);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	// Write the per-instance properties buffer view in the heap
	m_device->CreateShaderResourceView(m_instanceProperties.Get(), &srvDesc, srvHandle);

	// Slot4 - ????Hit.hlsl??????
    // Texture2D texture1 : register(t4);
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

    // ??????????????SRV
	D3D12_RESOURCE_DESC textureDesc = m_ModelTexture1->GetDesc();

    auto shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURE2D, textureDesc.Format, textureDesc.MipLevels);

	m_device->CreateShaderResourceView(m_ModelTexture1.Get(), &shaderResourceViewDesc, srvHandle);

	// Slot5 - ????Hit.hlsl??????
    // Texture2D texture2 : register(t5);
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

	// ??????????????SRV
	textureDesc = m_ModelTexture2->GetDesc();

    shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURE2D, textureDesc.Format, textureDesc.MipLevels);

    m_device->CreateShaderResourceView(m_ModelTexture2.Get(), &shaderResourceViewDesc, srvHandle);

	// Slot6 - ????Hit.hlsl??????
    // Texture2D texture3 : register(t6);
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

	// ??????????????SRV
	textureDesc = m_ModelTexture3->GetDesc();

    shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURE2D, textureDesc.Format, textureDesc.MipLevels);

	m_device->CreateShaderResourceView(m_ModelTexture3.Get(), &shaderResourceViewDesc, srvHandle);

	// Slot7 - ????Hit.hlsl??????
    // Texture2D texture4 : register(t7);
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

	// ??????????????SRV
	textureDesc = m_ModelTexture4->GetDesc();

	shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURE2D, textureDesc.Format, textureDesc.MipLevels);

	m_device->CreateShaderResourceView(m_ModelTexture4.Get(), &shaderResourceViewDesc, srvHandle);

	// Slot8 - ????Miss.hlsl??????
    // Texture2D environmentTexture : register(t0, space1);
	srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

    textureDesc = m_skyboxTexture->GetDesc();

	shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURECUBE, textureDesc.Format, textureDesc.MipLevels);

	m_device->CreateShaderResourceView(m_skyboxTexture.Get(), &shaderResourceViewDesc, srvHandle);
}

//-----------------------------------------------------------------------------
//
// The Shader Binding Table (SBT) is the cornerstone of the ray tracing setup:
// this is where the shader resources are bound to the shaders, in a way that
// can be interpreted by the ray tracer on GPU. In terms of layout, the SBT
// contains a series of shader IDs with their resource pointers. The SBT
// contains the ray generation shader, the miss shaders, then the hit groups.
// Using the helper class, those can be specified in arbitrary order.
//
void D3D12HelloRaytracing::CreateShaderBindingTable()
{
    // The SBT helper class collects calls to Add*Program. If called several 
    // times, the helper must be emptied before re-adding shaders.
    m_sbtHelper.Reset(); 

    // The pointer to the beginning of the heap is the only parameter required by 
    // shaders without root parameters 
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE skyboxSamplerHeapHandle = m_skyboxSamplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    CD3DX12_GPU_DESCRIPTOR_HANDLE textureHeapHandle(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	auto heapPointer = reinterpret_cast<uint64_t*>(srvUavHeapHandle.ptr);

	auto skyboxSamplerHeapPointer = reinterpret_cast<uint64_t*>(skyboxSamplerHeapHandle.ptr);

	auto textureHeapPointer1 = reinterpret_cast<uint64_t*>(textureHeapHandle.ptr);
	auto textureHeapPointer2 = reinterpret_cast<uint64_t*>(textureHeapHandle.ptr);

    // The ray generation only uses heap data 
    m_sbtHelper.AddRayGenerationProgram(L"RayGen", {heapPointer});
    
    // The miss and hit shaders do not access any external resources: instead they 
    // communicate their results through the ray payload 
	m_sbtHelper.AddMissProgram(L"Miss", {});

    // #DXR Extra - Another ray type
	m_sbtHelper.AddMissProgram(L"ShadowMiss", {});
    
    // ????SBT??HitGroup??????????
    // HitGroup
    // ShadowHitGroup
    // PlaneHitGroup
    // ShadowHitGroup
    // ModelHitGroup
    // ShadowHitGroup
    // ????????????????????????????(????????????????????.obj????)??????????????????HitGroup
	// HitGroup, PlaneHitGroup??ModelHitGroup????????
    // StructuredBuffer<STriVertex> BTriVertex : register(t0);
    // ????inputData????????????
    // Adding the triangle hit shader 
    m_sbtHelper.AddHitGroup(L"HitGroup", { (void*)(m_vertexBuffer->GetGPUVirtualAddress()) });

	// #DXR Extra - Another ray type
	m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});

	// #DXR Extra: Per-Instance Data
    // Adding the plane
	m_sbtHelper.AddHitGroup(L"PlaneHitGroup", { (void*)(m_planeVertexBuffer->GetGPUVirtualAddress()) });

	// #DXR Extra - Another ray type
	m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});

	// inputData??????????????????????????Hit.hlsl????
    // RaytracingAccelerationStructure SceneBVH : register(t3)????????????PlaneHitGroup??????????PlaneHitGroup??
    // ModelHitGroup??????????????????????????????????????
	std::vector<void*> inputData{(void*)(m_modelVertexBuffer->GetGPUVirtualAddress()),
		                         (void*)(m_modelIndexBuffer->GetGPUVirtualAddress()),
		                         (void*)(m_instanceProperties->GetGPUVirtualAddress())
                                };

    // ??????????????????????????????????SRV????????Shader??????????????????????????????????heapPointer
    // ???????????????????????? + ??????????Shader??????????????????????????????????????????????????????
    // ??????????????????????????????ID3D12Resource????(|-_-)??????????????????????????????????????????
    // ????????????????????????ID3D12Resource????????????????????????????????????????????????????????????
    // ??Shader????????????????????????????????????????????????????????????????????????ID3D12Resource????????
    // ??????????????????????????Texture Sampler, ????????????????????????????????????????????????????????????
    // Shader ????????????????????????????????????????????????????????????????
    //
    // ??????????????????????????????ID3D12Resource??????????????????????????????Buffer??????????(??????????)??
    // GetGPUVirtualAddress??????????nullptr????????????????????????????3??nullptr????????????????????????????????
	// ?????????????????????????????????????????? Debug Output ????????????????
    // D3D12 WARNING: ID3D12Resource2::ID3D12Resource::GetGPUVirtualAddress: GetGPUVirtualAddress returns 
    // NULL for non-buffer resources. [ MISCELLANEOUS WARNING #745: GETGPUVIRTUALADDRESS_INVALID_RESOURCE_DIMENSION]
    // ??????????????????????????????????(????)??
    // ????DXR??????????inputData????????????????????????????Shader??????????????????????????????????????????????nullptr
    // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    // ????????????????????????????????????????????????????????????????Shader??texture1??texture3??????????????texture2
    // ????????????????
    // 
    // ????????????????????CreateHitRootSignature()??????SRV??????????????????????SRV????????????Range??????????????????
    // SRV??????????Range????????????????texture1~texture4??????????????
	//rootSignatureGenerator.AddHeapRangesParameter(
	//{
	//	{
	//		3 /*t3*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	//		1 /*2nd slot of the heap*/
	//	},
	//	{
	//		4 /*t4*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	//		4 /*5th slot of the heap*/
	//	},
	//	{
	//		5 /*t5*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	//		5 /*6th slot of the heap*/
	//	},
	//	{
	//		6 /*t6*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	//		6 /*7th slot of the heap*/
	//	},
	//	{
	//		7 /*t7*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
	//		7 /*8th slot of the heap*/
	//	},
	//});
	//inputData.push_back(heapPointer);
	//inputData.push_back(nullptr);
	//inputData.push_back(nullptr);
	//inputData.push_back(nullptr);
	//inputData.push_back((void*)(m_ModelTexture1->GetGPUVirtualAddress()));
	//inputData.push_back((void*)(m_ModelTexture2->GetGPUVirtualAddress()));
	//inputData.push_back(skyboxSamplerHeapPointer);

	m_sbtHelper.AddHitGroup(L"ModelHitGroup", inputData);

    // #DXR Extra - Another ray type
    m_sbtHelper.AddHitGroup(L"ShadowHitGroup", {});

	const uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	m_sbtStorage = nv_helpers_dx12::CreateBuffer(m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	
    if (!m_sbtStorage)
	{
		throw std::logic_error("Could not allocate the shader binding table");
	}

	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rayTracingStateObjectProperties.Get());
}

//-----------------------------------------------------------------------------
//
// Create a vertex buffer for the plane
//
// #DXR Extra: Per-Instance Data
void D3D12HelloRaytracing::CreatePlaneVB()
{
    // Define the geometry for a plane. 
    Vertex planeVertices[] = { 
        {{-1.5f, -0.8f,  1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}},  // 0 
        {{-1.5f, -0.8f, -1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}},  // 1 
        {{ 1.5f, -0.8f,  1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}},  // 2 
        {{ 1.5f, -0.8f,  1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}},  // 2 
        {{-1.5f, -0.8f, -1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}},  // 1 
        {{ 1.5f, -0.8f, -1.5f}, { 0.0f, 1.0f, 0.0f}, { 0.0f, 0.0f }, {0.9f, 0.9f, 0.9f, 1.0f}}   // 4 
    }; 
    
    const UINT planeBufferSize = sizeof(planeVertices); 
    
    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be 
    // marshalled over. Please read up on Default Heap usage. An upload heap is 
    // used here for code simplicity and because there are very few verts to 
    // actually transfer. 
    CD3DX12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); 
    CD3DX12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC::Buffer(planeBufferSize); 
    
    ThrowIfFailed(m_device->CreateCommittedResource( 
        &heapProperty, 
        D3D12_HEAP_FLAG_NONE, 
        &bufferResource, 
        D3D12_RESOURCE_STATE_GENERIC_READ, 
        nullptr, 
        IID_PPV_ARGS(&m_planeVertexBuffer))); 
        
    // Copy the triangle data to the vertex buffer. 
    UINT8 *pVertexDataBegin; CD3DX12_RANGE readRange( 0, 0); 
        
    // We do not intend to read from this resource on the CPU. 
    ThrowIfFailed(m_planeVertexBuffer->Map( 0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin))); 
    memcpy_s(pVertexDataBegin, sizeof(planeVertices), planeVertices, sizeof(planeVertices));
    m_planeVertexBuffer->Unmap(0, nullptr);
        
    // Initialize the vertex buffer view. 
    m_planeVertexBufferView.BufferLocation = m_planeVertexBuffer->GetGPUVirtualAddress(); 
    m_planeVertexBufferView.StrideInBytes = sizeof(Vertex); 
    m_planeVertexBufferView.SizeInBytes = planeBufferSize;
}

void D3D12HelloRaytracing::CreateGlobalConstantBuffer()
{
    // Due to HLSL packing rules, we create the CB with 9 float4 (each needs to start on a 16-byte // boundary) 
    XMVECTOR bufferData[] = { 
        // A 
        XMVECTOR{1.0f, 0.0f, 0.0f, 1.0f}, 
        XMVECTOR{0.7f, 0.4f, 0.0f, 1.0f}, 
        XMVECTOR{0.4f, 0.7f, 0.0f, 1.0f}, 
        // B 
        XMVECTOR{0.0f, 1.0f, 0.0f, 1.0f}, 
        XMVECTOR{0.0f, 0.7f, 0.4f, 1.0f}, 
        XMVECTOR{0.0f, 0.4f, 0.7f, 1.0f}, 
        // C 
        XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f}, 
        XMVECTOR{0.4f, 0.0f, 0.7f, 1.0f}, 
        XMVECTOR{0.7f, 0.0f, 0.4f, 1.0f}, }; 
    
    // Create our buffer 
    m_globalConstantBuffer = nv_helpers_dx12::CreateBuffer( 
        m_device.Get(),
        sizeof(bufferData), 
        D3D12_RESOURCE_FLAG_NONE, 
        D3D12_RESOURCE_STATE_GENERIC_READ, 
        nv_helpers_dx12::kUploadHeapProps); 
    
    // Copy CPU memory to GPU 
    uint8_t* pData; ThrowIfFailed(m_globalConstantBuffer->Map(0, nullptr, (void**)&pData)); 
    memcpy_s(pData, sizeof(bufferData), bufferData, sizeof(bufferData));
    m_globalConstantBuffer->Unmap(0, nullptr);
}

//----------------------------------------------------------------------------------
//
// The camera buffer is a constant buffer that stores the transform matrices of
// the camera, for use by both the rasterization and ray tracing. This method
// allocates the buffer where the matrices will be copied. For the sake of code
// clarity, it also creates a heap containing only this buffer, to use in the
// rasterization path.
//
// #DXR Extra: Perspective Camera
void D3D12HelloRaytracing::CreateCameraBuffer()
{
    uint32_t nbMatrix = 4; 
    
    // view, perspective, viewInv, perspectiveInv 
    m_cameraBufferSize = nbMatrix * sizeof(XMMATRIX); 
    
    // Create the constant buffer for all matrices 
    m_cameraBuffer = nv_helpers_dx12::CreateBuffer( m_device.Get(), m_cameraBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps); 
    
    // Create a descriptor heap that will be used by the rasterization shaders
    m_constantbufferHeap = nv_helpers_dx12::CreateDescriptorHeap( m_device.Get(), static_cast<uint32_t>(transforms.size() + 2), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
    
    // Describe and create the constant buffer view. 
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {}; 
    cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress(); 
    cbvDesc.SizeInBytes = m_cameraBufferSize; 
    
    // Get a handle to the heap memory on the CPU side, to be able to write the 
    // descriptors directly 
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_constantbufferHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateConstantBufferView(&cbvDesc, srvHandle);

	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void**)&m_cameraBufferData));
}

// #DXR Extra: Perspective Camera
//--------------------------------------------------------------------------------
// Create and copies the view model and perspective matrices of the camera
//
void D3D12HelloRaytracing::UpdateCameraBuffer()
{
    std::vector<XMMATRIX> matrices(4); 
  
    float fovAngleY = 45.0f * XM_PI / 180.0f; 
    
    matrices[1] = XMMatrixPerspectiveFovLH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f);

	// Initialize the view matrix, ideally this should be based on user 
    // interactions The look at and perspective matrices used for rasterization are 
    // defined to transform world-space vertices into a [0,1]x[0,1]x[0,1] camera space 
    float x = std::cos(XM_PI / 180.0f * m_yaw) * std::cos(XM_PI / 180.0f * m_pitch);
	float y = std::sin(XM_PI / 180.0f * m_pitch);
	float z = std::sin(XM_PI / 180.0f * -m_yaw) * std::cos(XM_PI / 180.0f * -m_pitch);

    XMVECTOR front = XMVectorSet(x, y, z, 0.0f);

    m_front = XMVector3Normalize(front);

    m_right = XMVector3Cross(m_worldUp, front);
    m_up = XMVector3Cross(m_front, m_right);

	matrices[0] = XMMatrixLookAtLH(m_eye, m_eye + m_front, m_up);

    // Ray tracing has to do the contrary of rasterization: rays are defined in
    // camera space, and are transformed into world space. To do this, we need to 
    // store the inverse matrices as well. 
    XMVECTOR det;
    matrices[2] = XMMatrixInverse(&det, matrices[0]);
    matrices[3] = XMMatrixInverse(&det, matrices[1]);

    auto view = matrices[0];
	view.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    //matrices[0] = view;

    m_modelViewProjection = XMMatrixIdentity() * view * matrices[1];
    
    // Copy the matrix contents 
    memcpy_s(m_cameraBufferData, m_cameraBufferSize, matrices.data(), m_cameraBufferSize);
}

void D3D12HelloRaytracing::UpdateCameraMovement(float deltaTime)
{
    if (GetAsyncKeyState('W') & 0x8000)
    {
        m_eye += m_front * m_cameraSpeed * m_frameTime;
    }

	if (GetAsyncKeyState('S') & 0x8000)
	{
		m_eye -= m_front * m_cameraSpeed * m_frameTime;
	}

	if (GetKeyState('A') & 0x8000)
	{
		m_eye -= m_right * m_cameraSpeed * m_frameTime;
	}

	if (GetKeyState('D') & 0x8000)
	{
		m_eye += m_right * m_cameraSpeed * m_frameTime;
	}

	if (GetAsyncKeyState('Q') & 0x8000)
	{
		m_eye += m_up * m_cameraSpeed * m_frameTime;
	}

	if (GetAsyncKeyState('E') & 0x8000)
	{
		m_eye -= m_up * m_cameraSpeed * m_frameTime;
	}
}

void D3D12HelloRaytracing::CreateConstantBuffer()
{
	uint32_t nbMatrix = 1;

    m_constantBuffers.resize(transforms.size());

	// view, perspective, viewInv, perspectiveInv 
	m_constantBufferSize = ROUND_UP(nbMatrix * sizeof(XMMATRIX), 256);

	// Create the constant buffer for all matrices
    for (auto& constantBuffer : m_constantBuffers)
    {
        constantBuffer =  nv_helpers_dx12::CreateBuffer(m_device.Get(), m_constantBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
    }

	// Get a handle to the heap memory on the CPU side, to be able to write the 
	// descriptors directly 
    constantBufferDatas.resize(m_constantBuffers.size());

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_constantbufferHeap->GetCPUDescriptorHandleForHeapStart();

    for (auto i = 0; i < m_constantBuffers.size(); i++)
    {
		// Describe and create the constant buffer view. 
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_constantBuffers[i]->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = m_constantBufferSize;

        srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

		m_device->CreateConstantBufferView(&cbvDesc, srvHandle);

		// Copy the matrix contents 
        ThrowIfFailed(m_constantBuffers[i]->Map(0, nullptr, (void**)&constantBufferDatas[i]));
    }

	// ??????????????SRV
	D3D12_RESOURCE_DESC textureDesc = m_skyboxTexture->GetDesc();

	auto shaderResourceViewDesc = CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION_TEXTURECUBE, textureDesc.Format, textureDesc.MipLevels);

    srvHandle.ptr += m_SRVCBVUAVDescriptorHandleIncrementSize;

	m_device->CreateShaderResourceView(m_skyboxTexture.Get(), &shaderResourceViewDesc, srvHandle);
}

void D3D12HelloRaytracing::UpdateConstantBuffer()
{
    for (auto i = 0; i < transforms.size(); i++)
    {
		memcpy_s(constantBufferDatas[i], m_constantBufferSize, &transforms[i], m_constantBufferSize);
    }
}

void D3D12HelloRaytracing::createModelVertexBuffer(const DXModel& model, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView)
{
    const uint32_t vertexBufferSize = model.mesh.vertexBufferSize;

	// Note: using upload heaps to transfer static data like vert buffers is not 
	// recommended. Every time the GPU needs it, the upload heap will be marshalled 
	// over. Please read up on Default Heap usage. An upload heap is used here for 
	// code simplicity and because there are very few verts to actually transfer.
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_modelVertexBuffer)));

	// Copy the triangle data to the vertex buffer.
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_modelVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy_s(pVertexDataBegin, vertexBufferSize, model.mesh.vertices.data(), vertexBufferSize);
    m_modelVertexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view.
    vertexBufferView.BufferLocation = m_modelVertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;
}

void D3D12HelloRaytracing::createModelIndexBuffer(const DXModel& model, D3D12_INDEX_BUFFER_VIEW& indexBufferView)
{
	const uint32_t indexBufferSize = model.mesh.indexBufferSize;

	// Note: using upload heaps to transfer static data like vert buffers is not 
	// recommended. Every time the GPU needs it, the upload heap will be marshalled 
	// over. Please read up on Default Heap usage. An upload heap is used here for 
	// code simplicity and because there are very few verts to actually transfer.
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_modelIndexBuffer)));

	// Copy the triangle data to the vertex buffer.
	UINT8* pIndexDataBegin;
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_modelIndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy_s(pIndexDataBegin, indexBufferSize, model.mesh.indices.data(), indexBufferSize);
    m_modelIndexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view.
    indexBufferView.BufferLocation = m_modelIndexBuffer->GetGPUVirtualAddress();
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    indexBufferView.SizeInBytes = indexBufferSize;
}

//-----------------------------------------------------------------------------
//
// Create the depth buffer for rasterization. This buffer needs to be kept in a separate heap
//
// #DXR Extra: Depth Buffering
void D3D12HelloRaytracing::CreateDepthBuffer()
{
    // The depth buffer heap type is specific for that usage, and the heap contents are not visible 
    // from the shaders 
    m_dsvHeap = nv_helpers_dx12::CreateDescriptorHeap(m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false); 

    // The depth and stencil can be packed into a single 32-bit texture buffer. Since we do not need 
    // stencil, we use the 32 bits to store depth information (DXGI_FORMAT_D32_FLOAT). 
    D3D12_HEAP_PROPERTIES depthHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT); 
    D3D12_RESOURCE_DESC depthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 1); 
    depthResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; 

    // The depth values will be initialized to 1 
    CD3DX12_CLEAR_VALUE depthOptimizedClearValue(DXGI_FORMAT_D32_FLOAT, 1.0f, 0); 
    
    // Allocate the buffer itself, with a state allowing depth writes 
    ThrowIfFailed(m_device->CreateCommittedResource( 
        &depthHeapProperties, 
        D3D12_HEAP_FLAG_NONE, 
        &depthResourceDesc, 
        D3D12_RESOURCE_STATE_DEPTH_WRITE, 
        &depthOptimizedClearValue, 
        IID_PPV_ARGS(&m_depthStencil))); 
    
    // Write the depth buffer view into the depth buffer heap 
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {}; 
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; 
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; 
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

// Allocate memory to hold per-instance information
// #DXR Extra - Refitting
void D3D12HelloRaytracing::CreateInstancePropertiesBuffer()
{
	uint32_t bufferSize = ROUND_UP(
		static_cast<uint32_t>(m_instances.size()) * sizeof(InstanceProperties),
		D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	// Create the constant buffer for all matrices
	m_instanceProperties = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), bufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	CD3DX12_RANGE readRange(
		0, 0); // We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_instanceProperties->Map(0, &readRange,
		reinterpret_cast<void**>(&m_instancePropertiesBufferData)));
}

//--------------------------------------------------------------------------------------------------
// Copy the per-instance data into the buffer
// #DXR Extra - Refitting
void D3D12HelloRaytracing::UpdateInstancePropertiesBuffer()
{
	InstanceProperties* current = m_instancePropertiesBufferData;

	for (const auto& instance : m_instances) 
    {
        current->objectToWorld = std::get<1>(instance);
        current->hasTexture = std::get<2>(instance);
        current++;
	}
}

uint64_t D3D12HelloRaytracing::loadDDSTexture(const std::wstring& path, ComPtr<ID3D12Resource>& texture)
{
	//????Skybox?? Cube Map ??????????
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subResources;
	DDS_ALPHA_MODE alphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool bIsCube = false;

	ID3D12Resource* textureResource = nullptr;

	ThrowIfFailed(LoadDDSTextureFromFile(
		m_device.Get(),
        path.c_str(),
		&textureResource,
		ddsData,
		subResources,
		SIZE_MAX,
		&alphaMode,
		&bIsCube));

	// ??????????????????????????????????????Copy????????????????????
    texture.Attach(textureResource);

    auto textureDesc = texture->GetDesc();

	// ????skybox????????????????????????
	auto textureUploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast<uint32_t>(subResources.size()));

	D3D12_HEAP_DESC textureUploadHeapDesc{};
	textureUploadHeapDesc.Alignment = 0;
	textureUploadHeapDesc.SizeInBytes = ROUND_UP(2 * textureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	textureUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
	textureUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	textureUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// ????????????????????????????????
	textureUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
   
    ThrowIfFailed(m_device->CreateHeap(&textureUploadHeapDesc, IID_PPV_ARGS(&m_textureUploadHeap)));

	// ??skybox??????????????????????????????????????
    ThrowIfFailed(m_device->CreatePlacedResource(
        m_textureUploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_textureUploadBuffer)));

	// ????skybox??????
	UpdateSubresources(
        m_commandList.Get(),
        texture.Get(),
        m_textureUploadBuffer.Get(),
		0,
		0,
		static_cast<uint32_t>(subResources.size()),
		subResources.data()
	);

    m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    ThrowIfFailed(m_commandList->Close());

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

    WaitForPreviousFrame();

    return textureUploadBufferSize;
}

void D3D12HelloRaytracing::createSkyboxSamplerDescriptorHeap()
{
    m_skyboxSamplerDescriptorHeap = nv_helpers_dx12::CreateDescriptorHeap(m_device.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);

    m_skyboxSamplerDescriptorHeap->SetName(L"m_skyboxSamplerDescriptorHeap");
}

void D3D12HelloRaytracing::createSkyboxSampler()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE skyboxSamplerDescriptorHandle(m_skyboxSamplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SAMPLER_DESC skyboxSamplerDesc{};
	skyboxSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	skyboxSamplerDesc.MinLOD = 0;
	skyboxSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	skyboxSamplerDesc.MaxAnisotropy = 1;
	skyboxSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// Sampler 1
	skyboxSamplerDesc.BorderColor[0] = 1.0f;
	skyboxSamplerDesc.BorderColor[1] = 0.0f;
	skyboxSamplerDesc.BorderColor[2] = 0.0f;
	skyboxSamplerDesc.BorderColor[3] = 1.0f;
	skyboxSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;

	m_device->CreateSampler(&skyboxSamplerDesc, skyboxSamplerDescriptorHandle);

    skyboxSamplerDescriptorHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	// Sampler 2
	skyboxSamplerDesc.BorderColor[0] = 0.0f;
	skyboxSamplerDesc.BorderColor[1] = 1.0f;
	skyboxSamplerDesc.BorderColor[2] = 0.0f;
	skyboxSamplerDesc.BorderColor[3] = 1.0f;
	skyboxSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;

	m_device->CreateSampler(&skyboxSamplerDesc, skyboxSamplerDescriptorHandle);
}

D3D12_SHADER_RESOURCE_VIEW_DESC D3D12HelloRaytracing::CreateShaderResourceViewDesc(D3D12_SRV_DIMENSION ViewDimension, DXGI_FORMAT format, uint32_t mipLevels)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderResourceViewDesc.ViewDimension = ViewDimension;

    shaderResourceViewDesc.Format = format;

	if (shaderResourceViewDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
	{
        shaderResourceViewDesc.TextureCube.MipLevels = mipLevels;
	}
	else
	{
        shaderResourceViewDesc.Texture2D.MipLevels = mipLevels;
	}

    return shaderResourceViewDesc;
}

void D3D12HelloRaytracing::CreateSkyboxGraphicsPipelineState()
{
	TCHAR* shaderFileName = L"Shaders/Skybox.hlsl";

	ComPtr<ID3DBlob> skyboxVertexShader;
	ComPtr<ID3DBlob> skyboxPixelShader;

#if defined(_DEBUG)
	uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32_t compileFlags = 0;
#endif

	ThrowIfFailed(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "VSMain", "vs_5_1", compileFlags, 0, &skyboxVertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "PSMain", "ps_5_1", compileFlags, 0, &skyboxPixelShader, nullptr));

	// ??????????????????????????????
	D3D12_INPUT_ELEMENT_DESC skyboxInputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// ????????????PSO, ??????????????????????????
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxGraphicsPipelineStateDesc{};
	skyboxGraphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.InputLayout = { skyboxInputElementDesc, _countof(skyboxInputElementDesc) };
	skyboxGraphicsPipelineStateDesc.pRootSignature = m_rootSignature.Get();
	skyboxGraphicsPipelineStateDesc.VS = CD3DX12_SHADER_BYTECODE(skyboxVertexShader.Get());
	skyboxGraphicsPipelineStateDesc.PS = CD3DX12_SHADER_BYTECODE(skyboxPixelShader.Get());

	//explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT) noexcept
	//{
	//	FillMode = D3D12_FILL_MODE_SOLID;
	//	CullMode = D3D12_CULL_MODE_BACK;
	//	FrontCounterClockwise = FALSE;
	//	DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	//	DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	//	SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	//	DepthClipEnable = TRUE;
	//	MultisampleEnable = FALSE;
	//	AntialiasedLineEnable = FALSE;
	//	ForcedSampleCount = 0;
	//	ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	//}
	skyboxGraphicsPipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skyboxGraphicsPipelineStateDesc.RasterizerState.FrontCounterClockwise = FALSE;

	//explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT) noexcept
	//{
	//    AlphaToCoverageEnable = FALSE;
	//    IndependentBlendEnable = FALSE;
	//    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
	//    {
	//        FALSE,FALSE,
	//        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
	//        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
	//        D3D12_LOGIC_OP_NOOP,
	//        D3D12_COLOR_WRITE_ENABLE_ALL,
	//    };
	//    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
	//        RenderTarget[i] = defaultRenderTargetBlendDesc;
	//}
	skyboxGraphicsPipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	//skyboxGraphicsPipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	//skyboxGraphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	skyboxGraphicsPipelineStateDesc.SampleMask = UINT_MAX;
	skyboxGraphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyboxGraphicsPipelineStateDesc.NumRenderTargets = 1;
	skyboxGraphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	skyboxGraphicsPipelineStateDesc.SampleDesc.Count = 1;

	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&skyboxGraphicsPipelineStateDesc, IID_PPV_ARGS(&m_skyboxGraphicsPipelineState)));
}

void D3D12HelloRaytracing::CreateGlobalRootSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rootSignatureGenerator;

	rootSignatureGenerator.AddHeapRangesParameter({
	{
		0 /*t0*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		0 /*1st slot of the table*/},
	});

	rootSignatureGenerator.AddHeapRangesParameter({
	{
		1 /*t1*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		0 /*2nd slot of the table*/},
	});

	rootSignatureGenerator.AddHeapRangesParameter({
	{
		2 /*t2*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		0 /*3rd slot of the table*/},
	});

	rootSignatureGenerator.AddHeapRangesParameter({
	{
		3 /*t3*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		0 /*4th slot of the table*/},
	});

	// ????????????????
	rootSignatureGenerator.AddHeapRangesParameter({
	{   0 /*s0*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
		0 /*1st slot of the table*/},
	{   1 /*s1*/, 1, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
		1 /*2nd slot of the table*/},
	});

	m_globalRootSignature = rootSignatureGenerator.Generate(m_device.Get(), false);

	CD3DX12_ROOT_PARAMETER constantParameters[5]{};
	CD3DX12_DESCRIPTOR_RANGE ranges[5]{};

	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 1, 0);
	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 1, 0);
	ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 1, 0);
	ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 1);

	constantParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);
	constantParameters[4].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_ALL);

	// Create an empty root signature.
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		rootSignatureDesc.Init(_countof(constantParameters), constantParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		//ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature)));
	}
}
