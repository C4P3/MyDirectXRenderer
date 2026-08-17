# Gregory core（コピー）

`core/` は Gregory 曲面プロジェクトの `src/core/` をそのままコピーしたもの。
GL にもアプリにも依存せず、glm だけに依存する。

- 元リポジトリ: https://github.com/C4P3/Gregory（リアルタイム Gregory 曲面描画パイプライン）
- コピー元のコミット: `f8fbaa4`
- コピーした日: 2026-08-17

MSVC が UTF-8 として読めるよう、BOM 付き UTF-8 で保存している（元は BOM 無し）。
本家を更新したときは、このコミットからの差分を取って反映する。
