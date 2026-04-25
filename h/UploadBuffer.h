#pragma once

#include "d3dUtil.h"
#include "../h/d3dx12.h"

#include <wrl.h>
#include <cstring>
#include <d3d12.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

template<typename T>
class UploadBuffer {
public:
    UploadBuffer(ID3D12Device* device, unsigned int elementCount, bool isConstantBuffer)
        : mIsConstantBuffer(isConstantBuffer) {
        mElementCount = elementCount;
        mElementByteSize = sizeof(T);

        if (isConstantBuffer) {
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
        }
        const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC uploadDesc =
            CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(mElementByteSize) * elementCount);

        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)),
            "Create upload buffer failed");

        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)),
                      "Map upload buffer failed");
    }

    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;

    ~UploadBuffer() {
        if (mUploadBuffer) {
            mUploadBuffer->Unmap(0, nullptr);
        }
        mMappedData = nullptr;
    }

    ID3D12Resource* Resource() const {
        return mUploadBuffer.Get();
    }

    unsigned int GetElementSize() const {
        return mElementByteSize;
    }

    void CopyData(int elementIndex, const T& data) {
        if (elementIndex < 0 || static_cast<unsigned int>(elementIndex) >= mElementCount) {
            throw std::runtime_error("UploadBuffer::CopyData index out of range");
        }
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    ComPtr<ID3D12Resource> mUploadBuffer;
    unsigned char* mMappedData = nullptr;

    unsigned int mElementCount = 0;
    unsigned int mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
