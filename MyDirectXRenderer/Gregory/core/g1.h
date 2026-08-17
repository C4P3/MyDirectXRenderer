#pragma once
#include "core/patch_mesh.h"

namespace greg {

// 両立性補正（Takeda 2008 式(2.10)–(2.12)、原典は Chiyokura & Kimura 1983）。
//
// 境界曲線を共有する 2 パッチ A, B について:
//   - A 側の辺内部点を仮定 (2.10)（cross 接ベクトル場が線形）で置き直し、
//   - 角の関係 (2.12) B0 = k0·A0 + h0·C0, B3 = k1·A3 + h1·C2 から k,h を解き、
//   - (2.11) で B 側の辺内部点 B1, B2 を計算する。
// 角で B0 が span{A0, C0} に乗っていれば境界全域で厳密に G1 になる
// （乗っていない場合は最小二乗解で近似補正。残差は角に集中し (1−t)³ で減衰）。
// 境界制御点は動かさないので C0 接続は保存される。
void applyG1(GregoryPatch& A, int edgeA, GregoryPatch& B, int edgeB, bool flip);

// メッシュ内の全共有辺に適用
void applyG1(PatchMesh& mesh);

} // namespace greg
