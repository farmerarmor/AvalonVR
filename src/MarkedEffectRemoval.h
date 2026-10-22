#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>

// User-selected RGB effect producer, captured in Stages35/38/39. A skipped
// compute dispatch alone retains its old output; black is neutral for this
// exact RGB-only texture. Unknown shaders/resources keep their native path.
class MarkedEffectRemoval {
public:
    static constexpr uint64_t Shader = 0x7728ef9458651dadull;
    void Enable(bool value) { enabled.store(value, std::memory_order_relaxed); }
    bool Enabled() const { return enabled.load(std::memory_order_relaxed); }
    uint64_t Cleared() const { return cleared.load(std::memory_order_relaxed); }
    uint64_t Unsupported() const { return unsupported.load(std::memory_order_relaxed); }

    bool Apply(ID3D11DeviceContext* context, uint64_t hash, unsigned stage, bool bytecode) {
        if (!Enabled() || hash != Shader || stage != 2 || !bytecode || !context) return false;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
        context->CSGetUnorderedAccessViews(0, 1, &output);
        if (!output) { ++unsupported; return false; }
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        output->GetDesc(&view);
        // Captures show a Texture2DArray view of a single-slice RGB texture.
        // Never clear an arbitrary structured/alpha/color/depth target.
        if (view.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
            view.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2DARRAY ||
            view.Texture2DArray.FirstArraySlice != 0 || view.Texture2DArray.ArraySize != 1) {
            ++unsupported; return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        output->GetResource(&resource);
        if (!resource || FAILED(resource.As(&texture))) { ++unsupported; return false; }
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Format != DXGI_FORMAT_R11G11B10_FLOAT || description.ArraySize != 1 ||
            description.SampleDesc.Count != 1 || view.Texture2DArray.MipSlice >= description.MipLevels) {
            ++unsupported; return false;
        }
        const float black[4]{};
        context->ClearUnorderedAccessViewFloat(output.Get(), black);
        ++cleared;
        return true;
    }
private:
    std::atomic<bool> enabled{};
    std::atomic<uint64_t> cleared{}, unsupported{};
};
