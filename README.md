# MyDirectXRenderer

『DirectX12の魔導書』を読みながら作っている DirectX12 の学習用プロジェクト。
PMD モデルの表示と、Gregory 曲面（四辺形メッシュの XVL ラウンディング）の表示ができる。

## 必要環境

- Windows 10 / 11
- Visual Studio 2022（プラットフォーム ツールセット v143）
- Windows SDK 10

## セットアップ

### 1. クローン

glm を submodule で参照しているため `--recursive` を付ける。

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

## Gregory用LatticeMesh
プログラム内にモデルをハードコーディングしているため、モデルを追加する必要なく Gregory 曲面の表示はそのまま動く。
**参考文献・アルゴリズムについて**
- **ラウンディング手法**: Lattice Mesh からの曲面生成アルゴリズムは、XVLの基礎論文である[Akira Wakita, Makoto Yajima, Tsuyoshi Harada, Hiroshi Toriya, and Hiroaki Chiyokura. 2000. XVL:
A Compact And Qualified 3D Representation
With Lattice Mesh and Surface for the Internet. ](https://scispace.com/pdf/xvl-a-compact-and-qualified-3d-representation-with-lattice-4yfa556291.pdf)をベースにしている。
- **曲面の評価式**: Gregory パッチの具体的な面の方程式や、$u, v$ パラメータから曲面上の頂点座標を計算する処理については、[Charles Loop, Scott Schaefer, Tianyun Ni, and Ignacio Castaño. 2009. Approximating subdivision surfaces with Gregory patches for hardware tessellation. ACM Trans. Graph. 28, 5 (December 2009), 1–9. https://doi.org/10.1145/1618452.1618497](https://dl.acm.org/doi/abs/10.1145/1618452.1618497) などの文献を参考に実装を行っている。

## PMD モデル（任意）

`MyDirectXRenderer/Model/` に `初音ミク.pmd` を置くと PMD モデルが表示される。
ライセンスの都合でリポジトリには含めていない。

モデルが無い場合は読み込みをスキップされる。



## 依存関係

| 名前 | 取得方法 | 用途 |
|---|---|---|
| glm | submodule (`MyDirectXRenderer/External/glm`) | Gregory core が使う数学ライブラリ |
| DirectXTex | NuGet (`directxtex_desktop_win10`) | テクスチャ読み込み |
| d3dx12.h | リポジトリに同梱 | D3D12 ヘルパー |
| Gregory core | `MyDirectXRenderer/Gregory/core`（別リポジトリからのコピー） | Gregory 曲面の評価と XVL ラウンディング |
