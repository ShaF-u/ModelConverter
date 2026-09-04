# ModelConverter

FBX(Assimpが対応する各種3Dモデル形式)を、[GameEngine](../GameEngine)ランタイム用の独自バイナリ形式 `.mdl` に変換するオフラインCLIツールです。

## 背景

GameEngineは現在、実行時に直接AssimpでFBXをパースしてモデルを読み込んでいます(`Engine/Source/Graphics/Model/ModelLoader/ModelLoader.cpp`)。これを、

- ビルド時にこのツールで `.fbx` → `.mdl` に変換
- ランタイムはAssimpに依存しない軽量バイナリローダーで `.mdl` を読む

という構成に変えるための変換ツールです。

## これをやるメリット(DX12開発を進める上で)

1. **Releaseビルドの実行ファイルからAssimp依存を切り離せる**
   現状`Engine`はstatic libとしてAssimpをそのままRelease構成にもリンクしている。使わないフォーマット
   (OBJ/GLTF等)のパーサごと持ち込んでいる状態なので、変換をツール側に寄せれば実行ファイルが軽くなる。
2. **実行時ロードが速くなる**
   FBXはノード階層・マテリアル・アニメーション情報込みの汎用フォーマットで、Assimpのパースとシーング
   ラフ走査にコストがかかる。`ModelVertex`のレイアウトのままダンプしたバイナリなら、読んで頂点/イン
   デックスバッファに流し込むだけで済む。
3. **不正データ/エンコーディング問題を事前に潰せる**
   FBX内のテクスチャパスが非ASCIIでnarrow→wide変換が死ぬ、といった問題([GameEngine CLAUDE.md](../GameEngine/CLAUDE.md)に既知の落とし穴として記載)を変換時(ビルド時)に一度処理するだけで済み、プレイヤーの実行時に落ちるリスクが無くなる。
4. **拡張子を分けることで自動化しやすくなる**
   `.fbx`は変換対象、`.mdl`はランタイム同梱対象と拡張子だけで機械的に判別できるので、`.fbx`更新時に
   `.mdl`を再生成する等のビルドパイプライン自動化が組みやすい。

正直なところ、アセットが`Kipfel.fbx`1つだけの今の段階では1・2の恩恵はほぼ体感できない。今やる価値が
あるとすれば「Assimpをランタイムから外す」ことと、後々の自動化パイプライン整備の布石という意味合いが
大きい。詳しい設計背景は[docs/DESIGN.md](docs/DESIGN.md)、実装順は[docs/ROADMAP.md](docs/ROADMAP.md)を参照。

## 現在の配置について

このリポジトリ(ソース一式)は `GameEngine` と兄弟フォルダ(`Desktop/ModelConverter`)に独立して置いています。
GameEngine側には**ビルド済み成果物のみ**(`ModelConverter.exe` + `assimp-vc143-mt.dll`)を `GameEngine/Tools/ModelConverter/` に配置しており、
このリポジトリ自体をGameEngineへ移設したり、Git submoduleにする予定はありません(2026-09-04決定)。
ツールを更新した場合は、Releaseビルドし直して成果物を手動コピーしてください(手順は `GameEngine/Tools/ModelConverter/README.md` 参照)。

## ビルド

Visual Studio 2022 (v143 toolset)。

```powershell
& "F:\visualstudioIDE\MSBuild\Current\Bin\MSBuild.exe" ModelConverter.sln /p:Configuration=Release /p:Platform=x64
```

(VS2022のインストール先はマシンによって異なる。`vswhere -all -property installationPath`で確認できる。)

出力は `bin\<Configuration>\ModelConverter.exe`(Assimpの動的ライブラリはビルド後イベントで自動コピーされます)。

## 使い方

```powershell
ModelConverter.exe <input.fbx> [output.mdl]
```

`output.mdl` を省略した場合、入力ファイルと同じ場所・同じ名前で拡張子だけ `.mdl` になります。

## バイナリフォーマット

`docs/DESIGN.md` を参照してください。
