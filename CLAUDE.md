# CLAUDE.md

## プロジェクト
『DirectX12の魔導書』を読みながらDirectX12を学習中。最終目標はMMDモデルを動かせる自作エンジン。
C++ / Visual Studio / Windows / DirectX12（DirectXTex, d3dx12.h を使用）

プロジェクトの現状・構成・設計上の迷い・今後のロードマップは [README.md](./README.md) に書いてある。
このプロジェクトについて答える前に README.md を読むこと。進捗の記録は README.md 側で管理していて、この CLAUDE.md には書かない。

## 依頼の仕方
- 明示的な指示がない限りファイルの作成・編集はしない。聞いたことにチャットで答えるだけ。実装例を聞かれた場合はチャット上で表示をする。
- 本の流れに沿って進めたいので、先回りして高度な設計を提案しすぎない。聞かれた範囲で答える。
- 日本語で答える。

## 注意
ソースの文字コードは UTF-8 に統一している。

- `.h` / `.cpp` は **BOM 付き**（MSVC が UTF-8 と判別するため）
- `.hlsl` / `.hlsli` は **BOM 無し**（fxc が BOM を受け付けず `error X3000: Illegal character in shader file` になるため）
- `External/` と `d3dx12.h` は他所から持ってきたものなので対象外

新しく作るファイルもこれに合わせること。
