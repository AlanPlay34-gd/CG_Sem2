#include "DirectXApp.h"

#include "DDSTextureLoader.h"
#include "GameTimer.h"
#include "d3dUtil.h"
#include "model_loader.h"
#include "../h/d3dx12.h"

#include <Windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <random>
#include <functional>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void AppendMeshData(MeshData& dst, const MeshData& src) {
    const unsigned int vertexOffset = static_cast<unsigned int>(dst.vertices.size());
    const unsigned int indexOffset = static_cast<unsigned int>(dst.indices.size());

    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());

    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (unsigned int idx : src.indices) {
        dst.indices.push_back(idx + vertexOffset);
    }

    dst.submeshes.reserve(dst.submeshes.size() + src.submeshes.size());
    for (auto sm : src.submeshes) {
        sm.startIndexLocation += indexOffset;
        sm.baseVertexLocation = 0;
        dst.submeshes.push_back(sm);
    }
}

struct MeshBounds {
    XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    XMFLOAT3 extents = {1.0f, 1.0f, 1.0f};
    float radius = 1.0f;
};

MeshBounds ComputeMeshBounds(const MeshData& mesh) {
    MeshBounds bounds{};
    if (mesh.vertices.empty()) {
        return bounds;
    }

    XMFLOAT3 vMin(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    XMFLOAT3 vMax(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    for (const auto& v : mesh.vertices) {
        vMin.x = (std::min)(vMin.x, v.Position.x);
        vMin.y = (std::min)(vMin.y, v.Position.y);
        vMin.z = (std::min)(vMin.z, v.Position.z);

        vMax.x = (std::max)(vMax.x, v.Position.x);
        vMax.y = (std::max)(vMax.y, v.Position.y);
        vMax.z = (std::max)(vMax.z, v.Position.z);
    }

    bounds.center = XMFLOAT3(
        0.5f * (vMin.x + vMax.x),
        0.5f * (vMin.y + vMax.y),
        0.5f * (vMin.z + vMax.z));
    bounds.extents = XMFLOAT3(
        0.5f * (vMax.x - vMin.x),
        0.5f * (vMax.y - vMin.y),
        0.5f * (vMax.z - vMin.z));

    float radius = 0.0f;
    for (const auto& v : mesh.vertices) {
        const float dx = v.Position.x - bounds.center.x;
        const float dy = v.Position.y - bounds.center.y;
        const float dz = v.Position.z - bounds.center.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        radius = (std::max)(radius, d);
    }

    bounds.radius = (radius > 1e-5f) ? radius : 1.0f;
    return bounds;
}

void NormalizeMeshToRadius(MeshData& mesh, float targetRadius, float yOffset) {
    const MeshBounds originalBounds = ComputeMeshBounds(mesh);
    const float normalizeScale = (originalBounds.radius > 1e-5f) ? (targetRadius / originalBounds.radius) : 1.0f;

    for (auto& v : mesh.vertices) {
        v.Position.x = (v.Position.x - originalBounds.center.x) * normalizeScale;
        v.Position.y = (v.Position.y - originalBounds.center.y) * normalizeScale + yOffset;
        v.Position.z = (v.Position.z - originalBounds.center.z) * normalizeScale;
    }
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    ComPtr<ID3D12Resource>& uploadBuffer) {
    ComPtr<ID3D12Resource> defaultBuffer;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC defaultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &defaultBufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer)),
        "Create default buffer failed");

    const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)),
        "Create upload buffer failed");

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = initData;
    subData.RowPitch = byteSize;
    subData.SlicePitch = subData.RowPitch;

    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &toCopyDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subData);

    auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &toRead);

    return defaultBuffer;
}

bool LoadWicTextureFromFile12(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& uploadHeap) {
    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
    }
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) {
        return false;
    }

    if (FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom))) {
        return false;
    }

    const UINT rowPitch = width * 4u;
    const UINT imageSize = rowPitch * height;
    std::vector<unsigned char> pixels(imageSize);

    if (FAILED(converter->CopyPixels(nullptr, rowPitch, imageSize, pixels.data()))) {
        return false;
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (FAILED(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)))) {
        return false;
    }

    const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    if (FAILED(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap)))) {
        return false;
    }

    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = pixels.data();
    subresource.RowPitch = rowPitch;
    subresource.SlicePitch = imageSize;

    UpdateSubresources(cmdList, texture.Get(), uploadHeap.Get(), 0, 0, 1, &subresource);

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    return true;
}

bool LoadTgaTextureFromFile12(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& uploadHeap) {
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file) {
        return false;
    }

    unsigned char header[18] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file) {
        return false;
    }

    const unsigned char idLength = header[0];
    const unsigned char colorMapType = header[1];
    const unsigned char imageType = header[2];
    const unsigned short width = static_cast<unsigned short>(header[12] | (header[13] << 8));
    const unsigned short height = static_cast<unsigned short>(header[14] | (header[15] << 8));
    const unsigned char bpp = header[16];
    const unsigned char descriptor = header[17];

    if (colorMapType != 0 || width == 0 || height == 0) {
        return false;
    }
    const bool supportedType = (imageType == 2 || imageType == 3 || imageType == 10 || imageType == 11);
    if (!supportedType) {
        return false;
    }
    if (bpp != 8 && bpp != 24 && bpp != 32) {
        return false;
    }

    if (idLength > 0) {
        file.seekg(idLength, std::ios::cur);
        if (!file) {
            return false;
        }
    }

    const unsigned int pixelCount = static_cast<unsigned int>(width) * static_cast<unsigned int>(height);
    std::vector<unsigned char> rgba(pixelCount * 4u, 255u);

    const auto writePixel = [&](unsigned int pixelIndex, const unsigned char* src) {
        const unsigned int dst = pixelIndex * 4u;
        if (bpp == 32) {
            rgba[dst + 0] = src[2];
            rgba[dst + 1] = src[1];
            rgba[dst + 2] = src[0];
            rgba[dst + 3] = src[3];
        } else if (bpp == 24) {
            rgba[dst + 0] = src[2];
            rgba[dst + 1] = src[1];
            rgba[dst + 2] = src[0];
            rgba[dst + 3] = 255;
        } else {
            rgba[dst + 0] = src[0];
            rgba[dst + 1] = src[0];
            rgba[dst + 2] = src[0];
            rgba[dst + 3] = 255;
        }
    };

    const unsigned int bytesPerPixel = bpp / 8u;
    std::vector<unsigned char> pixel(bytesPerPixel);
    unsigned int pixelIndex = 0;

    if (imageType == 2 || imageType == 3) {
        while (pixelIndex < pixelCount) {
            file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
            if (!file) {
                return false;
            }
            writePixel(pixelIndex, pixel.data());
            ++pixelIndex;
        }
    } else {
        while (pixelIndex < pixelCount) {
            unsigned char packetHeader = 0;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);
            if (!file) {
                return false;
            }

            const unsigned int runLength = (packetHeader & 0x7Fu) + 1u;
            if (packetHeader & 0x80u) {
                file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
                if (!file) {
                    return false;
                }
                for (unsigned int i = 0; i < runLength && pixelIndex < pixelCount; ++i, ++pixelIndex) {
                    writePixel(pixelIndex, pixel.data());
                }
            } else {
                for (unsigned int i = 0; i < runLength && pixelIndex < pixelCount; ++i, ++pixelIndex) {
                    file.read(reinterpret_cast<char*>(pixel.data()), bytesPerPixel);
                    if (!file) {
                        return false;
                    }
                    writePixel(pixelIndex, pixel.data());
                }
            }
        }
    }

    const bool topOrigin = (descriptor & 0x20u) != 0u;
    if (!topOrigin) {
        const unsigned int rowPitch = static_cast<unsigned int>(width) * 4u;
        std::vector<unsigned char> flipped(rgba.size());
        for (unsigned int y = 0; y < static_cast<unsigned int>(height); ++y) {
            const unsigned int srcOffset = (static_cast<unsigned int>(height) - 1u - y) * rowPitch;
            const unsigned int dstOffset = y * rowPitch;
            std::copy_n(rgba.data() + srcOffset, rowPitch, flipped.data() + dstOffset);
        }
        rgba.swap(flipped);
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (FAILED(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)))) {
        return false;
    }

    const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    if (FAILED(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap)))) {
        return false;
    }

    const UINT rowPitch = static_cast<UINT>(width) * 4u;
    const UINT imageSize = rowPitch * static_cast<UINT>(height);
    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = rgba.data();
    subresource.RowPitch = rowPitch;
    subresource.SlicePitch = imageSize;

    UpdateSubresources(cmdList, texture.Get(), uploadHeap.Get(), 0, 0, 1, &subresource);

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    return true;
}

} // namespace

