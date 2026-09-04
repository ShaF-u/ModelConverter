// FBX -> バイナリ変換フォーマットの定義。
// 頂点レイアウトは Engine::Graphics::ModelVertex (Engine/Source/Graphics/Model/ModelVertex/ModelVertex.hpp)
// と一致させること。変更した場合は kVersion を上げること。
#pragma once
#include <cstdint>

namespace ModelBinary
{
    inline constexpr char kMagic[4] = { 'E', 'M', 'D', 'L' };
    inline constexpr uint32_t kVersion = 1;

    // Engine::Graphics::ModelVertex と同じメモリレイアウト（XMFLOAT3/XMFLOAT2はfloat配列と等価）
    struct Vertex
    {
        float position[3];
        float normal[3];
        float uv[2];
        float tangent[3];
        float bitangent[3];
    };

    // ファイルレイアウト:
    //   char     magic[4]        "EMDL"
    //   uint32_t version
    //   uint32_t meshCount
    //   [meshCount 回繰り返し]
    //     uint32_t vertexCount
    //     uint32_t indexCount
    //     Vertex   vertices[vertexCount]
    //     uint32_t indices[indexCount]
    //     uint32_t materialNameLength
    //     char     materialName[materialNameLength]
    //     float    color[4]
    //     uint32_t textureNameLength   (0 = テクスチャなし)
    //     char     textureName[textureNameLength]
} // namespace ModelBinary
