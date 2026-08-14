#pragma once
#include <Windows.h>

class Application
{
private:
    HWND hwnd = nullptr;
    WNDCLASSEX w = {};
    int window_width;
    int window_height;

public:
    Application(int width, int height); // 宣言のみ
    ~Application();

    bool Init();
    bool ProcessMessage(bool& quit);

    HWND GetWindowHandle() const { return hwnd; } // 1行で終わる単純なゲッターはヘッダーに書いてもOK (インライン化)
    int GetWindowWidth() const { return window_width; }
    int GetWindowHeight() const { return window_height; }
};