// CommandContext — RHI 依存の唯一の継ぎ目
//
// 論理層（RenderGraph）はこのインターフェースしか知らない。
// Mac では LoggingCommandContext を使って Compile()/Execute() の結果を文字列で検証する。
// Windows では ID3D12GraphicsCommandList をラップした実装に差し替える。
#pragma once

#include <string>
#include <vector>

#include "RenderGraph.h"

namespace rg {

class CommandContext {
public:
    virtual ~CommandContext() = default;

    virtual void Transition(const std::string& resource, State from, State to) = 0;

    // TODO: DX12 実装ではここでアタッチメント一覧（RTV ハンドルと LoadOp）を受け取り、
    //       OMSetRenderTargets / Clear*View を呼ぶことになる。
    virtual void BeginPass(const std::string& name) = 0;
    virtual void EndPass()                          = 0;

    // execute ラムダ側から呼ぶ、描画の代わりのマーカー（Mac 検証用）
    virtual void Draw(const std::string& what) = 0;
};

// Mac 用のダミーバックエンド。起きたことを 1 行ずつ積むだけ。
class LoggingCommandContext : public CommandContext {
public:
    void Transition(const std::string& resource, State from, State to) override {
        log.push_back(resource + ": " + ToString(from) + " -> " + ToString(to));
    }
    void BeginPass(const std::string& name) override { log.push_back("[" + name + "]"); }
    void EndPass() override {}
    void Draw(const std::string& what) override { log.push_back("  draw " + what); }

    // バリア行だけを抜き出す（順序と本数の答え合わせ用）
    std::vector<std::string> Transitions() const {
        std::vector<std::string> out;
        for (const auto& line : log)
            if (line.find(" -> ") != std::string::npos) out.push_back(line);
        return out;
    }

    std::vector<std::string> log;
};

}  // namespace rg
