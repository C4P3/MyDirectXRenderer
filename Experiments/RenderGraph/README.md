# RenderGraph 論理層のテスト

RenderGraph 導入を検討するための実験として始めたもの。

**論理層そのものは本体（`MyDirectXRenderer/RenderGraph/`）へ移動済みで、ここに残っているのは
テスト（`main.cpp`）とそのビルド定義（`Makefile`）だけ。** 論理層は D3D12 に依存しないので、
本体のソースを相対パスで参照して Mac の clang で単体ビルド・実行できる。

```
make test
```

参照先は `Makefile` の `RG_DIR`（= `../../MyDirectXRenderer/RenderGraph`）。
DX12 実装は `RenderGraph/Dx12/` にあり、こちらからは参照しない。

サニタイザ付き（このマシンの macOS では ASan ランタイムが起動時に固まったので既定では無効）:

```
make SAN=1 test
```

## 何を実装しているか

論理層（`MyDirectXRenderer/RenderGraph/` 直下）:

| ファイル | 中身 |
|---|---|
| `RenderGraph.h` | 型定義。`TextureHandle` / `TextureDesc` / `Access` / `Attachment` / `VirtualResource` / `RenderGraph::Builder` |
| `RenderGraph.cpp` | `Compile()` の 7 ステップと `Execute()` |
| `CommandContext.h` | **RHI の継ぎ目 その 1**。テスト用のログ出力実装 `LoggingCommandContext` を同居させてある |
| `TexturePool.h` / `.cpp` | フレームを越えて物理リソースを持ち回すプール。**RHI の継ぎ目 その 2**（`IResourceAllocator`） |

DX12 実装（`MyDirectXRenderer/RenderGraph/Dx12/`）:

| ファイル | 中身 |
|---|---|
| `Dx12CommandContext.h` / `.cpp` | バリアの発行と、`BeginPass()` での `OMSetRenderTargets` / `Clear*View` / `RSSetViewports` |
| `Dx12ResourceAllocator.h` / `.cpp` | `CreateCommittedResource` と RTV / DSV / SRV ディスクリプタの確保。`physicalId` から実体を引ける唯一の場所 |

テスト:

| ファイル | 中身 |
|---|---|
| `main.cpp` | テスト 11 本 |

`Compile()` の中身:

1. **用途フラグの集計** — `TextureDesc` に RT/DSV/SRV を書かせず、全パスの宣言から導出する
2. **辺の導出** — リソースのバージョニングにより write-after-write が自動で read-after-write になる
3. **カリング** — 参照カウントの連鎖。根は `requiredFinalState` を持つリソース（= `backbuffer`）
4. **実行順** — ハンドルを繋ぐ API なので辺は必ず「宣言が先のパス → 後」を向く。**トポロジカルソートは不要**
5. **ライフタイム** — 実行順での first-use / last-use。区間を出すところまで（エイリアシングは未実装）
6. **物理リソースの取得** — `TexturePool` から引く。カリングされたリソースは確保しない
7. **バリア導出** — 抽象的な状態遷移リストを吐く。`D3D12_RESOURCE_STATES` への変換はバックエンド側の仕事

## TexturePool の設計判断

1. **キーは `(name, descHash)` の複合キー。**
   `desc` だけでは足りない — `pera1` と `pera2` は desc が完全に同一なので衝突する。
   ライフタイムが重なる（`[0..1]` と `[1..2]` がパス 1 で重複）ので共有もできない。
   名前だけでもダメで、ウィンドウリサイズ後に古いサイズのリソースを返してしまう。
2. **状態（`State`）は物理リソース側が持つ。** `VirtualResource` は `poolEntry` インデックスだけを持つ。
3. **解放は遅延させる。** ダブルバッファリングしているので、フレーム N で不要になったリソースは
   フレーム N のフェンスが通るまで破棄できない。`EndFrame(fenceValue)` で保留キューに送り、
   `Reclaim(completedFence)` で実際に破棄する。**保留中のリソースは予算を消費し続ける**
   （まだメモリ上にあるため）。
4. **追い出しは「`kEvictAfterFrames`(=3) フレーム未使用」。** パス構成が変わる
   （CPU/GPU テッセレーション切り替え、デバッグパスの ON/OFF）とリソース要求が変わるので、
   放っておくとプールが太り続ける。
