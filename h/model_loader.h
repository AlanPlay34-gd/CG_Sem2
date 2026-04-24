#pragma once
#include "mesh_data.h"
#include <string>
#include <assimp/scene.h>

class ModelLoader
{
public:
    ModelLoader(float scale = 1.0f);
    MeshData loadModel(const std::string& fileName);

private:
    float mScale;
    void parseNode(const aiNode* node, const aiScene* scene,
                   const aiMatrix4x4& parentTransform, MeshData& meshData);
    void parseMesh(const aiMesh* mesh, const aiMatrix4x4& transform,
                   MeshData& meshData, const aiScene* scene);
};