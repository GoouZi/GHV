#include "d3d11_renderer.h"

#ifdef _WIN32
#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ghvplayer {
namespace {
std::string hr_text(const char* operation, HRESULT hr) {
    char text[96]{};
    std::snprintf(text, sizeof(text), "%s failed (HRESULT 0x%08lx)", operation, static_cast<unsigned long>(hr));
    return text;
}
}

bool D3D11Renderer::initialize(HWND window, std::string& error) {
    window_ = window;
    RECT rect{};
    GetClientRect(window, &rect);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = std::max<LONG>(1, rect.right);
    desc.BufferDesc.Height = std::max<LONG>(1, rect.bottom);
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION, &desc, &swap_chain_, &device_, &level, &context_);
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION, &desc, &swap_chain_, &device_, &level, &context_);
    }
    if (FAILED(hr)) { error = hr_text("Create D3D11 device", hr); return false; }
    if (!create_target()) { error = "Cannot create D3D11 render target."; return false; }

    static const char* shader = R"(
Texture2D yTex : register(t0); Texture2D uTex : register(t1); Texture2D vTex : register(t2);
SamplerState linearSampler : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID) {
    float2 pos[4] = {float2(-1,1),float2(1,1),float2(-1,-1),float2(1,-1)};
    float2 uv[4] = {float2(0,0),float2(1,0),float2(0,1),float2(1,1)};
    VSOut o; o.pos=float4(pos[id],0,1); o.uv=uv[id]; return o;
}
float4 ps_main(VSOut i) : SV_TARGET {
    float y = 1.164383 * (yTex.Sample(linearSampler,i.uv).r - 0.0627451);
    float u = uTex.Sample(linearSampler,i.uv).r - 0.5;
    float v = vTex.Sample(linearSampler,i.uv).r - 0.5;
    return float4(saturate(float3(y + 1.596027*v, y - 0.391762*u - 0.812968*v, y + 2.017232*u)),1);
})";
    Microsoft::WRL::ComPtr<ID3DBlob> vs_blob, ps_blob, errors;
    hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "vs_main", "vs_4_0", 0, 0, &vs_blob, &errors);
    if (FAILED(hr)) { error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hr_text("Compile vertex shader", hr); return false; }
    hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "ps_main", "ps_4_0", 0, 0, &ps_blob, &errors);
    if (FAILED(hr)) { error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hr_text("Compile pixel shader", hr); return false; }
    if (FAILED(hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader_)) ||
        FAILED(hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader_))) {
        error = hr_text("Create video shaders", hr); return false;
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(hr = device_->CreateSamplerState(&sampler, &sampler_))) { error = hr_text("Create sampler", hr); return false; }
    return true;
}

bool D3D11Renderer::create_target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    return SUCCEEDED(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_));
}

void D3D11Renderer::resize(uint32_t width, uint32_t height) {
    if (!swap_chain_ || !width || !height) return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    if (SUCCEEDED(swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) create_target();
}

bool D3D11Renderer::create_plane(uint32_t width, uint32_t height,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &texture)) &&
           SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view));
}

bool D3D11Renderer::upload(const ghv::VideoFrame& frame, std::string& error) {
    if (frame.width != video_width_ || frame.height != video_height_) {
        y_texture_.Reset(); u_texture_.Reset(); v_texture_.Reset();
        y_view_.Reset(); u_view_.Reset(); v_view_.Reset();
        if (!create_plane(frame.width, frame.height, y_texture_, y_view_) ||
            !create_plane(frame.width / 2, frame.height / 2, u_texture_, u_view_) ||
            !create_plane(frame.width / 2, frame.height / 2, v_texture_, v_view_)) {
            error = "Cannot allocate D3D11 YUV textures."; return false;
        }
        video_width_ = frame.width; video_height_ = frame.height;
    }
    context_->UpdateSubresource(y_texture_.Get(), 0, nullptr, frame.y, frame.y_stride, 0);
    context_->UpdateSubresource(u_texture_.Get(), 0, nullptr, frame.u, frame.u_stride, 0);
    context_->UpdateSubresource(v_texture_.Get(), 0, nullptr, frame.v, frame.v_stride, 0);
    return true;
}

void D3D11Renderer::render(uint32_t client_width, uint32_t video_area_height) {
    if (!target_ || !client_width || !video_area_height) return;
    const float black[4] = {0.015f, 0.015f, 0.018f, 1.0f};
    context_->OMSetRenderTargets(1, target_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(target_.Get(), black);
    if (y_view_ && video_width_ && video_height_) {
        const float source_aspect = float(video_width_) / video_height_;
        const float area_aspect = float(client_width) / video_area_height;
        float width = float(client_width), height = float(video_area_height), x = 0, y = 0;
        if (area_aspect > source_aspect) { width = height * source_aspect; x = (client_width - width) * 0.5f; }
        else { height = width / source_aspect; y = (video_area_height - height) * 0.5f; }
        D3D11_VIEWPORT viewport{x, y, width, height, 0.0f, 1.0f};
        context_->RSSetViewports(1, &viewport);
        ID3D11ShaderResourceView* views[] = {y_view_.Get(), u_view_.Get(), v_view_.Get()};
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        context_->PSSetShaderResources(0, 3, views);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context_->Draw(4, 0);
        ID3D11ShaderResourceView* clear[] = {nullptr, nullptr, nullptr};
        context_->PSSetShaderResources(0, 3, clear);
    }
    swap_chain_->Present(1, 0);
}

} // namespace ghvplayer
#endif
