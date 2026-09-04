# 設計メモ

## 目的とスコープ

`GameEngine`(`../GameEngine`)は現在、実行時に`Assimp`で`.fbx`を直接パースしてモデルを読み込んでいる(`Engine/Source/Graphics/Model/ModelLoader/ModelLoader.cpp`)。
このツールはその変換処理をオフラインに切り出し、GameEngineのランタイムがAssimpに依存しなくて済むようにする。

スコープ外(v1では対象外):
- スケルタルアニメーション / ボーン情報
- 複数UVチャンネル、頂点カラー
- ノード階層(現状のModelLoaderもフラットな`Mesh`の配列にしているため踏襲)

これらが必要になったら`kVersion`を上げてフォーマットを拡張する。

## なぜ拡張子を分けるか

ランタイム側の`Engine::System::Assets::AssetManager`は拡張子ごとにローダーを登録する仕組みを既に持っている(`Framework::RegisterAssetLoaders()`)。
`.fbx`と区別できる独自拡張子(`.mdl` = Engine MoDeL)にしておくことで、

- AssetManagerに新しいローダーを登録するだけで済み、既存の`.fbx`ローダーと共存できる
- ビルドスクリプトや将来のアセットパイプラインが「`.fbx`は変換対象」「`.mdl`はランタイム同梱対象」と拡張子だけで機械的に判別できる
- `.fbx`が更新されたら`.mdl`を再生成する、といった自動化(watchスクリプト、pre-buildステップ、CI)が組みやすい

拡張子は独自形式で問題ない(OS/ツールが予約しているものではなく、実体は完全に自前定義のバイナリ)。

## バイナリフォーマット (`.mdl`, version 1)

`Source/BinaryModelFormat.hpp` が正。エンディアンはリトルエンディアン固定(x64のみ対象のため考慮不要)。

```
char     magic[4]        "EMDL"
uint32_t version          = 1
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
バイト単位で一致させている。`XMFLOAT3`/`XMFLOAT2`はfloat配列と等価なので、Engine側は
DirectXMathの型にそのままreinterpret/コピーして読み込める。

| フィールド  | 型        | GameEngine側対応       |
|-------------|-----------|-------------------------|
| position    | float[3]  | `ModelVertex::position`  |
| normal      | float[3]  | `ModelVertex::normal`    |
| uv          | float[2]  | `ModelVertex::uv`        |
| tangent     | float[3]  | `ModelVertex::tangent`   |
| bitangent   | float[3]  | `ModelVertex::bitangent` |

**この構造体を変更したら`ModelVertex.hpp`と`kVersion`の両方を必ず更新すること。**

### マテリアルの持ち方

`Engine::Graphics::Material`(`Material.hpp`)に合わせて最小限にしている。

- `materialName` → `Material::name`
- `color`        → `Material::color`
- `textureName`  → ランタイム側の`Material::diffuseTexture`(`TextureID`)

`TextureID`はツール側では確定させない。理由は現行の`ModelLoader::ProcessMaterial`と同じで、
`AssetManager::GetTextureID()`がロード順(テクスチャ→モデルの順)やファイル名フォールバック
(同じディレクトリ内の唯一のテクスチャを使う等)といった実行時の解決ロジックを持っているため。
ツールはテクスチャの**ファイル名のみ**(パスの最後の要素、非ASCIIを含みうる生バイト列)を保存する。

`normalMap`/`roughnessMap`等は`Material.hpp`側にまだフィールドが無い(コメントアウトされている)ため、
このフォーマットにも含めていない。追加されたタイミングで両方を拡張する。

### 未対応・注意点

- FBX内のテクスチャパス文字列は元マシンの非ASCIIバイトを含みうるため、`std::filesystem::path`を
  経由せず手動でファイル名を切り出している(`ExtractFileName`)。GameEngine本体の
  `ModelLoader::ProcessMaterial`と同じ理由(`CLAUDE.md`にも記載されている既知の落とし穴)。
- マテリアルインデックスが不正な場合(`mMaterialIndex >= mNumMaterials`)はデフォルトマテリアル
  (`name="Default"`, 白色, テクスチャなし)を書き出す。

## ランタイム側で必要になる変更(このツールのスコープ外)

1. `AssetManager`に`.mdl`拡張子のローダーを登録
2. Assimpを使わずバイナリを読んで`Model`/`Mesh`/`Material`を構築する軽量ローダーを追加
   (既存の`ModelLoader`とは別クラスにする想定。例: `BinaryModelLoader`)
3. `.fbx`を直接読むパスを残すか完全に置き換えるかは別途判断
   (少なくとも移行期間は両方サポートしておくと安全)

上記はGameEngine本体側の作業であり、このリポジトリでは扱わない。
