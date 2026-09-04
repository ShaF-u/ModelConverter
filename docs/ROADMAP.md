# ロードマップ

現時点で決まっている設計方針([docs/DESIGN.md](DESIGN.md))を、実際に着手する順番に並べたもの。
各フェーズは「動くところまで作って確認してから次に進む」を前提とする。フェーズ番号は優先順位であって、
まとめて一気に実装する計画ではない。

## フェーズ1: 変換ツール本体(このリポジトリ) — 完了

- [x] `ModelConverter`プロジェクトの雛形(vcxproj/sln、Assimp参照)
- [x] `.fbx` → `.mdl` 変換ロジック(頂点/インデックス/マテリアル情報の書き出し)
- [x] フォーマット仕様のドキュメント化(`DESIGN.md`)
- [x] ビルド確認(2026-09-04、VS2022 17.14 / v143、Debug|x64)
- [x] `App/Assets/Models/Kipfel.fbx` を実際に変換して`.mdl`が生成できることを確認(2026-09-04、2 meshes、1,165,000 bytes)
- [x] 出力バイナリの中身をダンプ/検証するスクリプトで、値が壊れていないか確認(2026-09-04)
      — magic/version/meshCount一致、両メッシュとも不正インデックス0件、マテリアル名・テクスチャ名が正しくデコードでき、
      パース済みバイト数とファイルサイズが完全一致することを確認

このフェーズが終わるまでランタイム側(GameEngine本体)には一切手を入れない。

## フェーズ2: ランタイムに`.mdl`ローダーを追加(GameEngine本体側) — 完了

`.fbx`ローダーは残したまま、`.mdl`を読める経路を追加する。既存の`.fbx`直読みを壊さないことが条件。

- [x] `BinaryModelLoader`を`Engine/Source/Graphics/Model/BinaryModelLoader/`に追加(コミット`b4bfb34`)。
      Assimpを一切使わず、`.mdl`をパースして`Model`/`Mesh`/`Material`を構築する。
      `ModelManager::Load()`が拡張子(`.mdl`か否か)で`BinaryModelLoader`/`ModelLoader`を振り分け
- [x] `Framework::RegisterAssetLoaders()`(`AssetManager::RegisterLoader`)に`.mdl`拡張子のローダー登録を追加
      (`.fbx`/`.obj`ローダーとは共存、どちらもロードできる状態)
- [x] `Kipfel.mdl`を`App/Assets/Models/`に置いて実機確認(2026-09-04)。ログ(`Debugger/Logs/latest.log`)で、
      `.fbx`版・`.mdl`版とも頂点数/インデックス数が完全一致(mesh0: 11401/42672、mesh1: 5120/17250)、
      テクスチャ解決(`kipfel_mobile.png`のディレクトリフォールバック)も同一挙動になることを確認。
      `AssetManager`の名前解決(`GetModelID("Kipfel")`)は`.fbx`→`.mdl`の順でロードされるため`.mdl`側で
      上書きされ、シーンの`Kipfel`エンティティは実質的に`.mdl`経由でレンダリングされる状態になっていた。
      クラッシュ無くカメラ検出まで正常続行を確認。`Kipfel.mdl`はこの時点で既にリポジトリへコミット済みだった
      (`ModelConverter`側で独立に再変換した出力とバイト単位で一致することも確認)

## フェーズ3: 変換ツールをGameEngineリポジトリへ配置 — 完了(方針変更: ソース移設ではなくビルド成果物配置)

ユーザーとの確認の結果、当初案(ソース一式`GameEngine/Tools/ModelConverter`へ移動)ではなく、
**ビルド済み成果物(`ModelConverter.exe`+`assimp-vc143-mt.dll`)のみをGameEngine側に配置する**方式に決定(2026-09-04)。
ソースリポジトリ(このリポジトリ、`ShaF-u/ModelConverter`)は`Desktop/ModelConverter`に独立したまま。
Git submoduleにもしない(ユーザー確認済み)。

- [x] `GameEngine/Tools/ModelConverter/`に`ModelConverter.exe`(Release|x64)+`assimp-vc143-mt.dll`を配置(2026-09-04)
- [x] 配置後の`.exe`を実際に実行し、`Kipfel.fbx`→`.mdl`変換が動くこと・出力が既存の`Kipfel.mdl`とバイト単位で
      一致することを確認
- [x] `GameEngine/Tools/ModelConverter/README.md`(使い方・更新方法・このリポジトリへの導線)を新規作成
- [x] `GameEngine/CLAUDE.md`に`Tools/ModelConverter`のセクションを追加(ソースの所在・ランタイム側との対応関係を記載)
- [-] `ModelConverter.vcxproj`の`AssimpRoot`パス短縮 — ソースを移動しない方針のため対象外(N/A)
- [-] `DirectX12.sln`への追加 — 同上、ビルド成果物のみの配置のため対象外(N/A)

## フェーズ4: `.fbx`直読みパスの扱いを決める

- [ ] 全アセットが`.mdl`に変換された時点で、ランタイムの`.fbx`ローダー(Assimp依存)を
      残すか削除するかを判断。削除できればReleaseビルドからAssimp依存が完全に消える
- [ ] 自動化(アセットのタイムスタンプ比較などで`.fbx`更新時に`.mdl`を再生成する簡易スクリプト)は
      このタイミングで検討する。今のところ手動実行のみ

## 保留中の拡張(トリガー待ち)

実装しない理由は[DESIGN.md](DESIGN.md)参照。それぞれ「これが起きたら着手する」条件だけ決めておく。

| 項目 | 着手条件 |
|------|----------|
| `.mat`(マテリアル分離) | ランタイム側でマテリアルをメッシュ間で共有するID/参照の仕組みができたとき |
| `.anm`(アニメーション) | エンジンにボーン/スキニング/アニメーション再生機構が実装されたとき |

## 非スコープ

このロードマップおよびこのリポジトリでは扱わないこと:

- GameEngine本体のレンダリングパイプライン変更
- アニメーション・スキニングのランタイム実装そのもの(発生したら別途設計する)
- CI/自動ビルドの構築(フェーズ4で「検討する」までで、今回は実装しない)
