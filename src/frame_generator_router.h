#pragma once

#include "nvidia_frame_generator.h"
#include "nvidia_vfg_frame_generator.h"
#include "amd_amf_frame_generator.h"

// La app solo conoce una puerta de Frame Generation. Detras puede vivir el
// interpolador OFA de Dayaah, NVIDIA VFG o AMD AMF FRC, pero nunca dos de ellos
// peleandose por las mismas texturas al mismo tiempo.
class FrameGeneratorRouter {
public:
    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                       UINT width, UINT height, FrameGenerationMode mode) {
        Shutdown();
        mode_ = mode;
        if (FrameGenerationIsAmdFrc(mode)) {
            return amd_.Initialize(device, context, width, height, mode);
        }
        if (FrameGenerationIsVfg(mode)) {
            return vfg_.Initialize(device, context, width, height, mode);
        }
        if (FrameGenerationIsOfa(mode)) {
            return ofa_.Initialize(device, context, width, height, mode);
        }
        return E_INVALIDARG;
    }

    void Shutdown() {
        ofa_.Shutdown();
        vfg_.Shutdown();
        amd_.Shutdown();
        mode_ = FrameGenerationMode::Off;
    }

    bool IsVfg() const { return FrameGenerationIsVfg(mode_); }
    bool IsAmdFrc() const { return FrameGenerationIsAmdFrc(mode_); }
    bool Active() const { return IsAmdFrc() ? amd_.Active() : (IsVfg() ? vfg_.Active() : ofa_.Active()); }
    bool HasCurrent() const { return IsAmdFrc() ? amd_.HasCurrent() : (IsVfg() ? vfg_.HasCurrent() : ofa_.HasCurrent()); }
    int GridSize() const { return IsAmdFrc() ? amd_.GridSize() : (IsVfg() ? vfg_.GridSize() : ofa_.GridSize()); }
    bool UsesCostMap() const { return IsAmdFrc() ? amd_.UsesCostMap() : (IsVfg() ? vfg_.UsesCostMap() : ofa_.UsesCostMap()); }
    int LineCleanerLevel() const {
        return IsAmdFrc() ? amd_.LineCleanerLevel() :
               (IsVfg() ? vfg_.LineCleanerLevel() : ofa_.LineCleanerLevel());
    }
    int BypassMode() const { return IsAmdFrc() ? amd_.BypassMode() : (IsVfg() ? vfg_.BypassMode() : ofa_.BypassMode()); }
    int SourceTextureCount() const {
        return IsAmdFrc() ? amd_.SourceTextureCount() :
               (IsVfg() ? vfg_.SourceTextureCount() : ofa_.SourceTextureCount());
    }
    double LastFlowMs() const { return IsAmdFrc() ? amd_.LastFlowMs() : (IsVfg() ? vfg_.LastFlowMs() : ofa_.LastFlowMs()); }
    double GpuOpticalMs() const {
        return IsAmdFrc() ? amd_.GpuOpticalMs() :
               (IsVfg() ? vfg_.GpuOpticalMs() : ofa_.GpuOpticalMs());
    }
    double GpuWarpMs() const { return IsAmdFrc() ? amd_.GpuWarpMs() : (IsVfg() ? vfg_.GpuWarpMs() : ofa_.GpuWarpMs()); }
    double SyncVideoToOfaMs() const {
        return IsAmdFrc() ? amd_.SyncVideoToOfaMs() :
               (IsVfg() ? vfg_.SyncVideoToOfaMs() : ofa_.SyncVideoToOfaMs());
    }
    double SyncOfaToShaderMs() const {
        return IsAmdFrc() ? amd_.SyncOfaToShaderMs() :
               (IsVfg() ? vfg_.SyncOfaToShaderMs() : ofa_.SyncOfaToShaderMs());
    }
    double SyncBeforeReuseMs() const {
        return IsAmdFrc() ? amd_.SyncBeforeReuseMs() :
               (IsVfg() ? vfg_.SyncBeforeReuseMs() : ofa_.SyncBeforeReuseMs());
    }
    const FrameGenWarpStats& WarpStats() const {
        return IsAmdFrc() ? amd_.WarpStats() : (IsVfg() ? vfg_.WarpStats() : ofa_.WarpStats());
    }
    const std::wstring& Error() const { return IsAmdFrc() ? amd_.Error() : (IsVfg() ? vfg_.Error() : ofa_.Error()); }
    const std::wstring& SurfaceSummary() const {
        return IsAmdFrc() ? amd_.SurfaceSummary() :
               (IsVfg() ? vfg_.SurfaceSummary() : ofa_.SurfaceSummary());
    }
    const std::wstring& FlowDiagnosticLine() const {
        return IsAmdFrc() ? amd_.FlowDiagnosticLine() :
               (IsVfg() ? vfg_.FlowDiagnosticLine() : ofa_.FlowDiagnosticLine());
    }
    DXGI_FORMAT InputSurfaceFormat() const {
        return IsAmdFrc() ? amd_.InputSurfaceFormat() :
               (IsVfg() ? vfg_.InputSurfaceFormat() : ofa_.InputSurfaceFormat());
    }
    FrameGenerationMode Mode() const { return mode_; }
    ID3D11Texture2D* PendingTexture() const {
        return IsAmdFrc() ? amd_.PendingTexture() :
               (IsVfg() ? vfg_.PendingTexture() : ofa_.PendingTexture());
    }
    ID3D11Texture2D* SourceTexture(int index) const {
        return IsAmdFrc() ? amd_.SourceTexture(index) :
               (IsVfg() ? vfg_.SourceTexture(index) : ofa_.SourceTexture(index));
    }
    int PendingIndex() const { return IsAmdFrc() ? amd_.PendingIndex() : (IsVfg() ? vfg_.PendingIndex() : ofa_.PendingIndex()); }
    HRESULT PrepareForConvertedFrame() {
        return IsAmdFrc() ? amd_.PrepareForConvertedFrame() :
               (IsVfg() ? vfg_.PrepareForConvertedFrame() : ofa_.PrepareForConvertedFrame());
    }
    HRESULT CommitConvertedFrame() {
        return IsAmdFrc() ? amd_.CommitConvertedFrame() :
               (IsVfg() ? vfg_.CommitConvertedFrame() : ofa_.CommitConvertedFrame());
    }
    HRESULT SetOutputTarget(IDXGISwapChain1* swapChain) {
        return IsAmdFrc() ? amd_.SetOutputTarget(swapChain) :
               (IsVfg() ? vfg_.SetOutputTarget(swapChain) : ofa_.SetOutputTarget(swapChain));
    }
    void BeforeResize() {
        if (IsAmdFrc()) amd_.BeforeResize();
        else if (IsVfg()) vfg_.BeforeResize();
        else ofa_.BeforeResize();
    }
    HRESULT DrawCurrent(const RECT& contentRect) {
        return IsAmdFrc() ? amd_.DrawCurrent(contentRect) :
               (IsVfg() ? vfg_.DrawCurrent(contentRect) : ofa_.DrawCurrent(contentRect));
    }
    HRESULT DrawGenerated(const RECT& contentRect) {
        return IsAmdFrc() ? amd_.DrawGenerated(contentRect) :
               (IsVfg() ? vfg_.DrawGenerated(contentRect) : ofa_.DrawGenerated(contentRect));
    }

private:
    FrameGenerationMode mode_ = FrameGenerationMode::Off;
    NvidiaFrameGenerator ofa_;
    NvidiaVfgFrameGenerator vfg_;
    AmdAmfFrameGenerator amd_;
};
