#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

struct UIImageHandle
{
    uint32_t index = UINT32_MAX;
    bool IsValid() const { return index != UINT32_MAX; }
};

struct UIRect { float x, y, width, height; };

// Managerが所有。画像はキャッシュし、描画指定だけを毎フレーム破棄する。
class UIManager
{
public:
    static constexpr uint32_t MaxImages = 256;
    static constexpr uint32_t MaxQuads = 4096;
    UIImageHandle LoadImageFile(const std::filesystem::path& path);
    void BeginFrame() { m_commands.clear(); }

    // 左上原点、右が+X、下が+Yのピクセル座標。後の呼び出しが手前。
    bool DrawImage(UIImageHandle image, UIRect rect,
        DirectX::XMFLOAT4 tint = { 1, 1, 1, 1 });
    // 凸四角形の外周順（例: 左上、右上、右下、左下）。色はRGBA、0～1。
    bool DrawQuad(const std::array<DirectX::XMFLOAT2, 4>& points,
        DirectX::XMFLOAT4 color);
    bool DrawGradientQuad(const std::array<DirectX::XMFLOAT2, 4>& points,
        const std::array<DirectX::XMFLOAT4, 4>& colors);

    struct Image
    {
        std::filesystem::path path;
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> pixels;
    };
    struct Command
    {
        std::array<DirectX::XMFLOAT2, 4> points;
        DirectX::XMFLOAT4 color;
        std::array<DirectX::XMFLOAT4, 4> colors;
        UIImageHandle image; // 無効なら単色。
    };
    const Image* GetImage(UIImageHandle handle) const;
    const std::vector<Image>& GetImages() const { return m_images; }
    const std::vector<Command>& GetCommands() const { return m_commands; }

private:
    std::vector<Image> m_images;
    std::vector<Command> m_commands;
};