DirectXApp::DirectXApp() = default;

DirectXApp::~DirectXApp() {
    if (mDevice) {
        FlushCommandQueue();
    }

    mRenderingSystem.reset();
    mObjectCB.reset();
    mPassCB.reset();
    mLightingCB.reset();

    mTextureResources.clear();
    mTextureNameToIndex.clear();
    mSceneMesh = {};
    mLights.clear();
    mFallingLights.clear();

    mVertexBufferUploader.Reset();
    mIndexBufferUploader.Reset();
    mVertexBufferGPU.Reset();
    mIndexBufferGPU.Reset();
    mDepthStencilBuffer.Reset();
    for (auto& buffer : mSwapChainBuffer) {
        buffer.Reset();
    }

    mCbvSrvHeap.Reset();
    mRtvHeap.Reset();
    mDsvHeap.Reset();
    mCommandList.Reset();
    mDirectCmdListAlloc.Reset();
    mCommandQueue.Reset();
    mFence.Reset();
    mSwapChain.Reset();
    mDevice.Reset();
    mDxgiFactory.Reset();

    // Keep COM initialized until process end to avoid shutdown-time heap corruption
    // from third-party/static COM users that may outlive this object.
    mComInitialized = false;
}

bool DirectXApp::Initialize(HWND hwnd, unsigned int width, unsigned int height) {
    mHwnd = hwnd;
    mClientWidth = width;
    mClientHeight = height;

    if (!InitDirect3D()) {
        return false;
    }

    mRenderingSystem = std::make_unique<RenderingSystem>();
    if (!mRenderingSystem->Initialize(mDevice.Get(), mClientWidth, mClientHeight, mBackBufferFormat, mDepthStencilFormat)) {
        return false;
    }

    BuildScene();

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = {0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight)};

    return true;
}

bool DirectXApp::InitDirect3D() {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comHr)) {
        mComInitialized = true;
    } else if (comHr != RPC_E_CHANGED_MODE) {
        ThrowIfFailed(comHr, "CoInitializeEx failed");
    }

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&mDxgiFactory)), "CreateDXGIFactory2 failed");

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    for (UINT adapterIndex = 0;
         mDxgiFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                  IID_PPV_ARGS(&hardwareAdapter)) != DXGI_ERROR_NOT_FOUND;
         ++adapterIndex) {
        DXGI_ADAPTER_DESC1 desc = {};
        hardwareAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
            break;
        }
        hardwareAdapter.Reset();
    }

    if (!hardwareAdapter) {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(mDxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "EnumWarpAdapter failed");
        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)),
                      "Create WARP device failed");
    } else {
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)),
                      "Create hardware device failed");
    }

    ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)), "Create fence failed");

    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    CreateRenderTargetViews();
    CreateDepthStencilBuffer();

    return true;
}

void DirectXApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&mCommandQueue)), "Create command queue failed");

    ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mDirectCmdListAlloc)),
                  "Create command allocator failed");

    ThrowIfFailed(mDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)),
        "Create command list failed");

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");
}

void DirectXApp::CreateSwapChain() {
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = mHwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(mDxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, &mSwapChain),
                  "Create swap chain failed");
}

void DirectXApp::CreateRtvAndDsvDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)), "Create RTV heap failed");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)), "Create DSV heap failed");
}

void DirectXApp::CreateRenderTargetViews() {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (unsigned int i = 0; i < SwapChainBufferCount; ++i) {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])), "Get swap chain buffer failed");
        mDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
}

void DirectXApp::CreateDepthStencilBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mClientWidth;
    depthDesc.Height = mClientHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = mDepthStencilFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = mDepthStencilFormat;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mDepthStencilBuffer)),
        "Create depth buffer failed");

    mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), nullptr, DepthStencilView());
}

void DirectXApp::OnResize(unsigned int width, unsigned int height) {
    if (!mDevice || !mSwapChain) {
        return;
    }

    mClientWidth = (std::max)(1u, width);
    mClientHeight = (std::max)(1u, height);

    FlushCommandQueue();

    for (auto& buffer : mSwapChainBuffer) {
        buffer.Reset();
    }
    mDepthStencilBuffer.Reset();

    ThrowIfFailed(mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth,
        mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),
        "ResizeBuffers failed");

    mCurrBackBuffer = 0;

    CreateRenderTargetViews();
    CreateDepthStencilBuffer();

    if (mRenderingSystem) {
        mRenderingSystem->OnResize(mDevice.Get(), mClientWidth, mClientHeight);
    }

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;
    mScissorRect = {0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight)};

    if (mObjectCB && mPassCB && mLightingCB) {
        BuildMainSrvHeap();
        BindSubmeshTextures();
    }
}

void DirectXApp::BuildScene() {
    ThrowIfFailed(mDirectCmdListAlloc->Reset(), "Reset command allocator failed");
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr), "Reset command list failed");

    LoadModels();
    BuildGeometryBuffers();
    LoadTextures();
    CreateFallbackTextures();

    BuildScenePresets();
    ActivateScene(0, false);

    BuildConstantBuffers();
    BuildMainSrvHeap();
    BindSubmeshTextures();
    BuildLights();

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();

    mVertexBufferUploader.Reset();
    mIndexBufferUploader.Reset();
}

bool DirectXApp::ResolveModelPath(const std::vector<std::filesystem::path>& candidates, std::filesystem::path& resolved) const {
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            resolved = candidate;
            return true;
        }
    }
    return false;
}

