# 設計メモ

## 目的とスコープ

`GameEngine`(`../GameEngine`)のランタイムはAssimpに依存せず、このツールが事前変換した独自バイナリだけを読む
(2026-09-05にAssimp版`ModelLoader`は撤去済み)。このツールは以下の2種類のファイルを出力する。

| 拡張子 | 内容 | 変換元 |
|--------|------|--------|
| `.mdl` | スケルトン + メッシュ + マテリアル | モデルFBX(メッシュ入り) |
| `.anm` | アニメーションクリップ1本 | アニメーションFBX(メッシュ無しでよい) |

スコープ外:
- 複数UVチャンネル、頂点カラー
- スキン無しメッシュのノード階層(v1から引き続きフラットな`Mesh`配列。スキン付きメッシュはボーンで動くので不要)
- モーフターゲット / ブレンドシェイプ

必要になったら`kVersion`を上げてフォーマットを拡張する。

## なぜ `.mdl` と `.anm` を分けるか

- ランタイム側の`AssetManager`は拡張子ごとにローダーを登録する仕組みなので、`.anm`ローダーを1つ足すだけで済む
- Mixamo等のワークフローは「1スケルトンに対してクリップFBXが何十本」になる。クリップを`.mdl`に埋め込むと、
  モデルを再変換するたびに全クリップを指定し直す必要があり、自動化と相性が悪い
- `anim.fbx` → `anim.anm` の1:1変換にしておけば、「更新されたFBXだけ再変換する」が拡張子とタイムスタンプだけで組める

代償として、`.anm`単体ではスケルトンを知らないため、チャンネルの識別は**ボーン名**になる。
ランタイムはクリップをモデルに紐付ける時(ロード時に1回)に名前→indexを解決する想定で、
毎フレームの文字列比較は発生しない。

## バイナリフォーマット `.mdl` (version 2)

`Source/BinaryModelFormat.hpp` が正。エンディアンはリトルエンディアン固定(x64のみ対象のため考慮不要)。

```
char     magic[4]        "EMDL"
uint32_t version          = 2

uint32_t boneCount        (0 = 静的モデル)
[boneCount 回繰り返し]
  uint32_t   nameLength
  char       name[nameLength]         (UTF-8)
  BoneRecord record                   (下記)

uint32_t meshCount
[meshCount 回繰り返し]
  uint32_t vertexCount
  uint32_t indexCount
  Vertex   vertices[vertexCount]
  uint32_t indices[indexCount]
  uint32_t materialNameLength
  char     materialName[materialNameLength]   (UTF-8、長さプレフィックス方式)
  float    color[4]                            (RGBA)
  uint32_t textureNameLength
  char     textureName[textureNameLength]      (0なら「テクスチャなし」)
```

### `Vertex` のレイアウト

`Engine::Graphics::ModelVertex`(`Engine/Source/Graphics/Model/ModelVertex/ModelVertex.hpp`)と
バイト単位で一致させる。

| フィールド   | 型           | GameEngine側対応              |
|--------------|--------------|-------------------------------|
| position     | float[3]     | `ModelVertex::position`       |
| normal       | float[3]     | `ModelVertex::normal`         |
| uv           | float[2]     | `ModelVertex::uv`             |
| tangent      | float[3]     | `ModelVertex::tangent`        |
| bitangent    | float[3]     | `ModelVertex::bitangent`      |
| boneIndices  | uint32_t[4]  | `ModelVertex::boneIndices` (`XMUINT4`)  |
| boneWeights  | float[4]     | `ModelVertex::boneWeights` (`XMFLOAT4`) |

- ボーン情報は**静的モデルでも常に持つ**(全て0)。頂点フォーマットを1種類にして、エンジン側のPSO/入力レイアウトを
  1つで済ませるため。静的メッシュが32B/頂点太るが許容する
- ウェイトは変換時に**合計1.0に正規化済み**(元FBXでは0.42〜1.09のようにバラつくことがある)。
  静的頂点は合計0なので、エンジン側は「合計0ならスキニングしない」で判別できる
- 1頂点の影響ボーンは最大4(`aiProcess_LimitBoneWeights`)

**この構造体を変更したら`ModelVertex.hpp`と`kVersion`の両方を必ず更新すること。**

### スケルトン(`BoneRecord`)

```
int32_t parentIndex             (-1 = ルート)
float   localBindTransform[16]  (aiNode::mTransformation)
float   offsetMatrix[16]        (aiBone::mOffsetMatrix。aiBoneでないノードは単位行列)
```

- 配列に入るのは「いずれかのメッシュの`aiBone`」と「その祖先ノード全部」(Armatureルート、シーンルート含む)。
  中間ノードを飛ばすと階層変換が壊れるため
