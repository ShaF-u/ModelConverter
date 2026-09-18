// FBX(その他Assimp対応形式)をエンジン用バイナリ(.mdl / .anm)に変換するオフラインツール。
// メッシュ/マテリアル処理は、かつて GameEngine 側にあった Assimp 版 ModelLoader(2026-09-05 撤去)の
// ProcessMesh / ProcessMaterial を踏襲している。
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "BinaryAnimationFormat.hpp"
#include "BinaryModelFormat.hpp"

namespace fs = std::filesystem;

namespace
{
    // ---------------------------------------------------------------- 共通

    struct BoneData
    {
        std::string name;
        ModelBinary::BoneRecord record{};
    };

    struct SkeletonData
    {
        std::vector<BoneData> bones;
        std::unordered_map<std::string, uint32_t> nameToIndex;
    };

    struct MeshData
    {
        std::vector<ModelBinary::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string materialName = "Default";
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        std::string textureName; // 空文字ならテクスチャなし
    };

    struct ChannelData
    {
        std::string boneName;
        std::vector<AnimationBinary::VectorKey>     positionKeys;
        std::vector<AnimationBinary::QuaternionKey> rotationKeys;
        std::vector<AnimationBinary::VectorKey>     scaleKeys;
    };

    struct ClipData
    {
        std::string name;
        float duration = 0.0f;
        std::vector<ChannelData> channels;
    };

    std::string ExtractFileName(const std::string& rawPath)
    {
        auto slashPos = rawPath.find_last_of("/\\");
        return (slashPos != std::string::npos) ? rawPath.substr(slashPos + 1) : rawPath;
    }

    // Assimp(列ベクトル規約)の行列を DirectXMath(行ベクトル規約)向けに転置してコピーする。
    void CopyMatrixTransposed(const aiMatrix4x4& m, float out[16])
    {
        out[0]  = m.a1; out[1]  = m.b1; out[2]  = m.c1; out[3]  = m.d1;
        out[4]  = m.a2; out[5]  = m.b2; out[6]  = m.c2; out[7]  = m.d2;
        out[8]  = m.a3; out[9]  = m.b3; out[10] = m.c3; out[11] = m.d3;
        out[12] = m.a4; out[13] = m.b4; out[14] = m.c4; out[15] = m.d4;
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

    template <typename T>
    void WriteRaw(std::ofstream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    void WriteArray(std::ofstream& out, const std::vector<T>& values)
    {
        uint32_t count = static_cast<uint32_t>(values.size());
        WriteRaw(out, count);
        if (count > 0)
        {
            out.write(reinterpret_cast<const char*>(values.data()),
                static_cast<std::streamsize>(count * sizeof(T)));
        }
    }

    // ---------------------------------------------------------------- スケルトン

    void CollectNodes(const aiNode* node, std::unordered_map<std::string, const aiNode*>& out)
    {
        out[node->mName.C_Str()] = node;
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            CollectNodes(node->mChildren[i], out);
        }
    }

    void AppendNeededNodes(const aiNode* node, int32_t parentIndex,
        const std::unordered_set<const aiNode*>& needed,
        const std::unordered_map<std::string, const aiBone*>& boneByName,
        SkeletonData& skeleton)
    {
        if (needed.count(node) == 0)
        {
            return;
        }

        BoneData bone;
        bone.name = node->mName.C_Str();
        bone.record.parentIndex = parentIndex;
        CopyMatrixTransposed(node->mTransformation, bone.record.localBindTransform);

        auto boneIt = boneByName.find(bone.name);
        if (boneIt != boneByName.end())
        {
            CopyMatrixTransposed(boneIt->second->mOffsetMatrix, bone.record.offsetMatrix);
        }
        else
        {
            CopyMatrixTransposed(aiMatrix4x4(), bone.record.offsetMatrix);
        }

        int32_t index = static_cast<int32_t>(skeleton.bones.size());
        skeleton.nameToIndex[bone.name] = static_cast<uint32_t>(index);
        skeleton.bones.push_back(std::move(bone));

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            AppendNeededNodes(node->mChildren[i], index, needed, boneByName, skeleton);
        }
    }

