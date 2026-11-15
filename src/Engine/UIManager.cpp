//-------------------------------------------------
//
//          UIManager.cpp
//
//  UI 画像の読み込みと一フレーム分の描画命令を管理する。
//-------------------------------------------------

#include "UIManager.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// 同じパスの画像は共有し、描画命令には軽量なハンドルだけを渡す。
UIImageHandle UIManager::LoadImageFile(const std::filesystem::path& path)
{
    std::error_code error;
    const auto key = std::filesystem::weakly_canonical(path, error);
    if (error) return {};
    for (uint32_t i = 0; i < m_images.size(); ++i)
        if (m_images[i].path == key) return { i };
    if (m_images.size() >= MaxImages) return {};

    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return {};
    struct ComScope
    {
        bool initialized;
        ~ComScope() { if (initialized) CoUninitialize(); }
    } scope { SUCCEEDED(init) };
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(key.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return {};
    Image image;
    image.path = key;
    if (FAILED(converter->GetSize(&image.width, &image.height)) ||
        !image.width || !image.height || image.width > 16384 || image.height > 16384)
        return {};
    image.pixels.resize(size_t(image.width) * image.height * 4);
    if (FAILED(converter->CopyPixels(nullptr, image.width * 4,
        static_cast<UINT>(image.pixels.size()), image.pixels.data()))) return {};
    m_images.push_back(std::move(image));
    return { static_cast<uint32_t>(m_images.size() - 1) };
}

const UIManager::Image* UIManager::GetImage(UIImageHandle handle) const
{
    return handle.index < m_images.size() ? &m_images[handle.index] : nullptr;
}

bool UIManager::DrawImage(UIImageHandle image, UIRect rect, DirectX::XMFLOAT4 tint)
{
    if (!GetImage(image) || !(rect.width > 0) || !(rect.height > 0)) return false;
    if (!DrawQuad({ DirectX::XMFLOAT2 { rect.x, rect.y },
        { rect.x + rect.width, rect.y }, { rect.x + rect.width, rect.y + rect.height },
        { rect.x, rect.y + rect.height } }, tint)) return false;
    m_commands.back().image = image;
    return true;
}

bool UIManager::DrawQuad(const std::array<DirectX::XMFLOAT2, 4>& points,
    DirectX::XMFLOAT4 color)
{
    return DrawGradientQuad(points, { color, color, color, color });
}

// 頂点ごとの色を指定し、ゲージやフェード用の四角形をキューへ積む。
bool UIManager::DrawGradientQuad(
    const std::array<DirectX::XMFLOAT2, 4>& points,
    const std::array<DirectX::XMFLOAT4, 4>& colors)
{
    if (m_commands.size() >= MaxQuads) return false;
    for (const auto& p : points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    m_commands.push_back({ points, colors[0], colors, {} });
    return true;
}
