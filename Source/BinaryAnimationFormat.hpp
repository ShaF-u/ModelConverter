// FBX -> バイナリ変換フォーマット(.anm)の定義。
// 1ファイル = 1クリップ。スケルトンは持たず、チャンネルはボーン名で識別する。
// エンジン側でモデル(.mdl)のスケルトンに紐付ける際に名前 -> index を解決する想定。
#pragma once
#include <cstdint>

namespace AnimationBinary
{
    inline constexpr char kMagic[4] = { 'E', 'A', 'N', 'M' };
    inline constexpr uint32_t kVersion = 1;

    struct VectorKey
    {
        float time;     // 秒
        float value[3];
    };

    struct QuaternionKey
    {
        float time;     // 秒
        float value[4]; // (x, y, z, w) — DirectXMath の XMFLOAT4 と同じ並び。Assimp の aiQuaternion は (w,x,y,z) なので並べ替える
    };

    // ファイルレイアウト:
    //   char     magic[4]        "EANM"
    //   uint32_t version         = 1
    //   uint32_t nameLength
    //   char     name[nameLength]           (クリップ名。出力ファイル名の stem)
    //   float    duration                   (秒)
    //   uint32_t channelCount
    //   [channelCount 回繰り返し]
    //     uint32_t      boneNameLength
    //     char          boneName[boneNameLength]
    //     uint32_t      positionKeyCount
    //     VectorKey     positionKeys[positionKeyCount]
    //     uint32_t      rotationKeyCount
    //     QuaternionKey rotationKeys[rotationKeyCount]
    //     uint32_t      scaleKeyCount
    //     VectorKey     scaleKeys[scaleKeyCount]
    //
    // 時間はすべて aiAnimation::mTicksPerSecond で割って秒に正規化済み(0 の場合は 25 として扱う)。
} // namespace AnimationBinary
