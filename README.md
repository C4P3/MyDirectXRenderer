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

## PMD モデル（任意）

`MyDirectXRenderer/Model/` に `初音ミク.pmd` を置くと PMD モデルが表示される。
ライセンスの都合でリポジトリには含めていない。

モデルが無い場合は読み込みをスキップされる。

## Gregory用LatticeMesh
プログラム内にモデルをハードコーディングしているため、モデルを追加する必要なく Gregory 曲面の表示はそのまま動く。

## 依存関係

| 名前 | 取得方法 | 用途 |
|---|---|---|
| glm | submodule (`MyDirectXRenderer/External/glm`) | Gregory core が使う数学ライブラリ |
| DirectXTex | NuGet (`directxtex_desktop_win10`) | テクスチャ読み込み |
| d3dx12.h | リポジトリに同梱 | D3D12 ヘルパー |
| Gregory core | `MyDirectXRenderer/Gregory/core`（別リポジトリからのコピー） | Gregory 曲面の評価と XVL ラウンディング |
