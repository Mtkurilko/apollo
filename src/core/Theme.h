// Colors. No rendering library in here, keeps core portable.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apollo {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;

    std::string hex() const;
    // Takes #rgb, #rrggbb, rgb(r,g,b) or one of the 16 ANSI color names.
    static std::optional<Rgb> parse(const std::string& text);
    Rgb mix(const Rgb& other, float t) const;

    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

struct Theme {
    std::string name = "apollo";

    Rgb bg{18, 20, 28};
    Rgb surface{24, 27, 38};
    Rgb fg{192, 202, 245};
    Rgb muted{86, 95, 137};
    Rgb border{41, 46, 66};
    Rgb accent{122, 162, 247};
    Rgb accentAlt{187, 154, 247};
    Rgb success{158, 206, 106};
    Rgb warning{224, 175, 104};
    Rgb error{247, 118, 142};
    Rgb selection{40, 52, 87};

    std::array<Rgb, 16> ansi{};

    static std::optional<Theme> builtin(const std::string& name);
    static std::vector<std::string> builtinNames();

    // Applies `key = #rrggbb` pairs from a decoration block or theme file.
    bool setColor(const std::string& key, const std::string& value);
    static std::vector<std::string> colorKeys();

    // Rebuilds the 16 ANSI colors off the theme's own palette.
    void rebuildRamp();
};

} // namespace apollo