void DirectXApp::LoadModels() {
    mSceneMesh = {};
    mModelAssets.clear();

    struct ModelLoadSpec {
        std::string name;
        std::vector<std::filesystem::path> candidates;
        float targetRadius = 1.0f;
        float yOffset = 0.0f;
        bool forceEarthMaterial = false;
    };

    const std::vector<ModelLoadSpec> loadSpecs = {
        {
            "earth",
            {
                "../assets/Earth.fbx",
                "../assets/earth.fbx"
            },
            3.5f,
            0.0f,
            true
        },
        {
            "sponza",
            {
                "../assets/sponza/sponza.obj",
                "../assets/sponza.obj"
            },
            18.0f,
            0.0f,
            false
        }
    };

    for (const auto& spec : loadSpecs) {
        std::filesystem::path resolvedPath;
        if (!ResolveModelPath(spec.candidates, resolvedPath)) {
            std::string warn = "Model not found for spec: " + spec.name + "\n";
            OutputDebugStringA(warn.c_str());
            continue;
        }

        MeshData mesh = ModelLoader::LoadModel(resolvedPath.u8string(), XMMatrixIdentity());
        NormalizeMeshToRadius(mesh, spec.targetRadius, spec.yOffset);

        if (spec.forceEarthMaterial) {
            for (auto& sm : mesh.submeshes) {
                sm.material.diffuseTextureName = "Earth_ALB";
                sm.material.normalTextureName = "Earth_NORM";
                sm.material.displacementTextureName = "Earth_HEIGHT";
                sm.material.shininess = 64.0f;
            }
        }

        const MeshBounds bounds = ComputeMeshBounds(mesh);
        ModelAsset asset;
        asset.name = spec.name;
        asset.submeshStart = static_cast<unsigned int>(mSceneMesh.submeshes.size());
        asset.submeshCount = static_cast<unsigned int>(mesh.submeshes.size());
        asset.localBoundsCenter = bounds.center;
        asset.localBoundsExtents = bounds.extents;
        asset.localBoundsRadius = bounds.radius;
        mModelAssets.push_back(asset);

        AppendMeshData(mSceneMesh, mesh);

        std::string msg = "Loaded model: " + spec.name + " from " + resolvedPath.u8string() + "\n";
        OutputDebugStringA(msg.c_str());
    }

    if (mModelAssets.empty()) {
        throw std::runtime_error("No scene models were loaded. Check assets paths.");
    }
}

void DirectXApp::BuildScenePresets() {
    mScenePresets.clear();

    auto findModelIndex = [&](const std::string& modelName) -> unsigned int {
        const std::string key = ToLowerAscii(modelName);
        for (unsigned int i = 0; i < static_cast<unsigned int>(mModelAssets.size()); ++i) {
            if (ToLowerAscii(mModelAssets[i].name) == key) {
                return i;
            }
        }
        return 0;
    };

    const unsigned int earthModel = findModelIndex("earth");
    const unsigned int sponzaModel = findModelIndex("sponza");
    const bool hasSponza = ToLowerAscii(mModelAssets[sponzaModel].name) == "sponza";

    ScenePreset lab123Scene;
    lab123Scene.name = L"Scene 1: Legacy Labs 1-3";
    lab123Scene.cameraPos = XMFLOAT3(0.0f, 2.8f, -15.0f);
    lab123Scene.cameraYaw = 0.0f;
    lab123Scene.cameraPitch = 0.0f;
    {
        SceneObject earth;
        earth.modelAssetIndex = earthModel;
        earth.position = XMFLOAT3(0.0f, 0.6f, 0.0f);
        earth.scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
        lab123Scene.objects.push_back(earth);
    }
    mScenePresets.push_back(lab123Scene);

    ScenePreset multiModelScene;
    multiModelScene.name = L"Scene 2: Multi-Model";
    multiModelScene.cameraPos = XMFLOAT3(0.0f, 6.5f, -36.0f);
    multiModelScene.cameraYaw = 0.0f;
    multiModelScene.cameraPitch = -0.08f;
    {
        if (hasSponza) {
            SceneObject sponza;
            sponza.modelAssetIndex = sponzaModel;
            sponza.position = XMFLOAT3(0.0f, -6.0f, 0.0f);
            sponza.scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
            multiModelScene.objects.push_back(sponza);
        }

        const int rows = 9;
        const int cols = 9;
        const float spacing = 7.0f;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                SceneObject earth;
                earth.modelAssetIndex = earthModel;
                earth.position = XMFLOAT3(
                    (static_cast<float>(c) - (cols - 1) * 0.5f) * spacing,
                    1.0f + ((r + c) % 3) * 0.35f,
                    (static_cast<float>(r) - (rows - 1) * 0.5f) * spacing);
                const float s = 0.35f + 0.1f * static_cast<float>((r + c) % 4);
                earth.scale = XMFLOAT3(s, s, s);
                earth.rotationY = 0.25f * static_cast<float>((r * cols + c) % 7);
                multiModelScene.objects.push_back(earth);
            }
        }
    }
    mScenePresets.push_back(multiModelScene);

    ScenePreset lab4Scene;
    lab4Scene.name = L"Scene 3: Lab 4 Stress";
    lab4Scene.cameraPos = XMFLOAT3(0.0f, 8.5f, -46.0f);
    lab4Scene.cameraYaw = 0.0f;
    lab4Scene.cameraPitch = -0.02f;
    {
        if (hasSponza) {
            SceneObject sponza;
            sponza.modelAssetIndex = sponzaModel;
            sponza.position = XMFLOAT3(0.0f, -5.8f, 0.0f);
            sponza.scale = XMFLOAT3(0.85f, 0.85f, 0.85f);
            lab4Scene.objects.push_back(sponza);
        }

        std::mt19937 rng(20260501u);
        std::uniform_real_distribution<float> xzDist(-55.0f, 55.0f);
        std::uniform_real_distribution<float> yDist(0.0f, 5.0f);
        std::uniform_real_distribution<float> scaleDist(0.12f, 0.3f);
        std::uniform_real_distribution<float> rotDist(0.0f, XM_2PI);

        const unsigned int objectCount = 1000;
        for (unsigned int i = 0; i < objectCount; ++i) {
            SceneObject earth;
            earth.modelAssetIndex = earthModel;
            earth.position = XMFLOAT3(xzDist(rng), yDist(rng), xzDist(rng));
            const float s = scaleDist(rng);
            earth.scale = XMFLOAT3(s, s, s);
            earth.rotationY = rotDist(rng);
            lab4Scene.objects.push_back(earth);
        }
    }
    mScenePresets.push_back(lab4Scene);
}

void DirectXApp::ActivateScene(unsigned int sceneIndex, bool rebuildGpuResources) {
    if (mScenePresets.empty()) {
        return;
    }

    mActiveSceneIndex = (std::min)(sceneIndex, static_cast<unsigned int>(mScenePresets.size() - 1));
    const ScenePreset& preset = mScenePresets[mActiveSceneIndex];
    mSceneObjects = preset.objects;
    mEyePos = preset.cameraPos;
    mYaw = preset.cameraYaw;
    mPitch = preset.cameraPitch;
    mFallingLights.clear();
    mAnimateTextures = false;
    mTexAnimU = 0.0f;
    mTexAnimV = 0.0f;
    mTexScaleU = 1.0f;
    mTexScaleV = 1.0f;

    RebuildSceneObjectTransforms();
    BuildOctree();
    mVisibleObjects.resize(mSceneObjects.size());
    for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
        mVisibleObjects[i] = i;
    }
    mLastVisibleCount = static_cast<unsigned int>(mVisibleObjects.size());
    mLastTestedCount = static_cast<unsigned int>(mSceneObjects.size());
    mLastOctreeNodesVisited = 0;

    if (rebuildGpuResources && mObjectCB && mPassCB && mLightingCB) {
        BuildConstantBuffers();
        BuildMainSrvHeap();
        BindSubmeshTextures();
    }

    UpdateWindowTitle();
}

