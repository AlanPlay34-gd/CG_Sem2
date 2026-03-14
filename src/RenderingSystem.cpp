#include "../h/RenderingSystem.h"
#include "../h/d3dx12.h"
#include "../h/d3dUtil.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>

RenderingSystem::RenderingSystem(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12Fence* fence,
    UINT swapChainBufferCount,
    DXGI_FORMAT backBufferFormat)
    : mDevice(device)
    , mCommandQueue(commandQueue)
    , mCommandList(commandList)
    , mCommandAllocator(commandAllocator)
    , mFence(fence)
    , mSwapChainBufferCount(swapChainBufferCount)
    , mBackBufferFormat(backBufferFormat)
{
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

RenderingSystem::~RenderingSystem()
{
    Shutdown();
}

bool RenderingSystem::Initialize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    if (!CreateGBuffer(width, height))
        return false;

    if (!CreateLightingResources())
        return false;

    return true;
}

bool RenderingSystem::CreateGBuffer(UINT width, UINT height)
{
    mGBuffer = std::make_unique<GBuffer>();
    return mGBuffer->Initialize(mDevice, width, height);
}

bool RenderingSystem::CreateLightingResources()
{

    // Загружаем шейдеры
    auto vsLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "VS",
        "vs_5_0");

    if (!vsLighting) return false;

    auto psLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "PS",
        "ps_5_0");

    if (!psLighting) return false;

    // ============= ROOT SIGNATURE =============
    D3D12_DESCRIPTOR_RANGE srvRanges[3];

    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 1;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[1].NumDescriptors = 1;
    srvRanges[1].BaseShaderRegister = 1;
    srvRanges[1].RegisterSpace = 0;
    srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[2].NumDescriptors = 1;
    srvRanges[2].BaseShaderRegister = 2;
    srvRanges[2].RegisterSpace = 0;
    srvRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
    rootParams[0].DescriptorTable.pDescriptorRanges = srvRanges;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 16;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serializedRootSig, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    hr = mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
                                     serializedRootSig->GetBufferSize(),
                                     IID_PPV_ARGS(&mLightingRootSignature));
    if (FAILED(hr))
    {
        return false;
    }

    // ============= PSO =============
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.VS = { vsLighting->GetBufferPointer(), vsLighting->GetBufferSize() };
    psoDesc.PS = { psLighting->GetBufferPointer(), psLighting->GetBufferSize() };
    psoDesc.pRootSignature = mLightingRootSignature.Get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.InputLayout = { nullptr, 0 };

    hr = mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO));
    if (FAILED(hr))
    {
        char msg[256];
        sprintf_s(msg, "PSO create failed: 0x%08X", hr);
        OutputDebugStringA(msg);
        return false;
    }

    mLightingCB = std::make_unique<UploadBuffer<LightConstants>>(
        mDevice,
        10,  // Максимум источников
        true);

    return true;
}

void RenderingSystem::GeometryPass(
    ID3D12PipelineState* pso,
    ID3D12RootSignature* rootSignature,
    ID3D12DescriptorHeap* cbvSrvHeap,
    UINT cbvSrvDescriptorSize,
    const std::vector<Submesh>& submeshes,
    const std::vector<Material>& materials,
    ID3D12Resource* vertexBuffer,
    ID3D12Resource* indexBuffer,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
    ID3D12Resource* depthStencilBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect,
    UINT materialCount,
    ID3D12Resource* secondaryTexture)
{
    if (!mGBuffer) return;

    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator, pso);

    // Переводим G-буфер текстуры в состояние RENDER_TARGET
    D3D12_RESOURCE_BARRIER barriers[GBuffer::GBUFFER_COUNT + 1];

    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // Переводим depth buffer в состояние DEPTH_WRITE
    barriers[GBuffer::GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT + 1, barriers);

    // Очищаем G-буфер
    mGBuffer->ClearRenderTargets(mCommandList);

    // Очищаем depth buffer
    mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Устанавливаем render targets G-буфера
    mGBuffer->SetRenderTargets(mCommandList, mGBuffer->mRtvHeap.Get(), dsvHandle);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    mCommandList->SetGraphicsRootSignature(rootSignature);

    ID3D12DescriptorHeap* heaps[] = { cbvSrvHeap };
    mCommandList->SetDescriptorHeaps(1, heaps);

    mCommandList->SetGraphicsRootDescriptorTable(0, cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    for (auto& sm : submeshes)
    {
        const Material* mat = nullptr;
        for (auto& m : materials)
        {
            if (m.Name == sm.MaterialName)
            {
                mat = &m;
                break;
            }
        }

        if (!mat) continue;

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle1 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle1.ptr += (1 + mat->SrvHeapIndex) * cbvSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle1);

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle2 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle2.ptr += (1 + materialCount) * cbvSrvDescriptorSize;

        bool isFloor = (mat->Name.find("floor") != std::string::npos);
        mCommandList->SetGraphicsRootDescriptorTable(2, isFloor ? srvHandle2 : srvHandle1);

        mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
    }

    // Переводим G-буфер текстуры в состояние PIXEL_SHADER_RESOURCE для прохода освещения
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Переводим depth buffer в состояние DEPTH_READ
    barriers[GBuffer::GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ);

    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT + 1, barriers);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
}

void RenderingSystem::LightingPass(
    ID3D12Resource* backBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
    const std::vector<Light>& lights,
    const DirectX::XMFLOAT3& cameraPos,
    const D3D12_VIEWPORT& viewport,
    const D3D12_RECT& scissorRect,
    int& currBackBufferIndex,
    IDXGISwapChain* swapChain,
    ID3D12PipelineState* lightingPSO,
    ID3D12RootSignature* lightingRootSignature,
    UploadBuffer<LightConstants>* lightingCB,
    GBuffer* gBuffer)
{
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator, lightingPSO);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);

    mCommandList->SetGraphicsRootSignature(lightingRootSignature);
    ID3D12DescriptorHeap* heaps[] = { gBuffer->mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, gBuffer->mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    D3D12_GPU_VIRTUAL_ADDRESS baseAddr = lightingCB->Resource()->GetGPUVirtualAddress();
    UINT elementSize = lightingCB->GetElementSize();

    for (size_t i = 0; i < lights.size(); ++i)
    {
        LightConstants lightConstants;
        lightConstants.SetFromLight(lights[i], cameraPos);

        lightingCB->CopyData((UINT)i, lightConstants);

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = baseAddr + i * elementSize;
        mCommandList->SetGraphicsRootConstantBufferView(1, cbAddr);

        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->DrawInstanced(3, 1, 0, 0);
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    swapChain->Present(0, 0);
    currBackBufferIndex = (currBackBufferIndex + 1) % mSwapChainBufferCount;
}

void RenderingSystem::Shutdown()
{
    FlushCommandQueue();

    if (mGBuffer)
    {
        mGBuffer->Shutdown();
        mGBuffer.reset();
    }

    mLightingPSO.Reset();
    mLightingRootSignature.Reset();
    mLightingCB.reset();
}

void RenderingSystem::FlushCommandQueue()
{
    static UINT64 fenceValue = 1;

    mCommandQueue->Signal(mFence, fenceValue);

    if (mFence->GetCompletedValue() < fenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    fenceValue++;
}