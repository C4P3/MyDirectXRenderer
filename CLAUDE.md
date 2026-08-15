# CLAUDE.md

## プロジェクト
『DirectX12の魔導書』を読みながらDirectX12を学習中。最終目標はMMDモデルを動かせる自作エンジン。
C++ / Visual Studio / Windows / DirectX12（DirectXTex, d3dx12.h を使用）

## 現状
- `Application` … ウィンドウ生成とメッセージ処理
- `Dx12Wrapper` … デバイス・スワップチェーン・RTV・フェンス、`BeginDraw`/`EndDraw`、バッファとテクスチャの生成
- `main.cpp` … 未整理のものなど

## 今後
これらの予定だが固まり切っていない
- `Application`
- `Dx12Wrapper` viewMatrix, projMatrix, Init(), UPDATE(), Draw()
- `PMDRenderer` PMD共通 pipeline, rootSignature, Update(), Draw()
- `PMDActor` モデルごと vBView, iBView, transBuffer, materialNum, materialHeap, worldMatrix, Load(), Update(), Draw()

## 依頼の仕方
- **コードは書かないでほしい。** ファイルの作成・編集はしない。聞いたことにチャットで答えるだけ。
- 欲しいのは解説・アドバイス・実装例の断片。全文の書き直しはしない。
- 実装例は必要な箇所だけ短く。「なぜそうするか」を一言添える。
- 本の流れに沿って進めたいので、先回りして高度な設計を提案しすぎない。聞かれた範囲で答える。
- 日本語で簡潔に。

## 注意
ソースの文字コードは Shift-JIS。コメントが文字化けして読めないことがあるので、その場合はコード部分だけを見て判断し、必要なら聞いてほしい。