void DirectXApp::RebuildSceneObjectTransforms() {
    for (auto& obj : mSceneObjects) {
        const XMMATRIX world =
            XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z) *
            XMMatrixRotationY(obj.rotationY) *
            XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);
        XMStoreFloat4x4(&obj.world, world);

        const ModelAsset& model = mModelAssets[obj.modelAssetIndex];
        const BoundingBox localBounds(model.localBoundsCenter, model.localBoundsExtents);
        BoundingBox worldBounds;
        localBounds.Transform(worldBounds, world);

        obj.worldBounds = worldBounds;
        obj.worldBoundsCenter = worldBounds.Center;
        obj.worldBoundsExtents = worldBounds.Extents;
        obj.worldBoundsRadius = std::sqrt(
            worldBounds.Extents.x * worldBounds.Extents.x +
            worldBounds.Extents.y * worldBounds.Extents.y +
            worldBounds.Extents.z * worldBounds.Extents.z);
    }
}

void DirectXApp::BuildOctree() {
    mOctreeRoot.reset();
    if (mSceneObjects.empty()) {
        return;
    }

    BoundingBox sceneBounds = mSceneObjects[0].worldBounds;
    for (unsigned int i = 1; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
        BoundingBox::CreateMerged(sceneBounds, sceneBounds, mSceneObjects[i].worldBounds);
    }

    auto makeNode = [](const XMFLOAT3& center, const XMFLOAT3& extents) {
        auto node = std::make_unique<OctreeNode>();
        node->center = center;
        node->extents = extents;
        node->bounds = BoundingBox(center, extents);
        return node;
    };

    const XMFLOAT3 center = sceneBounds.Center;
    XMFLOAT3 extents = sceneBounds.Extents;
    extents.x = (std::max)(extents.x, 1.0f);
    extents.y = (std::max)(extents.y, 1.0f);
    extents.z = (std::max)(extents.z, 1.0f);
    extents.x *= 1.001f;
    extents.y *= 1.001f;
    extents.z *= 1.001f;

    mOctreeRoot = makeNode(center, extents);

    constexpr unsigned int kMaxDepth = 8;
    constexpr unsigned int kMaxLeafObjects = 28;

    const auto fitsInside = [](const SceneObject& object, const BoundingBox& nodeBounds) {
        return nodeBounds.Contains(object.worldBounds) == CONTAINS;
    };

    const auto childCenter = [](const XMFLOAT3& parentCenter, const XMFLOAT3& childExtents, unsigned int childIndex) {
        return XMFLOAT3(
            parentCenter.x + (((childIndex & 1u) != 0u) ? childExtents.x : -childExtents.x),
            parentCenter.y + (((childIndex & 2u) != 0u) ? childExtents.y : -childExtents.y),
            parentCenter.z + (((childIndex & 4u) != 0u) ? childExtents.z : -childExtents.z));
    };

    std::function<void(OctreeNode*, unsigned int, unsigned int)> insertObject;
    insertObject = [&](OctreeNode* node, unsigned int objectIndex, unsigned int depth) {
        if (!node || depth >= kMaxDepth) {
            if (node) {
                node->objectIndices.push_back(objectIndex);
            }
            return;
        }

        const SceneObject& object = mSceneObjects[objectIndex];
        const XMFLOAT3 childExtents(node->extents.x * 0.5f, node->extents.y * 0.5f, node->extents.z * 0.5f);

        unsigned int targetChild = 0;
        targetChild |= (object.worldBoundsCenter.x >= node->center.x) ? 1u : 0u;
        targetChild |= (object.worldBoundsCenter.y >= node->center.y) ? 2u : 0u;
        targetChild |= (object.worldBoundsCenter.z >= node->center.z) ? 4u : 0u;

        const XMFLOAT3 nextCenter = childCenter(node->center, childExtents, targetChild);
        const BoundingBox childBounds(nextCenter, childExtents);
        if (!fitsInside(object, childBounds)) {
            node->objectIndices.push_back(objectIndex);
            return;
        }

        if (!node->children[targetChild]) {
            node->children[targetChild] = makeNode(nextCenter, childExtents);
        }
        insertObject(node->children[targetChild].get(), objectIndex, depth + 1u);
    };

    for (unsigned int objectIndex = 0; objectIndex < static_cast<unsigned int>(mSceneObjects.size()); ++objectIndex) {
        insertObject(mOctreeRoot.get(), objectIndex, 0);
    }

    std::function<void(OctreeNode*, unsigned int)> splitOverloaded;
    splitOverloaded = [&](OctreeNode* node, unsigned int depth) {
        if (!node || depth >= kMaxDepth || node->objectIndices.size() <= kMaxLeafObjects) {
            return;
        }

        const auto toReinsert = node->objectIndices;
        node->objectIndices.clear();
        for (unsigned int idx : toReinsert) {
            insertObject(node, idx, depth);
        }

        for (auto& child : node->children) {
            splitOverloaded(child.get(), depth + 1u);
        }
    };

    splitOverloaded(mOctreeRoot.get(), 0);
}

void DirectXApp::CollectVisibleFromOctree(const OctreeNode* node, const BoundingFrustum& frustum) {
    if (!node) {
        return;
    }

    ++mOctreeNodesVisitedThisFrame;
    if (!frustum.Intersects(node->bounds)) {
        return;
    }

    const ContainmentType nodeContainment = frustum.Contains(node->bounds);
    if (nodeContainment == CONTAINS) {
        std::function<void(const OctreeNode*)> gatherAll;
        gatherAll = [&](const OctreeNode* n) {
            if (!n) {
                return;
            }

            mObjectsTestedThisFrame += static_cast<unsigned int>(n->objectIndices.size());
            for (unsigned int objectIndex : n->objectIndices) {
                mVisibleObjects.push_back(objectIndex);
            }

            for (const auto& child : n->children) {
                gatherAll(child.get());
            }
        };

        gatherAll(node);
        return;
    }

    for (unsigned int objectIndex : node->objectIndices) {
        ++mObjectsTestedThisFrame;
        const SceneObject& object = mSceneObjects[objectIndex];
        if (frustum.Contains(object.worldBounds) != DISJOINT) {
            mVisibleObjects.push_back(objectIndex);
        }
    }

    for (const auto& child : node->children) {
        CollectVisibleFromOctree(child.get(), frustum);
    }
}

