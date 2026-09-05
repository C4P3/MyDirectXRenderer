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

    // resource はデバッグ用の名前。実体を指すのは physicalId の方で、
    // それを ID3D12Resource* に解決するのはバックエンド（IResourceAllocator の実装）の仕事。
    virtual void Transition(const std::string& resource, uint32_t physicalId,
                            State from, State to) = 0;

    // パスが書き込む先。DX12 実装はここで OMSetRenderTargets / Clear*View /
    // RSSetViewports を済ませるので、execute ラムダは描画だけを行えばよい。
    virtual void BeginPass(const std::string& name, const PassAttachments& attachments) = 0;
    virtual void EndPass()                                                              = 0;

    // execute ラムダ側から呼ぶ、描画の代わりのマーカー（Mac 検証用）
    virtual void Draw(const std::string& what) = 0;
};

// Mac 用のダミーバックエンド。起きたことを 1 行ずつ積むだけ。
class LoggingCommandContext : public CommandContext {
public:
    void Transition(const std::string& resource, uint32_t, State from, State to) override {
        log.push_back(resource + ": " + ToString(from) + " -> " + ToString(to));
    }
    void BeginPass(const std::string& name, const PassAttachments& att) override {
        log.push_back("[" + name + "]");
        attachments.push_back({ name, att });
    }

    // パス名 → そのパスのアタッチメント（テストの覗き窓）
    const PassAttachments* AttachmentsOf(const std::string& name) const {
        for (const auto& e : attachments)
            if (e.first == name) return &e.second;
        return nullptr;
    }
    std::vector<std::pair<std::string, PassAttachments>> attachments;
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
