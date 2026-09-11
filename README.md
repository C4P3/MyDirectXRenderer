# MyDirectXRenderer

『DirectX12の魔導書』を読みながら作っている DirectX12 の学習用プロジェクト。

本の題材である PMD モデル（MMD）の表示に加えて、ラティスメッシュから Gregory 曲面を生成して表示する経路を独自に追加している。最終的には MMD モデルを動かせる自作エンジンにしつつ、大規模な形状データを扱うための仕組み（テッセレーションの GPU 化、GPU メモリ管理、プロキシ表現）を試す土台にしたい。

![Cube](./screenshots/cube.png "Cube")
![Torus](./screenshots/torus.png "Torus")

## 現在動くもの

### PMD モデル

- PMD ファイルのパース（頂点・インデックス・マテリアル・テクスチャパス・ボーン・IK）
- マテリアルごとの描画とテクスチャ読み込み（DirectXTex）
- **トゥーン**（`toon` テクスチャによる輝度の量子化）、**スフィアマップ**（`sph` 乗算 / `spa` 加算）、スペキュラ、アンビエント
- モデルが無い場合は読み込みをスキップして起動する

### ボーンアニメーション（VMD）

- PMD のボーン階層を `BoneNode` のツリーに組み、「センター」から `RecursiveMatrixMultiply()` で親子の行列を合成
- ボーン行列 256 本を `Transform`（`b2`）で渡し、頂点シェーダ側でスキニング（ボーン 2 本 + ウェイトの線形補間）
- VMD モーションのパースと再生。`timeGetTime()` の経過時間を 30fps 換算し、`_duration` を超えたらループする
- キーフレーム間はベジェ曲線で補間（`GetYFromXOnBezier()`）。回転はクォータニオンの Slerp、移動は X / Y / Z 軸ごとに別のカーブを使う

### IK

- 間のノード数で解法を切り替える。1 なら LookAt、2 なら余弦定理 IK、3 以上なら CCD-IK
- CCD-IK は試行回数と 1 回あたりの回転制限（`PMDIK::limit`）を PMD から読む
- ボーン名に「ひざ」を含むものを拾っておき、余弦定理 IK ではその回転軸を X 軸に固定する
- VMD の IK オンオフ情報（`VMDIKEnable`）をフレーム番号で逆引きし、オフになっている IK はスキップする

### Gregory 曲面（ラティスメッシュ）

- ハードコードしたラティスメッシュ（トーラスやキューブ）を `roundLattice()` で Gregory パッチ列に変換
- **CPU 側**で各パッチを `segments × segments` に分割して $u, v$ をサンプルし、頂点・法線・インデックスを構築（`GregoryActor::BuildMesh()`）
- 生成した三角形メッシュを通常の頂点/インデックスバッファとして描画。シェーディングは法線ベースの単色ライティングのみ

### マルチパスレンダリング（ペラポリゴン）

- パスは 4 段構成で、順番もリソースの状態遷移も後述の RenderGraph が解決する
  1. 3D（PMD / Gregory）をオフスクリーン 1 枚目へ描く
  2. 1 枚目を画面全体のペラポリゴン（トライアングルストリップ 4 頂点）に貼り、**横方向のガウシアンぼかし**をかけて 2 枚目へ
  3. 2 枚目を同じペラポリゴンに貼り、**縦方向のガウシアンぼかし**をかけてバックバッファへ
  4. ImGui をバックバッファに上書きする
- ガウシアンウェイトは CPU 側で 8 個（σ = 3.0）計算し、`float4[2]` の定数バッファ（`b0`）で渡す。頂点シェーダとルートシグネチャは共通にして、ピクセルシェーダだけ違う PSO を 2 つ作って切り替えている
- `peraPixel.hlsl` には他に試したポストエフェクト（モノクロ・色反転・減色・ディザ・エンボス・シャープネス・輪郭線抽出・重み固定のガウシアン）がコメントで残っている

### シャドウマップ

- ライトから見た深度だけを 1024×1024 に焼くパスを RenderGraph の先頭に置いている。カラーアタッチメント無し・ピクセルシェーダー無しの PSO で、ビューポートは書き込み先のサイズから導出されるのでパス側は何もしない
- 深度を SRV で読むため `R32_TYPELESS` で確保し、ビューのフォーマットを用途ごとに変える（DSV なら `D32_FLOAT`、SRV なら `R32_FLOAT`）
- ライトカメラは `Scene` が持つ「`_shadowCenter` を中心とする半径 `_shadowRadius` の球」に合わせて組む。カメラの位置とは無関係にしてあるので、視点を動かしても影の範囲と解像度が変わらない
- 影の判定は比較サンプラー（`SamplerComparisonState` + `SampleCmpLevelZero`）。アドレスモードは BORDER で境界色が白（深度 1.0）なので、シャドウマップの外に落ちた点は影にならない。**3×3 の PCF** で輪郭をぼかしている
- キャスターは PMD と Gregory、レシーバーは PMD / Gregory / 地面。シャドウマップの参照（`t4` / `s2`）と PCF は `ShadowShaderHeader.hlsli` にまとめて 3 つのシェーダで共有している
- `b0` の宣言も `SceneShaderHeader.hlsli` 1 箇所にまとめた。ライト方向も `b0` 経由で渡すので、シャドウマップを焼いた向きとライティングの向きが食い違わない

