# RenderGraph 論理層のスケッチ

RenderGraph 導入を検討するための実験。**本体プロジェクト（`MyDirectXRenderer.vcxproj`）からは参照していない。**

D3D12 に依存しない論理層（Frontend）だけを実装してあるので、Mac の clang で単体でビルド・テストできる。

```
make test
```

サニタイザ付き（このマシンの macOS では ASan ランタイムが起動時に固まったので既定では無効）:

```
make SAN=1 test
```

## 何を実装しているか

| ファイル | 中身 |
|---|---|
| `RenderGraph.h` | 型定義。`TextureHandle` / `TextureDesc` / `Access` / `VirtualResource` / `RenderGraph::Builder` |
| `RenderGraph.cpp` | `Compile()` の 6 ステップと `Execute()` |
| `CommandContext.h` | **RHI の継ぎ目 その 1**。Mac 用のログ出力実装 `LoggingCommandContext` を同居させてある |
| `TexturePool.h` / `.cpp` | フレームを越えて物理リソースを持ち回すプール。**RHI の継ぎ目 その 2**（`IResourceAllocator`） |
| `main.cpp` | テスト 9 本 |

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

現状の `Dx12Wrapper` が 1 フレームに手書きで発行しているバリアは **6 個**
（`PreDrawToPera` / `PostDrawToPera` / `PreDrawToPera2` / `PostDrawToPera2` / `BeginDraw` / `EndDraw` に各 1 つ）。
`TestCurrentGraph` の 2 フレーム目がこの 6 個と一致することを確認している。
`depth` はパス間の辺にならないので遷移ゼロ、という点も現状のコードと合っている。

1 フレーム目は 4 個。リソースを「最初に必要な状態」で作るため初期バリアが要らず、
2 フレーム目以降が定常状態になる。

## まだ入れていないもの

- **メモリのエイリアシング** — ライフタイム区間は出すが、重ならないリソースを同じメモリに重ねる処理はしない。オフスクリーン 2 枚では節約の動機がないため、ロードマップ B まで来てから
- **内容がフレームを越えて残るリソース** — プールが再利用するのは「割り当て」だけで、内容は保証しない。TAA のヒストリバッファのように内容を持ち越したい場合は別概念（ping-pong か明示的な persistent フラグ）が必要
- **パスの型分離**（raster / compute）
- **`BufferHandle`** — 今はテクスチャだけ
- **DX12 の state promotion / decay を考慮したバリアの削減** — 実機のデバッグレイヤを見ながらやる作業なので、今は「多めに出す」で固定
- **`CommandContext::BeginPass()` へのアタッチメント情報の受け渡し** — DX12 実装で `OMSetRenderTargets` / `Clear*View` を呼ぶときに必要になる
- **ディスクリプタ（RTV / DSV / SRV）の管理** — `IResourceAllocator` の Windows 実装が面倒を見る部分。今の `Dx12Wrapper` は `_peraRTVHeap` / `_peraSRVHeap` を手で作っている
- **正確なサイズ計算** — `EstimateSizeBytes()` は `width * height * bpp` の概算。Windows では `GetResourceAllocationInfo()` を使わないとアラインメント分がずれる

## 文字コード

このディレクトリだけ **UTF-8**（本体は Shift-JIS）。Mac 上での実験用なので Visual Studio では開かない前提。
本体に取り込むときに変換する。
