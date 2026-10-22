#pragma once
#include <atomic>
#include <cstdint>

// The user's Stage57 local-fog draw marks. Skip only these graphics commands;
// HDRP still initializes and lights the volumetric buffers each frame.
class MarkedGroundFogRemoval {
public:
    static constexpr uint64_t PixelShader = 0xafdb0f0cc22f9ef0ull;
    static constexpr uint64_t VertexShader = 0x326cadd68150c1f1ull;
    void Enable(bool value) { enabled.store(value, std::memory_order_relaxed); }
    bool Enabled() const { return enabled.load(std::memory_order_relaxed); }
    static unsigned Match(uint64_t hash, unsigned stage, bool bytecode) {
        if (!bytecode) return 0;
        if (stage == 0 && hash == PixelShader) return 1;
        if (stage == 1 && hash == VertexShader) return 2;
        return 0;
    }
    void Record(unsigned matches) {
        ++draws;
        if (matches & 1) ++pixels;
        if (matches & 2) ++vertices;
    }
    uint64_t Draws() const { return draws.load(std::memory_order_relaxed); }
    uint64_t Pixels() const { return pixels.load(std::memory_order_relaxed); }
    uint64_t Vertices() const { return vertices.load(std::memory_order_relaxed); }
private:
    std::atomic<bool> enabled{};
    std::atomic<uint64_t> draws{}, pixels{}, vertices{};
};