### RenderGraph

パスの並びとリソースの状態遷移をデータから解決する仕組み。`MyDirectXRenderer/RenderGraph/` にある。

- パスは毎フレーム `Application::BuildGraph()` で宣言し直す。setup でリソースの読み書きを宣言し、
  execute では描画だけを行う（setup では GPU コマンドを積まない）
- リソースは `TextureHandle`（仮想リソース ID + バージョン）で受け渡す。書き込むと新しいバージョンが返るので、
  パス間の依存とその順序が宣言から決まる
- `Compile()` が用途フラグ（RT / DSV / SRV）の導出、カリング、ライフタイム算出、物理リソースの確保、
  **バリアの導出**まで行う。定常フレームで 6 個のバリアを出す
- アタッチメント（スロットと `LoadOp`）の宣言から `OMSetRenderTargets` / `Clear*View` / `RSSetViewports` が
  決まる。ビューポートは書き込み先のサイズから導出するので、解像度の違うパスが来てもパス側は何もしなくてよい
- オフスクリーンと深度の実体は `TexturePool` が持ち、生成は `Dx12ResourceAllocator` の一箇所に集約されている。
  ディスクリプタ（RTV / DSV / SRV）の確保もそこで行う
- **論理層は D3D12 に依存しない。** 継ぎ目は `CommandContext`（コマンドの発行）と `IResourceAllocator`
  （物理リソースの確保）の 2 つだけで、DX12 実装は `RenderGraph/Dx12/` に隔離してある。
  `Experiments/RenderGraph` から GPU なしで単体テストできる（テスト 11 本）

### 共通

- PMD と Gregory の 2 つを同一シーンに置き、`Update()` で回転させている
- ImGui によるデバッグ UI。カメラ（eye / target / fovY / near-far）をリアルタイムに変更できる。ImGui はぼかし後のバックバッファに直接描いている
- 深度バッファ、ダブルバッファリング、フェンスによる GPU 待ち

## 構成

| クラス | 役割 |
|---|---|
| `Application` | シングルトン。ウィンドウ生成、メッセージ処理、メインループ（UI 構築 → 論理更新 → 描画）、各モジュールの所有 |
| `Dx12Wrapper` | デバイス・スワップチェーン・バックバッファと RTV・コマンドリスト・フェンス、`EndDraw()`、バッファとテクスチャの生成 |
| `RenderGraph` | パスとリソースの宣言を受け取り、実行順・カリング・ライフタイム・バリアを導出する論理層（D3D12 非依存）|
| `TexturePool` | フレームを越えて物理リソースを持ち回すプール。メモリ予算の関門でもある |
| `DescriptorHeap` | shader-visible な CBV_SRV_UAV ヒープ 1 枚と、そこからの連続スロット割り当て。所有は `Dx12Wrapper` |
| `Dx12CommandContext` / `Dx12ResourceAllocator` | RenderGraph の DX12 バックエンド。バリアの発行、リソース生成、ディスクリプタ管理 |
| `Scene` | ビュー行列・プロジェクション行列と視点位置を持つ定数バッファ（`b0`）。ImGui でのカメラ操作もここ |
| `PMDRenderer` / `PMDActor` | 前者がルートシグネチャと PSO、後者が頂点/インデックス/ワールド行列/マテリアルとディスクリプタヒープ。Actor 側がボーン階層・VMD モーション・IK ソルバも持つ |
| `GregoryRenderer` / `GregoryActor` | 同じ構造の Gregory 用。Actor がラティスメッシュとパッチ列も保持する |
| `GroundRenderer` | 原点に置いた水平な板。影を受けるためだけのもので、Actor は持たない |
| `PeraRenderer` | 画面全体を覆うペラポリゴンの頂点バッファ、ぼかしウェイトの定数バッファ、横/縦ぼかし用の PSO（`DrawHorizontal()` / `DrawVertical()`）。読むテクスチャは RenderGraph が解決して渡す |
| `Gregory/core` | ラティスメッシュのラウンディングと Gregory パッチの評価（描画に依存しない純粋な形状処理） |

Renderer は Actor を非所有ポインタで保持し、所有者は `Application`。

## 今の設計上の迷い

このプロジェクトで自分がまだ答えを出せていないところ。

