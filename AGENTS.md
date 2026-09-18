# AGENTS.md

このファイルは、このリポジトリでコーディングエージェント(Claude Code等)が作業する際のガイドです。

## このプロジェクトについて

`.fbx`をGameEngineランタイム用バイナリ`.mdl`に変換するスタンドアロンCLIツール。
詳しい背景・フォーマット仕様は [docs/DESIGN.md](docs/DESIGN.md)、使い方は [README.md](README.md) を参照。

`../GameEngine` と兄弟フォルダに置かれている前提。**このリポジトリ自体をGameEngineへ移設する予定は無い**
(2026-09-04決定) — GameEngine側には`Tools/`直下にビルド済み成果物(exe+dll)のみを配置する運用。
GameEngine本体のコーディング規約は `../GameEngine/CLAUDE.md` を参照。ただしこのプロジェクトの
ソースファイルはすべて **UTF-8**(GameEngine本体の一部ファイルとは異なり、Shift-JIS/CP932ではない)。

## ビルド

Visual Studio 2022 (v143 toolset)。

```powershell
& "F:\visualstudioIDE\MSBuild\Current\Bin\MSBuild.exe" ModelConverter.sln /p:Configuration=Debug /p:Platform=x64
```

(VS2022のインストール先はマシンによって異なる。`vswhere -all -property installationPath`で確認できる。)

出力: `bin\<Configuration>\ModelConverter.exe`。テストは無し(CIも無し)。

**前提**: `AssimpRoot`(`ModelConverter.vcxproj`)は`$(SolutionDir)..\GameEngine\...`、つまり`Desktop\GameEngine`を
兄弟フォルダとして参照する。GameEngineリポジトリ本体は2026-09-04に`source\repos\GameEngine`から`Desktop\GameEngine`へ
移動済みで、実ディレクトリとしてここに存在する(過去にジャンクションで代用していた期間があったが解消済み)。

## アーキテクチャ

- `Source/main.cpp` — Assimpで読み込み、`.mdl`(`RunModel`)または`.anm`(`RunAnimation`、`--anim`指定時)として書き出す。
  メッシュ/マテリアル処理はかつてGameEngine側にあったAssimp版`ModelLoader`(2026-09-05撤去)の
  `ProcessMesh`/`ProcessMaterial`を踏襲している。ロジックを変更したらGameEngine本体側の対応するコードとの
  整合性([docs/DESIGN.md](docs/DESIGN.md)参照)を意識すること。
- `Source/BinaryModelFormat.hpp` — `.mdl`フォーマットの定義(マジック、バージョン、`Vertex`、`BoneRecord`)。
- `Source/BinaryAnimationFormat.hpp` — `.anm`フォーマットの定義(マジック、バージョン、キーフレーム構造体)。
- 行列はすべてAssimp(列ベクトル)→DirectXMath(行ベクトル)に**転置して**書き出す(`CopyMatrixTransposed`)。
  クォータニオンは`(x,y,z,w)`順に並べ替える。時間は秒に正規化する。詳細は[docs/DESIGN.md](docs/DESIGN.md)。

## 変更時に気をつけること

- `Vertex`構造体を変更する場合、`../GameEngine/Engine/Source/Graphics/Model/ModelVertex/ModelVertex.hpp`の
  `ModelVertex`とバイト単位で一致させ、`ModelBinary::kVersion`を上げること。
- Assimpの参照パスは `ModelConverter.vcxproj` の `AssimpRoot` プロパティ(`$(SolutionDir)..\GameEngine\Engine\external\Assimp\`)
  でハードコードしている。このリポジトリ自体は移設しない方針のため、このパスは今後も変更不要。
- ツールを更新したら `GameEngine/Tools/` の `ModelConverter.exe`/`assimp-vc143-mt.dll` を
  Releaseビルドの成果物で上書きコピーすること(自動化なし、手動同期)。
- FBX内のテクスチャパス文字列は非ASCIIバイトを含みうるため、`std::filesystem::path`を経由させないこと
  (GameEngine本体の`CLAUDE.md`に記載の既知の落とし穴と同じ)。
