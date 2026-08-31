#include "TexturePool.h"

#include <cassert>

namespace rg {

static size_t BytesPerPixel(Format f) {
    switch (f) {
        case Format::RGBA8_UNorm: return 4;
        case Format::D32_Float:   return 4;
    }
    return 4;
}

size_t EstimateSizeBytes(const TextureDesc& desc) {
    return static_cast<size_t>(desc.width) * desc.height * BytesPerPixel(desc.format);
}

uint64_t HashDesc(const TextureDesc& desc) {
    // FNV-1a。desc の「同一性」を決めるのは寸法と形式だけ。
    // clear 値は同一性に含めない（同じリソースを違う clear 値で使い回せる）。
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(desc.width);
    mix(desc.height);
    mix(static_cast<uint64_t>(desc.format));
    return h;
}

void TexturePool::BeginFrame() {
    ++_frame;
    for (auto& e : _entries) {
        e.inUseThisFrame   = false;
        e.freshlyAllocated = false;
    }
}

uint32_t TexturePool::Acquire(const std::string& name, const TextureDesc& desc,
                              uint8_t usageFlags) {
    const uint64_t hash = HashDesc(desc);

    // --- 既存エントリを探す：名前 + desc ハッシュの複合キー ---
    // desc だけで引くと pera1 / pera2（desc が同一）が衝突するので名前も見る。
    for (uint32_t i = 0; i < _entries.size(); ++i) {
        auto& e = _entries[i];
        if (e.inUseThisFrame) continue;          // 同フレーム内で二重に貸さない
        if (e.name != name || e.descHash != hash) continue;

        e.inUseThisFrame = true;
        e.lastUsedFrame  = _frame;
        return i;
    }

    // --- 無いので新規確保。ここが予算の関門 ---
    const size_t size = EstimateSizeBytes(desc);
    if (_budgetBytes != 0 && _usedBytes + size > _budgetBytes) {
        return kInvalidPoolEntry;  // 事前に弾く
    }

    Entry e;
    e.name             = name;
    e.descHash         = hash;
    e.desc             = desc;
    e.sizeBytes        = size;
    e.physicalId       = _allocator.Allocate(name, desc, usageFlags, size);
    e.state            = State::Undefined;  // 新規なので「最初に必要な状態で作る」扱い
    e.lastUsedFrame    = _frame;
    e.inUseThisFrame   = true;
    e.freshlyAllocated = true;

    _usedBytes += size;
    _entries.push_back(std::move(e));
    return static_cast<uint32_t>(_entries.size() - 1);
}

void TexturePool::EndFrame(uint64_t fenceValue) {
    // 一定フレーム要求されなかったエントリを保留キューへ。
    // ★ここで即 Release してはいけない：GPU がまだ読んでいる可能性がある。
    for (size_t i = 0; i < _entries.size();) {
        auto& e = _entries[i];
        const bool stale = !e.inUseThisFrame && (e.lastUsedFrame + kEvictAfterFrames < _frame);
        if (!stale) {
            ++i;
            continue;
        }
        _pending.push_back(Pending{ e.physicalId, e.sizeBytes, fenceValue });
        _entries.erase(_entries.begin() + static_cast<long>(i));
        // 注意: erase でインデックスがずれる。VirtualResource::poolEntry は
        //       フレーム内でしか有効でないので問題ないが、跨いで保持してはいけない。
    }
}

void TexturePool::Reclaim(uint64_t completedFence) {
    for (size_t i = 0; i < _pending.size();) {
        if (_pending[i].fenceValue > completedFence) {
            ++i;
            continue;
        }
        _allocator.Release(_pending[i].physicalId);
        // 保留中は予算を消費し続けていた。ここで初めて解放される。
        assert(_usedBytes >= _pending[i].sizeBytes);
        _usedBytes -= _pending[i].sizeBytes;
        _pending.erase(_pending.begin() + static_cast<long>(i));
    }
}

bool TexturePool::HasEntryFor(const std::string& name) const {
    for (const auto& e : _entries)
        if (e.name == name) return true;
    return false;
}

}  // namespace rg