1. **`Dx12Wrapper` の責務が広い**（RenderGraph 導入で一部解消）
   デバイス管理・フレーム制御・リソース生成が 1 クラスに同居していた。レンダーターゲットまわり（オフスクリーン・深度・そのディスクリプタ・パスの切り替え）は RenderGraph 側へ出たので、今残っているのはデバイスとスワップチェーン、フレーム制御、それと Actor が使う `CreateBuffer()` / `CreateTextureFromData()` 系。後者をどこへ置くかは 4 と同じ話。

2. **モデル種別ごとに Renderer を分ける設計が持つのか**
   今は `PMDRenderer` / `GregoryRenderer` が各々ルートシグネチャと PSO を丸ごと持っている。種類が増えたときの共通化の置き場所が決まっていない。「パス × モデル種別」の 2 軸のうち、**パス側は RenderGraph で解決した**（実行順もバリアも宣言から決まり、パスの並びは `Application::BuildGraph()` にデータとして置いてある）。残っているのはモデル種別側。シャドウマップで「同じモデルを別のパスで別の PSO で描く」が実際に出てきたが、今は `PMDRenderer` / `GregoryRenderer` がそれぞれ `_shadowPipelineState` を持ち、`DrawShadow()` を生やす形で通している。種類が増えたときにこの重複をどう畳むかは未解決。

3. **`Update()` / `Draw()` の責務分割**
   現在は `Application::Run()` が `Scene` と各 Actor の `Update()` を直接呼び、パス切り替えと Renderer の `Draw()` を順に並べている。アニメーションと IK を入れた結果 `PMDActor` にローダー・モーション再生・IK ソルバ・描画が同居して肥大化してきたので、どこで分けるか。方向としては 2 つ考えている。

   - **継承とポリモーフィズム**: `Actor` / `Renderer` の抽象基底クラスを作って `Update()` / `Draw()` を仮想関数にし、`Application` は `Actor` のリストを回すだけにする。今 `_pmdActor` / `_gregoryActor` を個別に持って手で並べて呼んでいる部分がそのまま解消するので、現状からは一番素直に伸ばせる。ただしこれは「Application が種類を知らなくて済む」話であって、`PMDActor` 内部の肥大化（ローダー・モーション再生・IK ソルバ・描画の同居）はクラス分割で別に解く必要がある。
   - **ECS（Entity Component System）**: 状態をコンポーネントに分け、モーション再生や IK を System 側に出す。`PMDActor` の肥大化そのものに効きそうなのはこちらだが、自分がまだ勉強不足。ECS なアーキテクチャについて勉強して、EnTT を組み込んで再設計してみたい。

   順番としては、まず基底クラスで `Application` 側の呼び出しを整えつつ `PMDActor` を役割ごとに分け、そのうえで ECS を試すのが現実的か。

4. **リソース生成の所在**（半分解決）
   レンダーターゲット系は `TexturePool` + `Dx12ResourceAllocator` に集約され、生成経路は一箇所になった。GPU メモリ上限もここに置ける。
   ディスクリプタについても、CBV_SRV_UAV は同時に 1 枚しかバインドできない都合で `DescriptorHeap` に集約した。所有は `Dx12Wrapper` で、`Dx12ResourceAllocator` も各 Actor も参照で借りるだけ。RenderGraph 側に置くと Actor がバックエンドに依存してしまうので、意図的に外に出してある。バインドは `Dx12CommandContext::BeginPass()` がパスの頭で 1 回だけ行う。
   一方で各 Actor は今も `Dx12Wrapper&` を持って自分で頂点・インデックス・定数バッファを作っている。こちらは UPLOAD ヒープで状態遷移が起きず、寿命もフレームを跨ぐので RenderGraph の管理対象にはならない。ただし**メモリ予算は同じ財布で数える必要がある**はずで、予算カウンタをプールの外に出して両方の経路から叩く形になりそう。

## ロードマップ

### A. 本の流れで進める学習項目

土台を揃えるための項目。順に進める予定。

- ~~ボーンアニメーション（VMD モーション再生）~~ → 実装済み（ベジェ補間まで）
- ~~IK~~ → 実装済み（LookAt / 余弦定理 / CCD-IK、VMD の IK オンオフ対応）
- ~~マルチパスレンダリング~~ → 実装済み（オフスクリーン 2 枚とガウシアンぼかし）。その後 RenderGraph に載せ替えた
- ~~シャドウマップ~~ → 実装済み（TYPELESS 対応、比較サンプラー、3×3 PCF、影を受ける地面まで）
- Effekseer によるエフェクト追加 → 次はここ

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

### PMD モデル / VMD モーション（任意）

`MyDirectXRenderer/Model/` に `初音ミク.pmd` を置くと PMD モデルが表示される。さらに `MyDirectXRenderer/Motion/` に `squat.vmd` を置くと、そのモーションを再生する。どちらもライセンスの都合でリポジトリには含めていない（`.pmd` / `.vmd` は `.gitignore` 済み）。モデルが無い場合は読み込みをスキップする。

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
