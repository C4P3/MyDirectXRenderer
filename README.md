# MyDirectXRenderer

『DirectX12の魔導書』を読みながら作っている DirectX12 の学習用プロジェクト。

本の題材である PMD モデル（MMD）の表示に加えて、**ラティスメッシュから Gregory 曲面を生成して表示する経路**を独自に追加している。最終的には MMD モデルを動かせる自作エンジンにしつつ、大規模な形状データを扱うための仕組み（テッセレーションの GPU 化、GPU メモリ管理、プロキシ表現）を試す土台にしたい。

![Cube](./screenshots/cube.png "Cube")
![Torus](./screenshots/torus.png "Torus")

## 現在動くもの

### PMD モデル

- PMD ファイルのパース（頂点・インデックス・マテリアル・テクスチャパス）
- マテリアルごとの描画とテクスチャ読み込み（DirectXTex）
- **トゥーン**（`toon` テクスチャによる輝度の量子化）、**スフィアマップ**（`sph` 乗算 / `spa` 加算）、スペキュラ、アンビエント
- モデルが無い場合は読み込みをスキップして起動する

### Gregory 曲面（ラティスメッシュ）

- ハードコードしたラティスメッシュ（トーラスやキューブ）を `roundLattice()` で Gregory パッチ列に変換
- **CPU 側**で各パッチを `segments × segments` に分割して $u, v$ をサンプルし、頂点・法線・インデックスを構築（`GregoryActor::BuildMesh()`）
- 生成した三角形メッシュを通常の頂点/インデックスバッファとして描画。シェーディングは法線ベースの単色ライティングのみ

### 共通

- PMD と Gregory の 2 つを同一シーンに置き、`Update()` で回転させている
- ImGui によるデバッグ UI。カメラ（eye / target / fovY / near-far）をリアルタイムに変更できる
- 深度バッファ、ダブルバッファリング、フェンスによる GPU 待ち

## 構成

| クラス | 役割 |
|---|---|
| `Application` | シングルトン。ウィンドウ生成、メッセージ処理、メインループ（UI 構築 → 論理更新 → 描画）、各モジュールの所有 |
| `Dx12Wrapper` | デバイス・スワップチェーン・RTV/DSV・フェンス、`BeginDraw()`/`EndDraw()`、バッファとテクスチャの生成 |
| `Scene` | ビュー行列・プロジェクション行列と視点位置を持つ定数バッファ（`b0`）。ImGui でのカメラ操作もここ |
| `PMDRenderer` / `PMDActor` | 前者がルートシグネチャと PSO、後者が頂点/インデックス/ワールド行列/マテリアルとディスクリプタヒープ |
| `GregoryRenderer` / `GregoryActor` | 同じ構造の Gregory 用。Actor がラティスメッシュとパッチ列も保持する |
| `Gregory/core` | ラティスメッシュのラウンディングと Gregory パッチの評価（描画に依存しない純粋な形状処理） |

Renderer は Actor を非所有ポインタで保持し、所有者は `Application`。

## 今の設計上の迷い

このプロジェクトで自分がまだ答えを出せていないところ。

1. **`Dx12Wrapper` の責務が広い**
   デバイス管理・フレーム制御・リソース生成が 1 クラスに同居している。Renderer を増やすたびにここが太っていくので、どこで層を切るべきか。

2. **モデル種別ごとに Renderer を分ける設計が持つのか**
   今は `PMDRenderer` / `GregoryRenderer` が各々ルートシグネチャと PSO を丸ごと持っている。種類が増えたときの共通化の置き場所が決まっていない。さらにマルチパス（影など）を入れると「パス × モデル種別」の 2 軸になるので、この分け方のままでは足りない気がしている。

3. **`Update()` / `Draw()` の責務分割**
   現在は `Application::Run()` が `Scene` と各 Actor の `Update()` を直接呼び、Renderer の `Draw()` を順に呼んでいる。アニメーションを入れて Actor 側の状態が増えたときに破綻するのではないか。ECS（Entity Component System）なアーキテクチャについて勉強してみたい。

4. **リソース生成の所在**
   各 Actor が `Dx12Wrapper&` を持って自分でバッファを作っている。後述の GPU メモリ上限管理を入れるなら、生成経路を一箇所に集約する必要が出てくるのではないか。

## ロードマップ

### A. 本の流れで進める学習項目

