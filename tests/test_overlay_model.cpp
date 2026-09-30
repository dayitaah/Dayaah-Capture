#include "../src/overlay_model.h"

#include <cassert>
#include <cmath>
#include <iostream>

static bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

int main() {
    OverlayItem item = MakeDefaultOverlayItem(OverlayItemType::Text, 1);
    item.width = 0.2f;
    item.height = 0.1f;

    item.anchor = OverlayAnchor::TopRight;
    item.x = 0.05f;
    item.y = 0.03f;
    OverlayRectF rect = ResolveOverlayRect(item);
    assert(Near(rect.left, 0.75f));
    assert(Near(rect.top, 0.03f));

    SetOverlayTopLeft(item, 0.60f, 0.20f);
    rect = ResolveOverlayRect(item);
    assert(Near(rect.left, 0.60f));
    assert(Near(rect.top, 0.20f));

    item.anchor = OverlayAnchor::Center;
    SetOverlayTopLeft(item, 0.40f, 0.45f);
    rect = ResolveOverlayRect(item);
    assert(Near(rect.left, 0.40f));
    assert(Near(rect.top, 0.45f));

    item.width = 4.0f;
    item.height = -1.0f;
    item.opacity = 2.0f;
    ClampOverlayItem(item);
    assert(Near(item.width, 1.0f));
    assert(Near(item.height, 0.03f));
    assert(Near(item.opacity, 1.0f));

    std::cout << "Overlay anchors, positioning and clamps: PASS\n";
}
