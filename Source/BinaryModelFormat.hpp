// FBX -> バイナリ変換フォーマット(.mdl)の定義。
// 頂点レイアウトは Engine::Graphics::ModelVertex (Engine/Source/Graphics/Model/ModelVertex/ModelVertex.hpp)
// と一致させること。変更した場合は kVersion を上げること。
#pragma once
#include <cstdint>

namespace ModelBinary
{
    inline constexpr char kMagic[4] = { 'E', 'M', 'D', 'L' };
    inline constexpr uint32_t kVersion = 2;

    // 1頂点が影響を受けるボーンの最大数(aiProcess_LimitBoneWeights のデフォルトと同じ)
    inline constexpr uint32_t kMaxBoneInfluences = 4;

    inline constexpr int32_t kNoParent = -1;

    // Engine::Graphics::ModelVertex と同じメモリレイアウト
    // (XMFLOAT3/XMFLOAT2/XMUINT4/XMFLOAT4 はそれぞれ float/uint32 配列と等価)
    struct Vertex
    {
        float    position[3];
        float    normal[3];
        float    uv[2];
        float    tangent[3];
        float    bitangent[3];
        uint32_t boneIndices[kMaxBoneInfluences]; // Bone 配列のindex。静的モデルは全て0
        float    boneWeights[kMaxBoneInfluences]; // 合計1.0。静的モデルは全て0
    };

    // スケルトンの1ノードのうち固定長部分(名前文字列の直後に置く)。
    // aiBone だけでなく、ボーンの祖先にあたる aiNode(Armature ルート等)も含める。
    // 配列は親が必ず子より前に来る順(深さ優先)で並ぶ。
    struct BoneRecord
    {
        int32_t parentIndex;             // kNoParent = ルート
        float   localBindTransform[16];  // aiNode::mTransformation
        float   offsetMatrix[16];        // aiBone::mOffsetMatrix。aiBoneでないノードは単位行列
    };

    // 行列はすべて DirectXMath の規約(行優先ストレージ・行ベクトル v' = v * M、平行移動は4行目)で書く。
    // Assimp の aiMatrix4x4 は列ベクトル規約(平行移動が4列目)なので、書き出し時に転置する。

    // ファイルレイアウト:
    //   char     magic[4]        "EMDL"
    //   uint32_t version         = 2
    //
    //   uint32_t boneCount       (0 = 静的モデル)
    //   [boneCount 回繰り返し]
    //     uint32_t   nameLength
    //     char       name[nameLength]   (UTF-8)
    //     BoneRecord record
    //
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