土台を揃えるための項目。順に進める予定。

- ボーンアニメーション（VMD モーション再生）
- IK
- シャドウマップと、そのためのマルチパスレンダリング
- Effekseer によるエフェクト追加

### B. ラティスメッシュと大規模データのための関心（未着手）

ここがまだ問題の形しか掴めていない。

- **ラティスメッシュ→表示までの CPU 処理の GPU 化**
  現状は `BuildMesh()` で CPU 側で曲面をサンプルして三角形に落としている。ハル/ドメインシェーダによるハードウェアテッセレーションのパイプラインを別に用意し、**CPU 版と切り替えて比較できる**形にしたい。形状が動く（変形する）ケースでは CPU 側の再構築がそのままボトルネックになるはずなので。
- **GPU メモリの上限設定と管理**
  上限を明示的に持ち、それを超えるロードを事前に弾けるようにしたい。今は各 Actor が無制限に確保できる。
- **劣化表現によるプロキシとレンダリング時のメモリ最適化**
  上限に収まらない場合に、粗いテッセレーションや低解像度テクスチャのプロキシに落として表示を維持する。LOD の切り替え基準をどう決めるかが分かっていない。
- **3D 点群データの表示**
  メッシュとは別のパイプラインとして追加してみたい。

### C. 単に物作りとして興味がある

- **簡易的なモデリング / アニメーション編集機能**
  IK・アニメーション・Gregory 曲面のリアルタイム評価が揃うなら、その場で変形アニメーションを作って直せるようにしたい、という動機。エンジンからツール側への飛躍なので A / B とは分けている。

## 必要環境

- Windows 10 / 11
- Visual Studio 2022（プラットフォーム ツールセット v143）
- Windows SDK 10

## セットアップ

### 1. クローン

glm と ImGui を submodule で参照しているため `--recursive` を付ける。

```
git clone --recursive https://github.com/C4P3/MyDirectXRenderer.git
```

クローン済みの場合は:

```
git submodule update --init
```

### 2. ビルド

`MyDirectXRenderer.sln` を Visual Studio 2022 で開き、構成を **Debug / x64** にしてビルドする。

DirectXTex は NuGet パッケージなので、初回ビルド時に自動で復元される（ネットワーク接続が必要）。

## モデルデータ

### Gregory 用ラティスメッシュ

プログラム内にモデルをハードコーディングしているため、モデルを追加する必要なく Gregory 曲面の表示はそのまま動く。

### PMD モデル（任意）

`MyDirectXRenderer/Model/` に `初音ミク.pmd` を置くと PMD モデルが表示される。ライセンスの都合でリポジトリには含めていない。モデルが無い場合は読み込みをスキップする。

## 参考文献・アルゴリズム

- **ラウンディング手法**: Lattice Mesh からの曲面生成アルゴリズムは、XVL の基礎論文である [Akira Wakita, Makoto Yajima, Tsuyoshi Harada, Hiroshi Toriya, and Hiroaki Chiyokura. 2000. XVL: A Compact And Qualified 3D Representation With Lattice Mesh and Surface for the Internet.](https://scispace.com/pdf/xvl-a-compact-and-qualified-3d-representation-with-lattice-4yfa556291.pdf) をベースにしている。
- **曲面の評価式**: Gregory パッチの具体的な面の方程式や、$u, v$ パラメータから曲面上の頂点座標を計算する処理については、[Charles Loop, Scott Schaefer, Tianyun Ni, and Ignacio Castaño. 2009. Approximating subdivision surfaces with Gregory patches for hardware tessellation. ACM Trans. Graph. 28, 5 (December 2009), 1–9.](https://dl.acm.org/doi/abs/10.1145/1618452.1618497) などの文献を参考に実装を行っている。

## 依存関係

| 名前 | 取得方法 | 用途 |
|---|---|---|
| glm | submodule (`MyDirectXRenderer/External/glm`) | Gregory core が使う数学ライブラリ |
| ImGui | submodule (`MyDirectXRenderer/External/imgui`) | デバッグ UI（カメラ操作） |
| DirectXTex | NuGet (`directxtex_desktop_win10`) | テクスチャ読み込み |
| d3dx12.h | リポジトリに同梱 | D3D12 ヘルパー |
| Gregory core | `MyDirectXRenderer/Gregory/core`（別リポジトリからのコピー） | Gregory 曲面の評価と XVL ラウンディング |
