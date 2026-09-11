// DescriptorHeap — shader-visible な CBV_SRV_UAV ヒープ 1 枚と、そこからのスロット割り当て
//
// D3D12 では CBV_SRV_UAV のヒープは同時に 1 枚しかバインドできない。
// アクターのマテリアルも RenderGraph のオフスクリーンも同じ描画の中で参照するので、
// 両方が同じヒープに載っている必要がある。ヒープの所有をここ 1 箇所にまとめて、
// Dx12ResourceAllocator も各 Actor も参照で借りるだけにする。
//
// 所有者は Dx12Wrapper。RenderGraph 側に置くと Actor が RenderGraph のバックエンドに
// 依存することになるので、意図的に外に出してある。
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <vector>

class DescriptorHeap {
public:
    // 連続したスロットの区間。ディスクリプタテーブルは連続していないと張れないので、
    // マテリアルのように「n 個まとめて」欲しい側はこれを持っておく。
    struct Range {
        static constexpr UINT kInvalid = 0xffffffffu;
        UINT first = kInvalid;
        UINT count = 0;
        bool Valid() const { return first != kInvalid; }
    };

    // shader-visible なヒープは後から伸ばせない（作り直すと GPU ハンドルが全部無効になる）。
    // capacity は最初に多めに取っておくこと。
    bool Init(ID3D12Device* dev, UINT capacity);

    // count 個の連続したスロットを確保する。足りなければ Valid() == false が返る。
    Range Alloc(UINT count = 1);
    void  Free(Range range);

    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT slot) const;

    UINT Increment() const { return _increment; }
    ID3D12DescriptorHeap* Raw() const { return _heap.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
    UINT _increment = 0;
    UINT _capacity  = 0;
    UINT _next      = 0;

    // 解放されたスロット。連続している保証がないので、再利用は 1 個確保のときだけ。
    // 複数確保は常に追記式で取る（今の用途では確保したら基本使い切るので足りる）。
    std::vector<UINT> _freeList;
};
