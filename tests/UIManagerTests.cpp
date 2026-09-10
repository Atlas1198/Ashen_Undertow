#include "UIManager.h"
#include <cassert>

int main()
{
    UIManager ui;
    assert(!ui.LoadImageFile("tests/fixtures/missing-ui.jpg").IsValid());
    const auto image = ui.LoadImageFile("assets/texture/wonhee.jpg");
    assert(image.IsValid());
    assert(ui.LoadImageFile("assets/texture/../texture/wonhee.jpg").index == image.index);
    assert(ui.GetImages().size() == 1);
    const auto* decoded = ui.GetImage(image);
    assert(decoded && decoded->width && decoded->height);
    assert(decoded->pixels.size() == size_t(decoded->width) * decoded->height * 4);
    assert(!ui.DrawImage({}, { 0, 0, 100, 100 }));
    assert(!ui.DrawImage(image, { 0, 0, -1, 100 }));
    assert(ui.DrawImage(image, { 24, 32, 200, 100 }));
    assert(ui.DrawQuad({ DirectX::XMFLOAT2 {0,0}, {100,0}, {100,100}, {0,100} }, {1,0,0,0.5f}));
    assert(ui.GetCommands().size() == 2);
    assert(ui.GetCommands()[0].image.index == image.index);
    assert(ui.GetCommands()[0].points[2].x == 224);
    assert(ui.GetCommands()[0].points[2].y == 132);
    assert(!ui.GetCommands()[1].image.IsValid());
    assert(ui.GetCommands()[1].color.w == 0.5f);
    ui.BeginFrame();
    assert(ui.GetCommands().empty());
    assert(ui.GetImage(image)); // 画像はフレームを越えて保持。
    for (uint32_t i = 0; i < UIManager::MaxQuads; ++i)
        assert(ui.DrawImage(image, {0,0,1,1}));
    assert(!ui.DrawImage(image, {0,0,1,1}));
}
