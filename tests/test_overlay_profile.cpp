#include "../src/overlay_profile.h"

#include <cassert>
#include <iostream>

int main() {
    OverlayConfig original{};
    original.enabled = false;
    for (int type = 0; type < 4; ++type) {
        OverlayItem item = MakeDefaultOverlayItem(static_cast<OverlayItemType>(type), type + 1);
        item.name = L"Reloj de Dayaah";
        item.text = L"México {\"texto\": \"hola\"}\nsegunda línea\t\\ruta 🚀";
        item.imagePath = L"C:\\Imágenes\\logo.png";
        item.opacity = 0.37f;
        item.fontSize = 53.0f;
        item.fontFamily = L"Consolas";
        item.argb = 0xff1a2b3cu;
        item.anchor = OverlayAnchor::BottomRight;
        item.alignment = OverlayAlignment::Right;
        item.clock24h = false;
        item.visible = type != 3;
        item.x = 0.05f;
        item.y = 0.06f;
        original.items.push_back(item);
    }
    const std::string serialized = overlay_profile::Serialize(original);
    OverlayConfig reloaded{};
    std::wstring error;
    if (!overlay_profile::Parse(serialized, reloaded, error)) {
        std::cerr << serialized;
        std::wcerr << error << L'\n';
        return 1;
    }
    assert(reloaded.items.size() == 4 && !reloaded.enabled);
    assert(overlay_profile::Serialize(reloaded) == serialized);
    assert(reloaded.items[0].text == original.items[0].text);
    assert(reloaded.items[2].imagePath == original.items[2].imagePath);

    for (size_t bytes : {size_t(0), serialized.size() / 2, serialized.size() - 2}) {
        assert(!overlay_profile::Parse(serialized.substr(0, bytes), reloaded, error));
        assert(overlay_profile::Serialize(reloaded) == serialized);
    }
    const auto rejects = [&](const std::string& replacement) {
        std::string bad = serialized;
        const size_t position = bad.find("0.37000");
        assert(position != std::string::npos);
        bad.replace(position, 7, replacement);
        assert(!overlay_profile::Parse(bad, reloaded, error));
        assert(overlay_profile::Serialize(reloaded) == serialized);
    };
    rejects("nan");
    rejects("1e9999");

    struct DecimalComma : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
    };
    const std::locale previous = std::locale();
    std::locale::global(std::locale(previous, new DecimalComma));
    assert(overlay_profile::Serialize(original) == serialized);
    assert(overlay_profile::Parse(serialized, reloaded, error));
    assert(overlay_profile::Serialize(reloaded) == serialized);
    std::locale::global(previous);

    size_t position = 0;
    std::string unicode;
    assert(overlay_profile::ReadStringToken("\"\\u00e9 \\ud83d\\ude80\"", position, unicode));
    assert(overlay_profile::FromUtf8(unicode) == L"é 🚀");
    position = 0;
    assert(!overlay_profile::ReadStringToken("\"\\uXXXX\"", position, unicode));

    OverlayConfig empty{};
    assert(overlay_profile::Parse(overlay_profile::Serialize(empty), reloaded, error));
    assert(reloaded.items.empty());
    std::cout << "Overlay JSON: settings/Unicode round-trip, locale, truncated profiles: PASS\n";
}
