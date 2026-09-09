// Debug — 初期化まわりの失敗理由をデバッグ出力（Visual Studio の出力ウィンドウ）に出す
//
// Application::Init() が false を返すと main() が -1 を返して終わるだけなので、
// 外からはどこで失敗したのか分からない。失敗する箇所で
//
//     if (FAILED(hr)) return DebugFail("PeraRenderer::Init", "法線マップの読み込み", hr);
//
// のように return と一緒に書いておくと、理由が出力ウィンドウに残る。常に false を返す。
#pragma once

#include <winerror.h>

bool DebugFail(const char* where, const char* what);
bool DebugFail(const char* where, const char* what, HRESULT hr);

// nullptr を返す関数用。書式は同じで、戻り値だけ違う。
decltype(nullptr) DebugFailNull(const char* where, const char* what);
decltype(nullptr) DebugFailNull(const char* where, const char* what, HRESULT hr);