void DirectXApp::CollectVisibleObjects(const XMMATRIX& view, const XMMATRIX& proj) {
    mVisibleObjects.clear();
    mObjectsTestedThisFrame = 0;
    mOctreeNodesVisitedThisFrame = 0;

    if (mSceneObjects.empty()) {
        return;
    }

    if (!mFrustumCullingEnabled) {
        mVisibleObjects.reserve(mSceneObjects.size());
        for (unsigned int i = 0; i < static_cast<unsigned int>(mSceneObjects.size()); ++i) {
            mVisibleObjects.push_back(i);
        }
        mObjectsTestedThisFrame = static_cast<unsigned int>(mSceneObjects.size());
        return;
    }

    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, proj);
    BoundingFrustum worldFrustum;
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);
    viewFrustum.Transform(worldFrustum, invView);

    if (mOctreeCullingEnabled && mOctreeRoot) {
        CollectVisibleFromOctree(mOctreeRoot.get(), worldFrustum);
        return;
    }

    mVisibleObjects.reserve(mSceneObjects.size());
    for (unsigned int objectIndex = 0; objectIndex < static_cast<unsigned int>(mSceneObjects.size()); ++objectIndex) {
        ++mObjectsTestedThisFrame;
        const SceneObject& object = mSceneObjects[objectIndex];
        if (worldFrustum.Contains(object.worldBounds) != DISJOINT) {
            mVisibleObjects.push_back(objectIndex);
        }
    }
}

void DirectXApp::UpdateWindowTitle() {
    std::wostringstream ws;
    ws << L"DirectX 12 Framework";
    if (!mScenePresets.empty()) {
        ws << L" | " << mScenePresets[mActiveSceneIndex].name;
    }

    if (mDebugViewMode == 2) {
        ws << L" | F2: Normal Debug";
    } else if (mDebugViewMode == 3) {
        ws << L" | F3: Tess Debug + Wireframe";
    } else {
        ws << L" | F1: Default";
    }

    ws << L" | B: Falling Lights " << (mFallingBallsEnabled ? L"ON" : L"OFF");
    ws << L" | T: Texture Anim " << (mAnimateTextures ? L"ON" : L"OFF");
    ws << L" | G: Disp Wave " << (mDisplacementWaveActive ? L"RUN" : L"READY");
    ws << L" | C: Frustum " << (mFrustumCullingEnabled ? L"ON" : L"OFF");
    ws << L" | O: Octree " << (mOctreeCullingEnabled ? L"ON" : L"OFF");

    ws << L" | Visible " << mLastVisibleCount << L"/" << mSceneObjects.size();
    ws << L" | Tested " << mLastTestedCount;
    if (mOctreeCullingEnabled) {
        ws << L" | OctNodes " << mLastOctreeNodesVisited;
    }

    ws << L" | Scene: [1-3]";
    ws << L" | Lab4 Debug: C/O";
    SetWindowTextW(mHwnd, ws.str().c_str());
}

void DirectXApp::BuildGeometryBuffers() {
    const UINT vbByteSize = static_cast<UINT>(mSceneMesh.vertices.size() * sizeof(Vertex));
    const UINT ibByteSize = static_cast<UINT>(mSceneMesh.indices.size() * sizeof(unsigned int));

    mVertexBufferGPU = CreateDefaultBuffer(mDevice.Get(), mCommandList.Get(), mSceneMesh.vertices.data(), vbByteSize,
                                           mVertexBufferUploader);

    mIndexBufferGPU = CreateDefaultBuffer(mDevice.Get(), mCommandList.Get(), mSceneMesh.indices.data(), ibByteSize,
                                          mIndexBufferUploader);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

void DirectXApp::LoadTextures() {
    mTextureResources.clear();
    mTextureNameToIndex.clear();

    const std::array<std::wstring, 3> dirs = {
        L"../assets/textures/sponza",
        L"../assets/textures/earth",
        L"../assets/textures/Rails"
    };

    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto ext = ToLowerAscii(entry.path().extension().string());
            const bool isDDS = (ext == ".dds");
            const bool isWIC = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff" || ext == ".tga");
            if (!isDDS && !isWIC) {
                continue;
            }

            TextureResource tex;
            tex.path = entry.path().wstring();

            bool loaded = false;
            if (isDDS) {
                const HRESULT hr = DirectX::CreateDDSTextureFromFile12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path.c_str(),
                    tex.resource,
                    tex.uploadHeap);
                loaded = SUCCEEDED(hr);
            } else if (ext == ".tga") {
                loaded = LoadTgaTextureFromFile12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path,
                    tex.resource,
                    tex.uploadHeap);
            } else {
                loaded = LoadWicTextureFromFile12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path,
                    tex.resource,
                    tex.uploadHeap);
            }

            if (!loaded) {
                std::string msg = "Failed to load texture: " + entry.path().string() + "\n";
                OutputDebugStringA(msg.c_str());
                continue;
            }

            const std::string name = ToLowerAscii(entry.path().stem().string());
            if (mTextureNameToIndex.find(name) != mTextureNameToIndex.end()) {
                continue;
            }

            const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
            mTextureNameToIndex[name] = newIndex;
            mTextureResources.push_back(std::move(tex));
        }
    }
}

void DirectXApp::CreateFallbackTextures() {
    auto addSolid = [&](const std::string& key, unsigned int rgba) {
        TextureResource tex;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&tex.resource)),
            "Create fallback texture failed");

        const UINT64 uploadSize = GetRequiredIntermediateSize(tex.resource.Get(), 0, 1);
        const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&tex.uploadHeap)),
            "Create fallback upload failed");

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = &rgba;
        subresource.RowPitch = 4;
        subresource.SlicePitch = 4;

        UpdateSubresources(mCommandList.Get(), tex.resource.Get(), tex.uploadHeap.Get(), 0, 0, 1, &subresource);
        auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
            tex.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &toSrv);

        const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
        mTextureNameToIndex[key] = newIndex;
        mTextureResources.push_back(std::move(tex));

        return newIndex;
    };

    mFallbackDiffuseIndex = addSolid("__fallback_diffuse", 0xFFFFFFFFu);
    mFallbackNormalIndex = addSolid("__fallback_normal", 0xFFFF8080u);
    mFallbackDisplacementIndex = addSolid("__fallback_displacement", 0xFF000000u);
}

void DirectXApp::BuildConstantBuffers() {
    mObjectCbvCount = (std::max)(1u, static_cast<unsigned int>(mSceneObjects.size()));
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(mDevice.Get(), mObjectCbvCount, true);
    mPassCB = std::make_unique<UploadBuffer<PassConstants>>(mDevice.Get(), 1, true);
    mLightingCB = std::make_unique<UploadBuffer<LightingConstants>>(mDevice.Get(), LightingCbElementCount, true);
}

