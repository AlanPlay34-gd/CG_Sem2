#pragma once
#include <cstdint>
#include <intsafe.h>
#include <string>
#include <vector>
#include "Vertex.h"

struct SubmeshMaterial
{
    std::string diffuseTextureName;
    std::string normalTextureName;
    std::string displacementTextureName;
    float shininess = 32.0f;

    // Индексы в SRV куче (заполняются в bindMaterialsToTextures)
    UINT diffuseSrvHeapIndex = 0;
    UINT normalSrvHeapIndex = 0;
    UINT displacementSrvHeapIndex = 0;
};

struct Submesh
{
    uint32_t indexCount = 0;
    uint32_t startIndiceIndex = 0;
    uint32_t startVerticeIndex = 0;
    SubmeshMaterial material;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
};