5. **プールは `RenderGraph` の外。** `Clear()` を越えて生き残る必要があり、
   かつメモリ予算の関門にしたいので `Compile(TexturePool&)` として外から渡す。
   → README の迷い 4「リソース生成の所在」と、ロードマップ B「GPU メモリ上限管理」がここに着地する。

`SetBudgetBytes()` を超える確保は `Acquire()` が失敗し、`Compile()` が `false` を返して
`AllocationFailures()` に名前が載る。ロードマップ B の「上限を超えるロードを事前に弾く」に相当。
超過時にプロキシ（粗いテッセレーション / 低解像度テクスチャ）へ落とす処理は未実装。

## 設計の要点

- **setup と execute の分離**（Unity URP / Frostbite と同じ）。setup では GPU コマンドを積まないので、setup + `Compile()` を GPU なしで丸ごとテストできる
- **`SetRenderAttachment()` はスロットを明示**し、新しいバージョンのハンドルを返す。`[[nodiscard]]` なので繋ぎ忘れをコンパイラが検出する
- **アタッチメントスロットはグラフのアルゴリズムでは使わない**。execute 時に RTV を並べるためだけの情報で、そのままバックエンドに素通しする

## テストの答え合わせ

移行前の `Dx12Wrapper` が 1 フレームに手書きで発行していたバリアは **6 個**
（`PreDrawToPera` / `PostDrawToPera` / `PreDrawToPera2` / `PostDrawToPera2` / `BeginDraw` / `EndDraw` に各 1 つ）。
`TestCurrentGraph` の 2 フレーム目がこの 6 個と一致することを確認している。移行後もこの本数は変わらない。
`depth` はパス間の辺にならないので遷移ゼロ、という点も手書き時代と合っている。

1 フレーム目は 4 個。リソースを「最初に必要な状態」で作るため初期バリアが要らず、
2 フレーム目以降が定常状態になる。DX12 では生成時に状態を決める必要があるので、
`Acquire()` / `Allocate()` に「最初に必要な状態」を渡してこの性質を実機でも成立させている。

## まだ入れていないもの

- **メモリのエイリアシング** — ライフタイム区間は出すが、重ならないリソースを同じメモリに重ねる処理はしない。オフスクリーン 2 枚では節約の動機がないため、ロードマップ B まで来てから
- **内容がフレームを越えて残るリソース** — プールが再利用するのは「割り当て」だけで、内容は保証しない。TAA のヒストリバッファのように内容を持ち越したい場合は別概念（ping-pong か明示的な persistent フラグ）が必要
- **パスの型分離**（raster / compute）
- **`BufferHandle`** — 今はテクスチャだけ。GPU が作って GPU が読むバッファ（GPU テッセレーションの出力、
  GPU カリングの結果、`ExecuteIndirect` の引数）が出てきたときに必要になる。
  Actor がロードする頂点・インデックス・定数バッファは UPLOAD ヒープで状態遷移が起きないため、対象外
- **DX12 の state promotion / decay を考慮したバリアの削減** — 実機のデバッグレイヤを見ながらやる作業なので、今は「多めに出す」で固定
- **深度を SRV で読むこと** — TYPELESS で作ってビューごとにフォーマットを変える必要がある。
  今は `Dx12ResourceAllocator::Allocate()` の assert で弾いている。シャドウマップで必要になる
- **予算の設定** — `SetBudgetBytes()` を呼んでいないので、確保は失敗しない。
  `EstimateSizeBytes()` も `width * height * bpp` の概算のままで、Windows では
  `GetResourceAllocationInfo()` を使わないとアラインメント分がずれる
- **`Compile()` が `false` を返したときの扱い** — 今は assert で止まるだけ。
  プロキシ（粗いテッセレーション / 低解像度テクスチャ）へ落とす処理は未実装

### 入れ終わったもの

- **`CommandContext::BeginPass()` へのアタッチメント情報の受け渡し** — 宣言（スロットと `LoadOp`）から
  `OMSetRenderTargets` / `Clear*View` / `RSSetViewports` が決まるようになった。
  ビューポートは書き込み先のサイズから導出するので、解像度の違うパスが来てもパス側は何もしなくてよい
- **ディスクリプタ（RTV / DSV / SRV）の管理** — `Dx12ResourceAllocator` がヒープを持ち、確保と解放を行う