void DirectXApp::BuildMainSrvHeap() {
    const unsigned int textureCount = static_cast<unsigned int>(mTextureResources.size());
    mObjectCbvStart = 0;
    mPassCbvIndex = mObjectCbvStart + mObjectCbvCount;
    mLightingCbvIndex = mPassCbvIndex + 1;
    mTextureSrvStart = mLightingCbvIndex + 1;
    mGBufferSrvStart = mTextureSrvStart + textureCount;

    const unsigned int descriptorCount = mObjectCbvCount + 2 + textureCount + GBuffer::Count;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = descriptorCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvSrvHeap)), "Create SRV heap failed");

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    const unsigned int objectElementSize = mObjectCB->GetElementSize();
    for (unsigned int i = 0; i < mObjectCbvCount; ++i) {
        cbvDesc.BufferLocation = mObjectCB->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(i) * objectElementSize;
        cbvDesc.SizeInBytes = objectElementSize;
        mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
        cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    cbvDesc.BufferLocation = mPassCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    cbvDesc.BufferLocation = mLightingCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(LightingConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    for (unsigned int i = 0; i < textureCount; ++i) {
        auto& tex = mTextureResources[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = tex.resource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = tex.resource->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        mDevice->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpuHandle);
        tex.srvHeapIndex = mTextureSrvStart + i;
        cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferCpu(mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
                                             static_cast<INT>(mGBufferSrvStart),
                                             mCbvSrvUavDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufferGpu(mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                                             static_cast<INT>(mGBufferSrvStart),
                                             mCbvSrvUavDescriptorSize);

    mRenderingSystem->GetGBuffer()->CreateSrvs(
        mDevice.Get(),
        gbufferCpu,
        gbufferGpu,
        mCbvSrvUavDescriptorSize);
}

void DirectXApp::BindSubmeshTextures() {
    auto resolve = [&](const std::string& rawName, unsigned int fallbackIndex) -> unsigned int {
        const std::string key = ToLowerAscii(rawName);
        auto it = mTextureNameToIndex.find(key);
        if (it == mTextureNameToIndex.end()) {
            return mTextureResources[fallbackIndex].srvHeapIndex;
        }
        return mTextureResources[it->second].srvHeapIndex;
    };

    for (auto& submesh : mSceneMesh.submeshes) {
        submesh.material.diffuseSrvHeapIndex = resolve(submesh.material.diffuseTextureName, mFallbackDiffuseIndex);
        submesh.material.normalSrvHeapIndex = resolve(submesh.material.normalTextureName, mFallbackNormalIndex);

        if (submesh.material.displacementTextureName.empty()) {
            submesh.material.displacementSrvHeapIndex = mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
        } else {
            submesh.material.displacementSrvHeapIndex =
                resolve(submesh.material.displacementTextureName, mFallbackDisplacementIndex);
        }
    }
}

void DirectXApp::BuildLights() {
    mLights.clear();

    LightData dir;
    dir.Type = static_cast<unsigned int>(LightType::Directional);
    dir.Direction = XMFLOAT3(-0.25f, -1.0f, 0.35f);
    dir.Color = XMFLOAT3(1.0f, 0.97f, 0.92f);
    dir.Intensity = 1.1f;
    mLights.push_back(dir);

    auto addPoint = [&](const XMFLOAT3& pos, const XMFLOAT3& color, float intensity, float range) {
        LightData point;
        point.Type = static_cast<unsigned int>(LightType::Point);
        point.Position = pos;
        point.Color = color;
        point.Intensity = intensity;
        point.Range = range;
        mLights.push_back(point);
    };

    addPoint(XMFLOAT3(-10.0f, 5.0f, -2.0f), XMFLOAT3(1.0f, 0.35f, 0.35f), 5.5f, 26.0f);
    addPoint(XMFLOAT3(9.0f, 6.0f, 7.0f), XMFLOAT3(0.3f, 0.55f, 1.0f), 4.8f, 24.0f);
    addPoint(XMFLOAT3(0.0f, 10.0f, -11.0f), XMFLOAT3(0.45f, 1.0f, 0.45f), 3.8f, 28.0f);

    auto addSpot = [&](const XMFLOAT3& pos, const XMFLOAT3& dirVec, const XMFLOAT3& color, float intensity, float range, float angle) {
        LightData spot;
        spot.Type = static_cast<unsigned int>(LightType::Spot);
        spot.Position = pos;
        spot.Direction = dirVec;
        spot.Color = color;
        spot.Intensity = intensity;
        spot.Range = range;
        spot.SpotAngle = angle;
        mLights.push_back(spot);
    };

    addSpot(XMFLOAT3(13.0f, 9.0f, 0.0f), XMFLOAT3(-1.0f, -0.75f, 0.0f), XMFLOAT3(1.0f, 0.9f, 0.5f), 2.2f, 34.0f, 0.35f);
    addSpot(XMFLOAT3(-13.0f, 8.0f, 3.0f), XMFLOAT3(1.0f, -0.85f, -0.15f), XMFLOAT3(0.4f, 1.0f, 0.9f), 2.0f, 30.0f, 0.32f);
}

void DirectXApp::UpdateFallingLights(float dt) {
    if (!mFallingBallsEnabled) {
        mFallingLights.clear();
        return;
    }

    static std::mt19937 rng{1337u};
    std::uniform_real_distribution<float> xzDist(-14.0f, 14.0f);
    std::uniform_real_distribution<float> colorDist(0.4f, 1.0f);
    std::uniform_real_distribution<float> speedDist(1.2f, 2.6f);
    std::uniform_real_distribution<float> rangeDist(6.0f, 12.0f);

    mFallingSpawnTimer += dt;
    while (mFallingSpawnTimer >= mFallingSpawnInterval && mFallingLights.size() < mMaxFallingLights) {
        mFallingSpawnTimer -= mFallingSpawnInterval;

        FallingLight light;
        light.position = XMFLOAT3(xzDist(rng), 12.0f + speedDist(rng) * 2.0f, xzDist(rng));
        light.velocity = XMFLOAT3(0.0f, -(3.5f + speedDist(rng)), 0.0f);
        light.color = XMFLOAT3(colorDist(rng), colorDist(rng), colorDist(rng));
        light.intensity = 6.5f + speedDist(rng) * 2.0f;
        light.range = rangeDist(rng);
        light.settled = false;
        mFallingLights.push_back(light);
    }

    constexpr float kGroundY = -0.02f;
    constexpr float kGravity = 8.5f;

    for (auto& light : mFallingLights) {
        if (!light.settled) {
            light.velocity.y -= kGravity * dt;
            light.position.y += light.velocity.y * dt;
            if (light.position.y <= kGroundY) {
                light.position.y = kGroundY;
                light.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
                light.settled = true;
            }
        } else {
            light.intensity -= 1.2f * dt;
            light.range -= 0.7f * dt;
        }
    }

    mFallingLights.erase(
        std::remove_if(mFallingLights.begin(), mFallingLights.end(), [](const FallingLight& light) {
            return light.intensity <= 0.2f || light.range <= 0.8f;
        }),
        mFallingLights.end());
}

void DirectXApp::UpdateCamera(float dt) {
    const float moveSpeed = 5.f;
    const float moveStep = moveSpeed * dt;

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

    XMVECTOR position = XMLoadFloat3(&mEyePos);

    if (GetAsyncKeyState('W') & 0x8000) {
        position = XMVectorAdd(position, XMVectorScale(forward, moveStep));
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        position = XMVectorSubtract(position, XMVectorScale(forward, moveStep));
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        position = XMVectorSubtract(position, XMVectorScale(right, moveStep));
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        position = XMVectorAdd(position, XMVectorScale(right, moveStep));
    }

    XMStoreFloat3(&mEyePos, position);
}

void DirectXApp::Update(const GameTimer& gt) {
    UpdateCamera(gt.DeltaTime());

    const bool f1Down = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    const bool f2Down = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    const bool f3Down = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    const bool bDown = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool tDown = (GetAsyncKeyState('T') & 0x8000) != 0;
    const bool rDown = (GetAsyncKeyState('R') & 0x8000) != 0;
    const bool gDown = (GetAsyncKeyState('G') & 0x8000) != 0;
    const bool cDown = (GetAsyncKeyState('C') & 0x8000) != 0;
    const bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    const bool digit1Down = (GetAsyncKeyState('1') & 0x8000) != 0;
    const bool digit2Down = (GetAsyncKeyState('2') & 0x8000) != 0;
    const bool digit3Down = (GetAsyncKeyState('3') & 0x8000) != 0;
    bool titleDirty = false;

    if (f1Down && !mF1WasDown) {
        mDebugViewMode = 1;
        titleDirty = true;
    }
    if (f2Down && !mF2WasDown) {
        mDebugViewMode = 2;
        titleDirty = true;
    }
    if (f3Down && !mF3WasDown) {
        mDebugViewMode = 3;
        titleDirty = true;
    }

    mF1WasDown = f1Down;
    mF2WasDown = f2Down;
    mF3WasDown = f3Down;

    if (bDown && !mBWasDown) {
        mFallingBallsEnabled = !mFallingBallsEnabled;
        if (!mFallingBallsEnabled) {
            mFallingLights.clear();
        }
        titleDirty = true;
    }
    mBWasDown = bDown;

    if (tDown && !mTWasDown) {
        mAnimateTextures = !mAnimateTextures;
        titleDirty = true;
    }
    mTWasDown = tDown;

    if (rDown && !mRWasDown) {
        mAnimateTextures = false;
        mTexAnimU = 0.0f;
        mTexAnimV = 0.0f;
        mTexScaleU = 1.0f;
        mTexScaleV = 1.0f;
        titleDirty = true;
    }
    mRWasDown = rDown;

    if (gDown && !mGWasDown) {
        mDisplacementWaveActive = true;
        mDisplacementWaveProgress = 0.0f;
        titleDirty = true;
    }
    mGWasDown = gDown;

    if (cDown && !mCWasDown) {
        mFrustumCullingEnabled = !mFrustumCullingEnabled;
        titleDirty = true;
    }
    mCWasDown = cDown;

    if (oDown && !mOWasDown) {
        mOctreeCullingEnabled = !mOctreeCullingEnabled;
        titleDirty = true;
    }
    mOWasDown = oDown;

    if (digit1Down && !mDigit1WasDown) {
        ActivateScene(0, true);
        titleDirty = true;
    }
    mDigit1WasDown = digit1Down;

    if (digit2Down && !mDigit2WasDown) {
        ActivateScene(1, true);
        titleDirty = true;
    }
    mDigit2WasDown = digit2Down;

    if (digit3Down && !mDigit3WasDown) {
        ActivateScene(2, true);
        titleDirty = true;
    }
    mDigit3WasDown = digit3Down;

    auto clampTiling = [](float v) {
        return std::clamp(v, 0.10f, 16.0f);
    };

    const float tileRate = 1.2f; // units per second
    float deltaU = 0.0f;
    float deltaV = 0.0f;

    if (GetAsyncKeyState('Y') & 0x8000) {
        deltaU += tileRate * gt.DeltaTime();
        deltaV += tileRate * gt.DeltaTime();
    }
    if (GetAsyncKeyState('H') & 0x8000) {
        deltaU -= tileRate * gt.DeltaTime();
        deltaV -= tileRate * gt.DeltaTime();
    }
    if (GetAsyncKeyState('U') & 0x8000) {
        deltaU += tileRate * gt.DeltaTime();
    }
    if (GetAsyncKeyState('J') & 0x8000) {
        deltaU -= tileRate * gt.DeltaTime();
    }
    if (GetAsyncKeyState('I') & 0x8000) {
        deltaV += tileRate * gt.DeltaTime();
    }
    if (GetAsyncKeyState('K') & 0x8000) {
        deltaV -= tileRate * gt.DeltaTime();
    }

    if (std::abs(deltaU) > 0.0f || std::abs(deltaV) > 0.0f) {
        mTexScaleU = clampTiling(mTexScaleU + deltaU);
        mTexScaleV = clampTiling(mTexScaleV + deltaV);
        titleDirty = true;
    }

    if (mDisplacementWaveActive) {
        mDisplacementWaveProgress += gt.DeltaTime() * 0.33f;
        if (mDisplacementWaveProgress > 1.15f) {
            mDisplacementWaveActive = false;
            mDisplacementWaveProgress = 0.0f;
            titleDirty = true;
        }
    }

    UpdateFallingLights(gt.DeltaTime());

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR eye = XMLoadFloat3(&mEyePos);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
                                                   static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
                                                   0.1f,
                                                   5000.0f);
    if (mAnimateTextures) {
        mTexAnimU += 0.04f * gt.DeltaTime();
        mTexAnimV += 0.015f * gt.DeltaTime();
        if (mTexAnimU > 1.0f) {
            mTexAnimU -= 1.0f;
        }
        if (mTexAnimV > 1.0f) {
            mTexAnimV -= 1.0f;
        }
    }
    const XMMATRIX texTransform =
        XMMatrixScaling(mTexScaleU, mTexScaleV, 1.0f) *
        XMMatrixTranslation(mTexAnimU, mTexAnimV, 0.0f);

    CollectVisibleObjects(view, proj);

    for (unsigned int objectIndex : mVisibleObjects) {
        const SceneObject& object = mSceneObjects[objectIndex];
        const XMMATRIX world = XMLoadFloat4x4(&object.world);

        ObjectConstants obj = {};
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(world * view * proj));
        XMStoreFloat4x4(&obj.TextureTransform, XMMatrixTranspose(texTransform));
        obj.TotalTime = gt.TotalTime();
        obj.Padding.x = static_cast<float>(mDebugViewMode);
        const float objectScale = (std::max)(object.scale.x, (std::max)(object.scale.y, object.scale.z));
        obj.Padding.y = std::clamp(0.06f * objectScale, 0.008f, 0.06f);
        obj.Padding.z = 0.0f;
        obj.WaveParams = XMFLOAT4(
            mDisplacementWaveProgress,
            5.0f,
            0.08f,
            mDisplacementWaveActive ? 1.0f : 0.0f);
        obj.ObjectCenter = XMFLOAT4(
            object.worldBoundsCenter.x,
            object.worldBoundsCenter.y,
            object.worldBoundsCenter.z,
            1.0f);
        mObjectCB->CopyData(static_cast<int>(objectIndex), obj);
    }

    PassConstants pass = {};
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&pass.InvViewProj, XMMatrixTranspose(invViewProj));
    pass.EyePosW = mEyePos;
    pass.AmbientColor = XMFLOAT4(0.08f, 0.08f, 0.1f, 1.0f);
    mPassCB->CopyData(0, pass);

    const unsigned int newVisibleCount = static_cast<unsigned int>(mVisibleObjects.size());
    const unsigned int newTestedCount = mObjectsTestedThisFrame;
    const unsigned int newOctreeNodeCount = mOctreeNodesVisitedThisFrame;
    if (newVisibleCount != mLastVisibleCount ||
        newTestedCount != mLastTestedCount ||
        newOctreeNodeCount != mLastOctreeNodesVisited) {
        titleDirty = true;
    }

    mLastVisibleCount = newVisibleCount;
    mLastTestedCount = newTestedCount;
    mLastOctreeNodesVisited = newOctreeNodeCount;

    if (titleDirty || mDebugViewMode != mLastTitleMode) {
        UpdateWindowTitle();
        mLastTitleMode = mDebugViewMode;
    }
}

