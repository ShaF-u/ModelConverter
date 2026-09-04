// FBX(その他Assimp対応形式)をエンジン用バイナリ(.mdl)に変換するオフラインツール。
// ロジックは Engine/Source/Graphics/Model/ModelLoader/ModelLoader.cpp の
// ModelLoader::ProcessMesh / ProcessMaterial を踏襲している。
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "BinaryModelFormat.hpp"

namespace fs = std::filesystem;

namespace
{
    struct MeshData
    {
        std::vector<ModelBinary::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string materialName = "Default";
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        std::string textureName; // 空文字ならテクスチャなし
    };

    std::string ExtractFileName(const std::string& rawPath)
    {
        auto slashPos = rawPath.find_last_of("/\\");
        return (slashPos != std::string::npos) ? rawPath.substr(slashPos + 1) : rawPath;
    }

    MeshData ProcessMesh(const aiMesh* aimesh, const aiScene* scene)
    {
        MeshData mesh;
        mesh.vertices.reserve(aimesh->mNumVertices);

        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i)
        {
            ModelBinary::Vertex v{};

            v.position[0] = aimesh->mVertices[i].x;
            v.position[1] = aimesh->mVertices[i].y;
            v.position[2] = aimesh->mVertices[i].z;

            if (aimesh->HasNormals())
            {
                v.normal[0] = aimesh->mNormals[i].x;
                v.normal[1] = aimesh->mNormals[i].y;
                v.normal[2] = aimesh->mNormals[i].z;
            }

            if (aimesh->mTextureCoords[0])
            {
                v.uv[0] = aimesh->mTextureCoords[0][i].x;
                v.uv[1] = aimesh->mTextureCoords[0][i].y;
            }

            if (aimesh->HasTangentsAndBitangents())
            {
                v.tangent[0] = aimesh->mTangents[i].x;
                v.tangent[1] = aimesh->mTangents[i].y;
                v.tangent[2] = aimesh->mTangents[i].z;
                v.bitangent[0] = aimesh->mBitangents[i].x;
                v.bitangent[1] = aimesh->mBitangents[i].y;
                v.bitangent[2] = aimesh->mBitangents[i].z;
            }

            mesh.vertices.push_back(v);
        }

        for (unsigned int i = 0; i < aimesh->mNumFaces; ++i)
        {
            const aiFace& face = aimesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                mesh.indices.push_back(face.mIndices[j]);
            }
        }

        if (aimesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* aiMat = scene->mMaterials[aimesh->mMaterialIndex];

            aiString name;
            aiMat->Get(AI_MATKEY_NAME, name);
            mesh.materialName = name.C_Str();

            static const aiTextureType kColorTextureTypes[] = {
                aiTextureType_DIFFUSE,
                aiTextureType_BASE_COLOR,
                aiTextureType_EMISSIVE,
            };

            aiTextureType colorTextureType = aiTextureType_NONE;
            for (aiTextureType type : kColorTextureTypes)
            {
                if (aiMat->GetTextureCount(type) > 0)
                {
                    colorTextureType = type;
                    break;
                }
            }

            aiColor4D matchedColor;
            bool gotMatchedColor = false;
            if (colorTextureType == aiTextureType_EMISSIVE)
            {
                gotMatchedColor = (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, matchedColor) == AI_SUCCESS);
            }
            else if (colorTextureType == aiTextureType_BASE_COLOR)
            {
                gotMatchedColor = (aiMat->Get(AI_MATKEY_BASE_COLOR, matchedColor) == AI_SUCCESS);
            }
            else if (colorTextureType == aiTextureType_DIFFUSE)
            {
                gotMatchedColor = (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, matchedColor) == AI_SUCCESS);
            }

            if (gotMatchedColor &&
                (matchedColor.r > 0.0f || matchedColor.g > 0.0f || matchedColor.b > 0.0f))
            {
                mesh.color[0] = matchedColor.r;
                mesh.color[1] = matchedColor.g;
                mesh.color[2] = matchedColor.b;
                mesh.color[3] = matchedColor.a;
            }

            if (colorTextureType != aiTextureType_NONE)
            {
                aiString texPath;
                aiMat->GetTexture(colorTextureType, 0, &texPath);
                // texPath.C_Str() は元マシンの非ASCIIバイトを含む場合があるため、
                // std::filesystem::path を経由せず手動でファイル名だけ取り出す。
                mesh.textureName = ExtractFileName(texPath.C_Str());
            }
        }

        return mesh;
    }

    void WriteString(std::ofstream& out, const std::string& s)
    {
        uint32_t len = static_cast<uint32_t>(s.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0)
        {
            out.write(s.data(), static_cast<std::streamsize>(len));
        }
    }

    bool WriteBinary(const fs::path& outPath, const std::vector<MeshData>& meshes)
    {
        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "出力ファイルを開けません: " << outPath.string() << "\n";
            return false;
        }

        out.write(ModelBinary::kMagic, sizeof(ModelBinary::kMagic));

        uint32_t version = ModelBinary::kVersion;
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));

        uint32_t meshCount = static_cast<uint32_t>(meshes.size());
        out.write(reinterpret_cast<const char*>(&meshCount), sizeof(meshCount));

        for (const auto& mesh : meshes)
        {
            uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
            out.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
            out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

            out.write(reinterpret_cast<const char*>(mesh.vertices.data()),
                static_cast<std::streamsize>(vertexCount * sizeof(ModelBinary::Vertex)));
            out.write(reinterpret_cast<const char*>(mesh.indices.data()),
                static_cast<std::streamsize>(indexCount * sizeof(uint32_t)));

            WriteString(out, mesh.materialName);
            out.write(reinterpret_cast<const char*>(mesh.color), sizeof(mesh.color));
            WriteString(out, mesh.textureName);
        }

        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "使い方: ModelConverter.exe <input.fbx> [output.mdl]\n";
        return 1;
    }

    fs::path inputPath(argv[1]);
    fs::path outputPath = (argc >= 3)
        ? fs::path(argv[2])
        : fs::path(inputPath).replace_extension(".mdl");

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        inputPath.string(),
        aiProcess_Triangulate |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cerr << "モデルの読み込みに失敗: " << inputPath.string()
                   << " - " << importer.GetErrorString() << "\n";
        return 1;
    }

    std::vector<MeshData> meshes;
    meshes.reserve(scene->mNumMeshes);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        meshes.push_back(ProcessMesh(scene->mMeshes[i], scene));
    }

    if (!WriteBinary(outputPath, meshes))
    {
        return 1;
    }

    std::cout << "変換完了: " << inputPath.string() << " -> " << outputPath.string()
               << " (" << meshes.size() << " meshes)\n";

    return 0;
}
