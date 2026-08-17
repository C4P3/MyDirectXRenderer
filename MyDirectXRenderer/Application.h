#pragma once
#include <Windows.h>
#include <memory>
class Dx12Wrapper;
class PMDRenderer;
class PMDActor;
class Scene;
class GregoryRenderer;
class GregoryActor;

class Application
{
private:
    // シングルトンパターンのため、コンストラクタとデストラクタをprivateに隠蔽
    Application() = default;
    ~Application();

    // 意図しないコピーやムーブを防ぐ
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // ウィンドウ関連
    HWND _hwnd = nullptr;
    WNDCLASSEX _windowClass = {};

    // 各主要モジュールの管理（自動でメモリ解放される unique_ptr を使用）
    std::unique_ptr<Dx12Wrapper> _dx12;
    std::unique_ptr<PMDRenderer> _pmdRenderer;
    std::unique_ptr<PMDActor> _pmdActor;
    std::unique_ptr<GregoryRenderer> _gregoryRenderer;
    std::unique_ptr<GregoryActor> _gregoryActor;
    std::unique_ptr<Scene> _scene;
public:
    // 唯一のインスタンスを取得する（シングルトンへのアクセスポイント）
    static Application& Instance();

    // アプリケーションのライフサイクル
    bool Init();
    void Run();
    void Terminate();

    // ゲッター
    HWND GetWindowHandle() const { return _hwnd; };
    //SIZE GetWindowSize();
};