void DirectXApp::Draw(const GameTimer&) {
    if (mSceneMesh.submeshes.empty() || mSceneMesh.indices.empty() || mSceneMesh.vertices.empty()) {
        OutputDebugStringA("Draw skipped: mesh is empty.\n");
        return;
    }
    FlushCommandQueue();

    ThrowIfFailed(mDirectCmdListAlloc->Reset(), "Reset command allocator failed");
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr), "Reset command list failed");

    ID3D12DescriptorHeap* descriptorHeaps[] = {mCbvSrvHeap.Get()};
    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    auto* gbuffer = mRenderingSystem->GetGBuffer();

    std::array<D3D12_RESOURCE_BARRIER, GBuffer::Count> toRT{};
    for (unsigned int i = 0; i < GBuffer::Count; ++i) {
        toRT[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            gbuffer->GetTexture(static_cast<GBuffer::TextureType>(i)),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    mCommandList->ResourceBarrier(static_cast<UINT>(toRT.size()), toRT.data());

    D3D12_CPU_DESCRIPTOR_HANDLE gbuffRtvs[3] = {
        gbuffer->GetRtv(GBuffer::Albedo),
        gbuffer->GetRtv(GBuffer::Normal),
        gbuffer->GetRtv(GBuffer::Depth)
    };

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    gbuffer->Clear(mCommandList.Get());
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                        1.0f, 0, 0, nullptr);

    auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(3, gbuffRtvs, FALSE, &dsv);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetGeometryRootSignature());

    mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrvHandle(mPassCbvIndex));

    for (unsigned int objectIndex : mVisibleObjects) {
        const SceneObject& object = mSceneObjects[objectIndex];
        const ModelAsset& model = mModelAssets[object.modelAssetIndex];
        mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mObjectCbvStart + objectIndex));

        const float dx = object.worldBoundsCenter.x - mEyePos.x;
        const float dy = object.worldBoundsCenter.y - mEyePos.y;
        const float dz = object.worldBoundsCenter.z - mEyePos.z;
        const float objectDistSq = dx * dx + dy * dy + dz * dz;
        const bool objectNearForTess = (objectDistSq <= (55.0f * 55.0f));

        for (unsigned int submeshOffset = 0; submeshOffset < model.submeshCount; ++submeshOffset) {
            const Submesh& submesh = mSceneMesh.submeshes[model.submeshStart + submeshOffset];
            const bool hasDisplacementTexture = !submesh.material.displacementTextureName.empty() &&
                                                submesh.material.displacementSrvHeapIndex !=
                                                    mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
            const bool hasDisplacement = hasDisplacementTexture && objectNearForTess;
            const bool wireframeDebug = (mDebugViewMode == 3);

            if (hasDisplacement) {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetTessellationWirePSO()
                                                   : mRenderingSystem->GetTessellationPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
            } else {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetGeometryWirePSO()
                                                   : mRenderingSystem->GetGeometryPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrvHandle(submesh.material.diffuseSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrvHandle(submesh.material.normalSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrvHandle(submesh.material.displacementSrvHeapIndex));

            mCommandList->DrawIndexedInstanced(
                submesh.indexCount,
                1,
                submesh.startIndexLocation,
                submesh.baseVertexLocation,
                0);
        }
    }

    std::array<D3D12_RESOURCE_BARRIER, GBuffer::Count> toSrv{};
    for (unsigned int i = 0; i < GBuffer::Count; ++i) {
        toSrv[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            gbuffer->GetTexture(static_cast<GBuffer::TextureType>(i)),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    mCommandList->ResourceBarrier(static_cast<UINT>(toSrv.size()), toSrv.data());

    auto bbToRt = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &bbToRt);

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto rtv = CurrentBackBufferView();
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);

    mCommandList->SetPipelineState(mRenderingSystem->GetLightingPSO());
    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetLightingRootSignature());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mGBufferSrvStart));
    mCommandList->SetGraphicsRootConstantBufferView(1, mPassCB->Resource()->GetGPUVirtualAddress());

    const unsigned int lightElementSize = mLightingCB->GetElementSize();
    unsigned int lightCbIndex = 0;
    const auto lightingCbAddress = [&](unsigned int index) -> D3D12_GPU_VIRTUAL_ADDRESS {
        return mLightingCB->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(index) * lightElementSize;
    };

    // Deferred lighting layers with additive blending: ambient + one draw per light.
    LightingConstants ambientConst = {};
    ambientConst.EnableAmbient = 1;
    mLightingCB->CopyData(static_cast<int>(lightCbIndex), ambientConst);
    mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
    mCommandList->DrawInstanced(3, 1, 0, 0);
    ++lightCbIndex;

    for (const auto& light : mLights) {
        if (lightCbIndex >= LightingCbElementCount) {
            break;
        }

        LightingConstants lightConst = {};
        lightConst.EnableAmbient = 0;
        lightConst.Light = light;
        mLightingCB->CopyData(static_cast<int>(lightCbIndex), lightConst);
        mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
        mCommandList->DrawInstanced(3, 1, 0, 0);
        ++lightCbIndex;
    }

    for (const auto& fl : mFallingLights) {
        if (lightCbIndex >= LightingCbElementCount) {
            break;
        }

        LightingConstants lightConst = {};
        lightConst.EnableAmbient = 0;
        lightConst.Light.Type = static_cast<unsigned int>(LightType::Point);
        lightConst.Light.Position = fl.position;
        lightConst.Light.Color = fl.color;
        lightConst.Light.Intensity = fl.intensity;
        lightConst.Light.Range = fl.range;
        mLightingCB->CopyData(static_cast<int>(lightCbIndex), lightConst);
        mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
        mCommandList->DrawInstanced(3, 1, 0, 0);
        ++lightCbIndex;
    }

    auto bbToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &bbToPresent);

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");

    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    ThrowIfFailed(mSwapChain->Present(1, 0), "Present failed");
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

