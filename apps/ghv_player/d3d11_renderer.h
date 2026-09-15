#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.h>

#include <ghv/ghv.h>

#include <cstdint>
#include <string>

namespace ghvplayer {

class D3D11Renderer {
public:
    bool initialize(HWND window, std::string& error);
    void resize(uint32_t width, uint32_t height);
    bool upload(const ghv::VideoFrame& frame, std::string& error);
    void render(uint32_t client_width, uint32_t video_height);

private:
    bool create_target();
    bool create_plane(uint32_t width, uint32_t height,
                      Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
                      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);

    HWND window_ = nullptr;
    uint32_t video_width_ = 0;
    uint32_t video_height_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> y_texture_, u_texture_, v_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> y_view_, u_view_, v_view_;
};

} // namespace ghvplayer
#endif