- 並びは深さ優先で、**親は必ず子より前**に来る。ランタイムは配列を先頭から舐めるだけで
  `global[i] = local[i] * global[parent[i]]` が計算できる
- 頂点の`boneIndices`はこの配列のindexを指す(aiBoneでないノードが指されることは無い)
- 最終的なスキニング行列は `offsetMatrix[i] * global[i]`(行ベクトル規約)

### 行列の規約

**すべての行列はDirectXMath規約(行優先ストレージ・行ベクトル `v' = v * M`・平行移動は4行目)で書く。**
Assimpの`aiMatrix4x4`は列ベクトル規約(平行移動が4列目)なので、書き出し時に転置している(`CopyMatrixTransposed`)。
ランタイムは`XMFLOAT4X4`にそのまま`memcpy`してよい。ここを揃えないと`Model.hlsl`の`pack_matrix(row_major)`事故と
同種のバグになる。

### マテリアルの持ち方

`Engine::Graphics::Material`(`Material.hpp`)に合わせて最小限にしている。

- `materialName` → `Material::name`
- `color`        → `Material::color`
- `textureName`  → ランタイム側の`Material::diffuseTexture`(`TextureID`)

`TextureID`はツール側では確定させない。`AssetManager::GetTextureID()`がロード順やファイル名フォールバック
(同じディレクトリ内の唯一のテクスチャを使う等)といった実行時の解決ロジックを持っているため。
ツールはテクスチャの**ファイル名のみ**(パスの最後の要素、非ASCIIを含みうる生バイト列)を保存する。

## バイナリフォーマット `.anm` (version 1)

`Source/BinaryAnimationFormat.hpp` が正。1ファイル = 1クリップ。

```
char     magic[4]        "EANM"
uint32_t version          = 1
uint32_t nameLength
char     name[nameLength]            (クリップ名 = 出力ファイル名のstem)
float    duration                    (秒)
uint32_t channelCount
[channelCount 回繰り返し]
  uint32_t      boneNameLength
  char          boneName[boneNameLength]
  uint32_t      positionKeyCount
  VectorKey     positionKeys[positionKeyCount]     { float time; float value[3]; }
  uint32_t      rotationKeyCount
  QuaternionKey rotationKeys[rotationKeyCount]     { float time; float value[4]; }
  uint32_t      scaleKeyCount
  VectorKey     scaleKeys[scaleKeyCount]
```

- 時間は`aiAnimation::mTicksPerSecond`で割って**秒に正規化済み**(0の場合は25として扱う)。
  ランタイムはtpsを気にしなくてよい
- クォータニオンは`(x, y, z, w)`順。`XMFLOAT4`と同じ。Assimpの`aiQuaternion`は`(w,x,y,z)`なので並べ替えている
- チャンネルに含まれないボーンは`localBindTransform`のまま動かさない(ランタイム側の規約)
- 1つのFBXに複数の`aiAnimation`が入っている場合は`<stem>_<index>.anm`に分割する
- FBXのクリップ名(`aiAnimation::mName`)は使わない。Mixamoは全部 `"mixamo.com"` になっていて役に立たないため

## 未対応・注意点

- FBX内のテクスチャパス文字列は元マシンの非ASCIIバイトを含みうるため、`std::filesystem::path`を
  経由せず手動でファイル名を切り出している(`ExtractFileName`)。GameEngine本体の`CLAUDE.md`にも記載の既知の落とし穴
- マテリアルインデックスが不正な場合(`mMaterialIndex >= mNumMaterials`)はデフォルトマテリアル
  (`name="Default"`, 白色, テクスチャなし)を書き出す
- `.anm`のボーン名が`.mdl`のスケルトンと一致するかは変換時には検証しない(別ファイルなので知りようがない)。
  不一致はランタイムで紐付ける時にしか分からない。`--dump`でチャンネル名一覧を出せるので目視確認に使う
- `.anm`の書き出しは、動作確認済みのアニメーション付きFBXがまだ手元に無いため、実データでの検証が未了(2026-09-18時点)

## ランタイム側で必要になる変更(このツールのスコープ外)

1. `ModelVertex`に`boneIndices`/`boneWeights`を追加し、`ModelRenderer`の入力レイアウトと`Model.hlsl`のVS入力を更新
2. `Model`に`Bone`配列を追加し、`BinaryModelLoader`をv2対応にする(v1は拒否してよい。`.mdl`は再生成する)
3. `.anm`ローダーと`AnimationClip`データ構造を追加、`AssetManager`に登録(ロード順: テクスチャ → モデル → アニメ)
4. ボーン行列パレット(Entity単位の定数バッファ)と、`Model.hlsl`でのスキニング
5. `AnimationSystem`: 時間を進めてキーフレーム補間 → 階層を掛けてパレットに書き込む

上記はGameEngine本体側の作業であり、このリポジトリでは扱わない。