LRESULT DirectXApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_RBUTTONDOWN:
        mLastMousePos.x = GET_X_LPARAM(lParam);
        mLastMousePos.y = GET_Y_LPARAM(lParam);
        SetCapture(mHwnd);
        return 0;

    case WM_RBUTTONUP:
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (wParam & MK_RBUTTON) {
            const float dx = XMConvertToRadians(0.25f * static_cast<float>(GET_X_LPARAM(lParam) - mLastMousePos.x));
            const float dy = XMConvertToRadians(0.25f * static_cast<float>(GET_Y_LPARAM(lParam) - mLastMousePos.y));

            mYaw += dx;
            mPitch += dy;
            mPitch = std::clamp(mPitch, -1.45f, 1.45f);
        }

        mLastMousePos.x = GET_X_LPARAM(lParam);
        mLastMousePos.y = GET_Y_LPARAM(lParam);
        return 0;

    case WM_SIZE:
        if (mDevice && wParam != SIZE_MINIMIZED) {
            OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void DirectXApp::FlushCommandQueue() {
    ++mCurrentFence;

    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence), "Signal fence failed");

    if (mFence->GetCompletedValue() < mCurrentFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle), "SetEventOnCompletion failed");
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::CurrentBackBufferView() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(mCurrBackBuffer),
        mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::DepthStencilView() const {
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

ID3D12Resource* DirectXApp::CurrentBackBuffer() const {
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXApp::GetGpuSrvHandle(unsigned int heapIndex) const {
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        static_cast<INT>(heapIndex),
        mCbvSrvUavDescriptorSize);
}
