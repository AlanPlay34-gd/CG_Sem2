#pragma once

#include "Light.h"
#include "UploadBuffer.h"
#include "mesh_data.h"
#include "RenderingSystem.h"

#include <array>
#include <DirectXCollision.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct ObjectConstants {
    XMFLOAT4X4 WorldViewProj = {};
    XMFLOAT4X4 World = {};
    XMFLOAT4X4 TextureTransform = {};
    float TotalTime = 0.0f;
    XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
    XMFLOAT4 WaveParams = {0.0f, 0.0f, 0.0f, 0.0f};
    XMFLOAT4 ObjectCenter = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct PassConstants {
    XMFLOAT4X4 View = {};
    XMFLOAT4X4 InvViewProj = {};
    XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
    float Padding = 0.0f;
    XMFLOAT4 AmbientColor = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct ShadowPassConstants {
    XMFLOAT4X4 LightViewProj = {};
};

struct ShadowConstants {
    XMFLOAT4X4 LightViewProj[4] = {};
    XMFLOAT4 CascadeSplits = {0.0f, 0.0f, 0.0f, 0.0f};
    XMFLOAT3 LightDirection = {0.0f, -1.0f, 0.0f};
    float ShadowStrength = 1.0f;
    float DepthBias = 0.0012f;
    float NormalBias = 0.0035f;
    XMFLOAT2 Padding = {0.0f, 0.0f};
};

struct ParticleSimConstants {
    float Dt = 0.0f;
    float TotalTime = 0.0f;
    unsigned int AliveCount = 0;
    unsigned int SpawnCount = 0;

    XMFLOAT3 EmitterPos = {0.0f, 2.0f, 0.0f};
    float BaseSize = 0.16f;

    XMFLOAT3 EmitterVelocity = {0.0f, 0.0f, 0.0f};
    float Gravity = 3.2f;

    float LifeMin = 1.1f;
    float LifeMax = 2.2f;
    float SpeedMin = 1.2f;
    float SpeedMax = 3.0f;

    unsigned int MaxParticles = 0;
    XMFLOAT3 Padding0 = {0.0f, 0.0f, 0.0f};

    XMFLOAT3 CollisionCenter = {0.0f, 0.0f, 0.0f};
    float CollisionRadius = 0.0f;

    float Restitution = 0.55f;
    XMFLOAT3 Padding1 = {0.0f, 0.0f, 0.0f};
};

struct ParticleRenderConstants {
    XMFLOAT4X4 ViewProj = {};
    XMFLOAT3 CameraRight = {1.0f, 0.0f, 0.0f};
    float RenderSizeScale = 1.0f;
    XMFLOAT3 CameraUp = {0.0f, 1.0f, 0.0f};
    float AlphaDiscard = 0.5f;
};

class GameTimer;

class DirectXApp {
public:
    DirectXApp();
    ~DirectXApp();

    bool Initialize(HWND hwnd, unsigned int width, unsigned int height);

    void OnResize(unsigned int width, unsigned int height);
    void Update(const GameTimer& gt);
    void Draw(const GameTimer& gt);

    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    struct ParticleGpu {
        XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
        float Size = 0.16f;
        XMFLOAT3 Velocity = {0.0f, 0.0f, 0.0f};
        float Age = 0.0f;
        XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
        float Lifetime = 1.0f;
        XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
    };

    struct ModelAsset;
    struct SceneObject;
    struct ScenePreset;
    struct OctreeNode;

    bool InitDirect3D();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();

    void BuildScene();
    void LoadModels();
    void BuildGeometryBuffers();
    void LoadTextures();
    void CreateFallbackTextures();
    void BindSubmeshTextures();
    void BuildScenePresets();
    void ActivateScene(unsigned int sceneIndex, bool rebuildGpuResources);
    void RebuildSceneObjectTransforms();
    void BuildOctree();
    void CollectVisibleObjects(const XMMATRIX& view, const XMMATRIX& proj);
    void CollectVisibleFromOctree(const OctreeNode* node, const BoundingFrustum& frustum);
    void UpdateWindowTitle();

    void BuildConstantBuffers();
    void BuildShadowResources();
    void BuildMainSrvHeap();
    void BuildLights();
    void UpdateFallingLights(float dt);
    float ComputeLodAnimationTime(float dt, float totalTime, float distanceToCamera, float& timeAccum, float& stepAccum);
    void UpdateAnimatedObjects(float dt, float totalTime);
    void InitializeParticleResources();
    void ResetParticlesForActiveScene();
    void UpdateParticles(float dt, float totalTime);
    void ExecuteParticleSimulation(ID3D12GraphicsCommandList* cmdList);
    void DrawParticles(ID3D12GraphicsCommandList* cmdList, const XMMATRIX& view, const XMMATRIX& proj,
                       D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    void UpdateParticleCounterFromReadback();
    void UpdateSceneObjectTransform(SceneObject& obj);
    void UpdateShadowCascades(const XMMATRIX& view);
    void RenderShadowMaps(const XMMATRIX& view);

    void UpdateCamera(float dt);

    void FlushCommandQueue();

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE ShadowCascadeDsv(unsigned int cascadeIndex) const;
    ID3D12Resource* CurrentBackBuffer() const;

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(unsigned int heapIndex) const;
    bool ResolveModelPath(const std::vector<std::filesystem::path>& candidates, std::filesystem::path& resolved) const;

private:
    struct ModelAsset {
        std::string name;
        unsigned int submeshStart = 0;
        unsigned int submeshCount = 0;
        XMFLOAT3 localBoundsCenter = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 localBoundsExtents = {1.0f, 1.0f, 1.0f};
        float localBoundsRadius = 1.0f;
    };

    struct SceneObject {
        unsigned int modelAssetIndex = 0;
        XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};
        float rotationY = 0.0f;
        XMFLOAT4X4 world = {};
        XMFLOAT3 worldBoundsCenter = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 worldBoundsExtents = {1.0f, 1.0f, 1.0f};
        float worldBoundsRadius = 1.0f;
        BoundingBox worldBounds = {};
    };

    struct ScenePreset {
        std::wstring name;
        std::vector<SceneObject> objects;
        XMFLOAT3 cameraPos = {0.0f, 2.0f, -12.0f};
        float cameraYaw = 0.0f;
        float cameraPitch = 0.0f;
    };

    struct OctreeNode {
        XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 extents = {0.0f, 0.0f, 0.0f};
        std::vector<unsigned int> objectIndices;
        std::array<std::unique_ptr<OctreeNode>, 8> children;
        BoundingBox bounds = {};
    };

    struct TextureResource {
        std::wstring path;
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> uploadHeap;
        unsigned int srvHeapIndex = 0;
    };

    struct FallingLight {
        XMFLOAT3 position = {0.0f, 5.0f, 0.0f};
        XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 color = {1.0f, 0.7f, 0.3f};
        float intensity = 16.0f;
        float range = 10.0f;
        bool settled = false;
    };

    struct AnimatedObjectTrack {
        int objectIndex = -1;
        XMFLOAT3 basePosition = {0.0f, 0.0f, 0.0f};
        float halfWidth = 2.0f;
        float speed = 2.2f;
        float phaseOffset = 0.0f;
        float timeAccum = 0.0f;
        float stepAccum = 0.0f;
    };

private:
    static constexpr unsigned int SwapChainBufferCount = 2;
    static constexpr unsigned int LightingCbElementCount = 2048;
    static constexpr unsigned int ShadowCascadeCount = 4;
    static constexpr unsigned int ShadowMapSize = 2048;

    HWND mHwnd = nullptr;
    unsigned int mClientWidth = 1280;
    unsigned int mClientHeight = 720;

    ComPtr<IDXGIFactory6> mDxgiFactory;
    ComPtr<IDXGISwapChain> mSwapChain;
    ComPtr<ID3D12Device> mDevice;

    ComPtr<ID3D12Fence> mFence;
    unsigned long long mCurrentFence = 0;
    bool mComInitialized = false;

    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    std::array<ComPtr<ID3D12Resource>, SwapChainBufferCount> mSwapChainBuffer;
    ComPtr<ID3D12Resource> mDepthStencilBuffer;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mCbvSrvHeap;

    unsigned int mRtvDescriptorSize = 0;
    unsigned int mDsvDescriptorSize = 0;
    unsigned int mCbvSrvUavDescriptorSize = 0;

    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    D3D12_VIEWPORT mScreenViewport = {};
    D3D12_RECT mScissorRect = {};

    unsigned int mCurrBackBuffer = 0;

    std::unique_ptr<RenderingSystem> mRenderingSystem;

    MeshData mSceneMesh;
    ComPtr<ID3D12Resource> mVertexBufferGPU;
    ComPtr<ID3D12Resource> mVertexBufferUploader;
    ComPtr<ID3D12Resource> mIndexBufferGPU;
    ComPtr<ID3D12Resource> mIndexBufferUploader;

    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};

    std::vector<TextureResource> mTextureResources;
    std::unordered_map<std::string, unsigned int> mTextureNameToIndex;

    unsigned int mFallbackDiffuseIndex = 0;
    unsigned int mFallbackNormalIndex = 0;
    unsigned int mFallbackDisplacementIndex = 0;

    unsigned int mObjectCbvStart = 0;
    unsigned int mObjectCbvCount = 0;
    unsigned int mPassCbvIndex = 0;
    unsigned int mLightingCbvIndex = 0;
    unsigned int mTextureSrvStart = 0;
    unsigned int mGBufferSrvStart = 0;
    unsigned int mShadowSrvIndex = 0;
    unsigned int mShadowOverlaySrvIndex = 0;
    unsigned int mParticleSrvStart = 0;
    unsigned int mParticleUavStart = 0;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::unique_ptr<UploadBuffer<PassConstants>> mPassCB;
    std::unique_ptr<UploadBuffer<LightingConstants>> mLightingCB;
    std::unique_ptr<UploadBuffer<ShadowPassConstants>> mShadowPassCB;
    std::unique_ptr<UploadBuffer<ShadowConstants>> mShadowCB;

    std::vector<LightData> mLights;
    std::vector<FallingLight> mFallingLights;

    ComPtr<ID3D12Resource> mShadowMap;
    D3D12_RESOURCE_STATES mShadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    std::array<XMFLOAT4X4, ShadowCascadeCount> mShadowViewProj = {};
    std::array<float, ShadowCascadeCount> mShadowSplitDepths = {1.0f, 1.0f, 1.0f, 1.0f};
    XMFLOAT3 mShadowLightDirection = {-0.25f, -1.0f, 0.35f};
    float mShadowSplitLambda = 0.72f;
    float mShadowMaxDistance = 180.0f;

    bool mFallingBallsEnabled = false;
    float mFallingSpawnTimer = 0.0f;
    float mFallingSpawnInterval = 0.02f;
    unsigned int mMaxFallingLights = 1000;

    XMFLOAT3 mEyePos = {0.0f, 2.0f, -12.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    POINT mLastMousePos = {0, 0};

    int mDebugViewMode = 1; // F1: default, F2: world normal debug, F3: tessellation factor + wireframe debug
    bool mF1WasDown = false;
    bool mF2WasDown = false;
    bool mF3WasDown = false;
    bool mBWasDown = false;
    bool mTWasDown = false;
    bool mRWasDown = false;
    bool mGWasDown = false;
    bool mAnimateTextures = false;
    bool mDisplacementWaveActive = false;
    float mDisplacementWaveProgress = 0.0f;
    float mTexAnimU = 0.0f;
    float mTexAnimV = 0.0f;
    float mTexScaleU = 1.0f;
    float mTexScaleV = 1.0f;
    int mLastTitleMode = -1;

    std::vector<ModelAsset> mModelAssets;
    std::vector<ScenePreset> mScenePresets;
    std::vector<SceneObject> mSceneObjects;
    std::vector<unsigned int> mVisibleObjects;
    std::unique_ptr<OctreeNode> mOctreeRoot;

    bool mFrustumCullingEnabled = true;
    bool mOctreeCullingEnabled = false;
    bool mCWasDown = false;
    bool mOWasDown = false;
    bool mDigit1WasDown = false;
    bool mDigit2WasDown = false;
    bool mDigit3WasDown = false;
    bool mDigit4WasDown = false;
    bool mDigit5WasDown = false;

    unsigned int mActiveSceneIndex = 0;
    unsigned int mLastVisibleCount = 0;
    unsigned int mLastTestedCount = 0;
    unsigned int mLastOctreeNodesVisited = 0;
    unsigned int mObjectsTestedThisFrame = 0;
    unsigned int mOctreeNodesVisitedThisFrame = 0;

    std::vector<AnimatedObjectTrack> mAnimatedTracks;

    static constexpr unsigned int ParticleBufferCount = 2;
    unsigned int mParticleMaxCount = 1056;

    std::array<ComPtr<ID3D12Resource>, ParticleBufferCount> mParticleBuffers;
    std::array<ComPtr<ID3D12Resource>, ParticleBufferCount> mParticleCounters;
    std::array<D3D12_RESOURCE_STATES, ParticleBufferCount> mParticleBufferStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    std::array<D3D12_RESOURCE_STATES, ParticleBufferCount> mParticleCounterStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    ComPtr<ID3D12Resource> mParticleCounterResetUpload;
    ComPtr<ID3D12Resource> mParticleCounterReadback;

    std::unique_ptr<UploadBuffer<ParticleSimConstants>> mParticleSimCB;
    std::unique_ptr<UploadBuffer<ParticleRenderConstants>> mParticleRenderCB;

    bool mParticleSourceIndexA = true;
    unsigned int mParticleAliveCount = 0;
    unsigned int mParticleSpawnCount = 0;
    bool mParticleReadbackValid = false;
    float mParticleSpawnAccumulator = 0.0f;
    XMFLOAT3 mParticleEmitterPos = {0.0f, 0.0f, 0.0f};
};
