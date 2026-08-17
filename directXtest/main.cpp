
#include <Windows.h>
#include "Application.h"

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif

#pragma region 1. ウィンドウの生成
	auto& app = Application::Instance();
	if (!app.Init()) {
		return -1;
	}
#pragma region 5. メインループ
	app.Run();
	app.Terminate();
#pragma endregion 5. メインループ
	return 0;
}