    // 全メッシュの aiBone と、その祖先ノードを深さ優先順で並べたスケルトンを作る。
    SkeletonData BuildSkeleton(const aiScene* scene)
    {
        SkeletonData skeleton;

        std::unordered_map<std::string, const aiBone*> boneByName;
        for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (unsigned int b = 0; b < mesh->mNumBones; ++b)
            {
                boneByName.emplace(mesh->mBones[b]->mName.C_Str(), mesh->mBones[b]);
            }
        }
        if (boneByName.empty())
        {
            return skeleton;
        }

        std::unordered_map<std::string, const aiNode*> nodeByName;
        CollectNodes(scene->mRootNode, nodeByName);

        std::unordered_set<const aiNode*> needed;
        for (const auto& [name, bone] : boneByName)
        {
            auto it = nodeByName.find(name);
            if (it == nodeByName.end())
            {
                std::cerr << "警告: ボーン '" << name << "' に対応するノードが見つかりません\n";
                continue;
            }
            for (const aiNode* n = it->second; n != nullptr; n = n->mParent)
            {
                needed.insert(n);
            }
        }

        AppendNeededNodes(scene->mRootNode, ModelBinary::kNoParent, needed, boneByName, skeleton);
        return skeleton;
    }

    // ---------------------------------------------------------------- メッシュ

    void ApplyBoneWeights(const aiMesh* aimesh, const SkeletonData& skeleton, MeshData& mesh)
    {
        std::vector<uint8_t> influenceCount(mesh.vertices.size(), 0);

        for (unsigned int b = 0; b < aimesh->mNumBones; ++b)
        {
            const aiBone* bone = aimesh->mBones[b];
            auto it = skeleton.nameToIndex.find(bone->mName.C_Str());
            if (it == skeleton.nameToIndex.end())
            {
                continue;
            }
            uint32_t boneIndex = it->second;

            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
                const aiVertexWeight& weight = bone->mWeights[w];
                if (weight.mVertexId >= mesh.vertices.size())
                {
                    continue;
                }
                uint8_t& count = influenceCount[weight.mVertexId];
                if (count >= ModelBinary::kMaxBoneInfluences)
                {
                    std::cerr << "警告: 頂点 " << weight.mVertexId << " の影響ボーンが "
                              << ModelBinary::kMaxBoneInfluences << " を超えています(切り捨て)\n";
                    continue;
                }
                ModelBinary::Vertex& v = mesh.vertices[weight.mVertexId];
                v.boneIndices[count] = boneIndex;
                v.boneWeights[count] = weight.mWeight;
                ++count;
            }
        }

        // 元データのウェイト合計は1になっていないことがある(Kipfel.fbxで0.42〜1.09を確認)ので正規化する
        for (auto& v : mesh.vertices)
        {
            float sum = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
            if (sum <= 0.0f)
            {
                continue;
            }
            for (float& w : v.boneWeights)
            {
                w /= sum;
            }
        }
    }

    MeshData ProcessMesh(const aiMesh* aimesh, const aiScene* scene, const SkeletonData& skeleton)
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

        ApplyBoneWeights(aimesh, skeleton, mesh);

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

    bool WriteModel(const fs::path& outPath, const SkeletonData& skeleton, const std::vector<MeshData>& meshes)
    {
        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "出力ファイルを開けません: " << outPath.string() << "\n";
            return false;
        }

        out.write(ModelBinary::kMagic, sizeof(ModelBinary::kMagic));
        WriteRaw(out, ModelBinary::kVersion);

        WriteRaw(out, static_cast<uint32_t>(skeleton.bones.size()));
        for (const auto& bone : skeleton.bones)
        {
            WriteString(out, bone.name);
            WriteRaw(out, bone.record);
        }

        WriteRaw(out, static_cast<uint32_t>(meshes.size()));
        for (const auto& mesh : meshes)
        {
            uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
            WriteRaw(out, vertexCount);
            WriteRaw(out, indexCount);

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

    // ---------------------------------------------------------------- アニメーション

    ClipData ProcessAnimation(const aiAnimation* anim, const std::string& clipName)
    {
        ClipData clip;
        clip.name = clipName;

        const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
        clip.duration = static_cast<float>(anim->mDuration / tps);

        clip.channels.reserve(anim->mNumChannels);
        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* nodeAnim = anim->mChannels[c];
            ChannelData channel;
            channel.boneName = nodeAnim->mNodeName.C_Str();

            channel.positionKeys.reserve(nodeAnim->mNumPositionKeys);
            for (unsigned int k = 0; k < nodeAnim->mNumPositionKeys; ++k)
            {
                const aiVectorKey& key = nodeAnim->mPositionKeys[k];
                channel.positionKeys.push_back({
                    static_cast<float>(key.mTime / tps),
                    { key.mValue.x, key.mValue.y, key.mValue.z } });
            }

            channel.rotationKeys.reserve(nodeAnim->mNumRotationKeys);
            for (unsigned int k = 0; k < nodeAnim->mNumRotationKeys; ++k)
            {
                const aiQuatKey& key = nodeAnim->mRotationKeys[k];
                channel.rotationKeys.push_back({
                    static_cast<float>(key.mTime / tps),
                    { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w } });
            }

            channel.scaleKeys.reserve(nodeAnim->mNumScalingKeys);
            for (unsigned int k = 0; k < nodeAnim->mNumScalingKeys; ++k)
            {
                const aiVectorKey& key = nodeAnim->mScalingKeys[k];
                channel.scaleKeys.push_back({
                    static_cast<float>(key.mTime / tps),
                    { key.mValue.x, key.mValue.y, key.mValue.z } });
            }

            clip.channels.push_back(std::move(channel));
        }

        return clip;
    }

    bool WriteAnimation(const fs::path& outPath, const ClipData& clip)
    {
        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "出力ファイルを開けません: " << outPath.string() << "\n";
            return false;
        }

        out.write(AnimationBinary::kMagic, sizeof(AnimationBinary::kMagic));
        WriteRaw(out, AnimationBinary::kVersion);

        WriteString(out, clip.name);
        WriteRaw(out, clip.duration);

        WriteRaw(out, static_cast<uint32_t>(clip.channels.size()));
        for (const auto& channel : clip.channels)
        {
            WriteString(out, channel.boneName);
            WriteArray(out, channel.positionKeys);
            WriteArray(out, channel.rotationKeys);
            WriteArray(out, channel.scaleKeys);
        }

        return true;
    }

    // ---------------------------------------------------------------- エントリ

    struct Options
    {
        bool animMode = false;
        bool dump = false;
        fs::path inputPath;
        fs::path outputPath;
    };

    void PrintUsage()
    {
        std::cerr
            << "使い方:\n"
            << "  ModelConverter.exe <input.fbx> [output.mdl] [--dump]\n"
            << "  ModelConverter.exe --anim <input.fbx> [output.anm] [--dump]\n"
            << "\n"
            << "  --anim  入力FBXのアニメーションを .anm として書き出す(メッシュは無視)\n"
            << "  --dump  ボーン一覧 / チャンネル一覧を標準出力に表示する\n";
    }

    bool ParseArgs(int argc, char** argv, Options& opt)
    {
        std::vector<std::string> positional;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--anim")
            {
                opt.animMode = true;
            }
            else if (arg == "--dump")
            {
                opt.dump = true;
            }
            else if (arg.rfind("--", 0) == 0)
            {
                std::cerr << "不明なオプション: " << arg << "\n";
                return false;
            }
            else
            {
                positional.push_back(arg);
            }
        }

        if (positional.empty() || positional.size() > 2)
        {
            return false;
        }

        opt.inputPath = positional[0];
        if (positional.size() == 2)
        {
            opt.outputPath = positional[1];
        }
        else
        {
            opt.outputPath = fs::path(opt.inputPath).replace_extension(opt.animMode ? ".anm" : ".mdl");
        }
        return true;
    }

    int RunModel(const Options& opt)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            opt.inputPath.string(),
            aiProcess_Triangulate |
            aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals |
            aiProcess_LimitBoneWeights
        );

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        {
            std::cerr << "モデルの読み込みに失敗: " << opt.inputPath.string()
                      << " - " << importer.GetErrorString() << "\n";
            return 1;
        }

        SkeletonData skeleton = BuildSkeleton(scene);

        std::vector<MeshData> meshes;
        meshes.reserve(scene->mNumMeshes);
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            meshes.push_back(ProcessMesh(scene->mMeshes[i], scene, skeleton));
        }

        if (opt.dump)
        {
            std::cout << "ボーン (" << skeleton.bones.size() << ")\n";
            for (size_t i = 0; i < skeleton.bones.size(); ++i)
            {
                const auto& b = skeleton.bones[i];
                std::cout << "  [" << i << "] " << b.name << "  parent=" << b.record.parentIndex << "\n";
            }

            for (size_t m = 0; m < meshes.size(); ++m)
            {
                size_t unweighted = 0;
                float minSum = 1e9f, maxSum = -1e9f;
                for (const auto& v : meshes[m].vertices)
                {
                    float sum = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
                    if (sum == 0.0f) { ++unweighted; continue; }
                    minSum = std::min(minSum, sum);
                    maxSum = std::max(maxSum, sum);
                }
                std::cout << "メッシュ [" << m << "] '" << meshes[m].materialName << "' vertices="
                          << meshes[m].vertices.size() << " unweighted=" << unweighted;
                if (unweighted < meshes[m].vertices.size())
                {
                    std::cout << " weightSum=[" << minSum << ", " << maxSum << "]";
                }
                std::cout << "\n";
            }
        }

        if (!WriteModel(opt.outputPath, skeleton, meshes))
        {
            return 1;
        }

        std::cout << "変換完了: " << opt.inputPath.string() << " -> " << opt.outputPath.string()
                  << " (" << meshes.size() << " meshes, " << skeleton.bones.size() << " bones)\n";
        return 0;
    }

    int RunAnimation(const Options& opt)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(opt.inputPath.string(), 0);

        if (!scene || !scene->mRootNode)
        {
            std::cerr << "アニメーションの読み込みに失敗: " << opt.inputPath.string()
                      << " - " << importer.GetErrorString() << "\n";
            return 1;
        }
        if (scene->mNumAnimations == 0)
        {
            std::cerr << "アニメーションが含まれていません: " << opt.inputPath.string() << "\n";
            return 1;
        }

        for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
        {
            // 複数クリップ入りのFBXは <stem>_<index>.anm に分ける
            fs::path outPath = opt.outputPath;
            if (scene->mNumAnimations > 1)
            {
                outPath.replace_filename(
                    opt.outputPath.stem().string() + "_" + std::to_string(a) + opt.outputPath.extension().string());
            }

            ClipData clip = ProcessAnimation(scene->mAnimations[a], outPath.stem().string());

            if (opt.dump)
            {
                std::cout << "クリップ '" << clip.name << "' duration=" << clip.duration
                          << "s channels=" << clip.channels.size() << "\n";
                for (const auto& ch : clip.channels)
                {
                    std::cout << "  " << ch.boneName
                              << "  pos=" << ch.positionKeys.size()
                              << " rot=" << ch.rotationKeys.size()
                              << " scale=" << ch.scaleKeys.size() << "\n";
                }
            }

            if (!WriteAnimation(outPath, clip))
            {
                return 1;
            }

            std::cout << "変換完了: " << opt.inputPath.string() << " -> " << outPath.string()
                      << " (" << clip.channels.size() << " channels, " << clip.duration << "s)\n";
        }

        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (!ParseArgs(argc, argv, opt))
    {
        PrintUsage();
        return 1;
    }

    return opt.animMode ? RunAnimation(opt) : RunModel(opt);
}
