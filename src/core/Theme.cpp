#include "core/Theme.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>

namespace apollo {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

const std::map<std::string, Rgb>& namedColors() {
    static const std::map<std::string, Rgb> table = {
        {"black", {0, 0, 0}},         {"red", {205, 49, 49}},
        {"green", {13, 188, 121}},    {"yellow", {229, 229, 16}},
        {"blue", {36, 114, 200}},     {"magenta", {188, 63, 188}},
        {"cyan", {17, 168, 205}},     {"white", {229, 229, 229}},
        {"gray", {128, 128, 128}},    {"grey", {128, 128, 128}},
        {"orange", {224, 175, 104}},  {"purple", {187, 154, 247}},
        {"pink", {247, 118, 142}},    {"teal", {42, 195, 222}},
    };
    return table;
}

} // namespace

// A sensible 16-colour ramp derived from a theme's own palette, so a theme only
// has to name the handful of colours that matter.
void Theme::rebuildRamp() {
    const Theme& t = *this;
    ansi = {{
        t.bg.mix({0, 0, 0}, 0.4f), t.error, t.success, t.warning,
        t.accent, t.accentAlt, {42, 195, 222}, t.fg,
        t.muted, t.error.mix({255, 255, 255}, 0.25f),
        t.success.mix({255, 255, 255}, 0.25f), t.warning.mix({255, 255, 255}, 0.25f),
        t.accent.mix({255, 255, 255}, 0.25f), t.accentAlt.mix({255, 255, 255}, 0.25f),
        Rgb{42, 195, 222}.mix({255, 255, 255}, 0.25f), {255, 255, 255},
    }};
}


std::string Rgb::hex() const {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", r, g, b);
    return buffer;
}

float Rgb::luma() const {
    return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
}

Rgb Rgb::mix(const Rgb& other, float t) const {
    const auto lerp = [t](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(a + (b - a) * t);
    };
    return {lerp(r, other.r), lerp(g, other.g), lerp(b, other.b)};
}

std::optional<Rgb> Rgb::parse(const std::string& text) {
    std::string value = text;
    value.erase(0, value.find_first_not_of(" \t"));
    if (const auto end = value.find_last_not_of(" \t"); end != std::string::npos) {
        value.erase(end + 1);
    }
    if (value.empty()) return std::nullopt;

    if (value[0] == '#') {
        const std::string digits = value.substr(1);
        if (digits.size() == 3) {
            int v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = hexDigit(digits[i]);
                if (v[i] < 0) return std::nullopt;
                v[i] = v[i] * 16 + v[i]; // #abc -> #aabbcc
            }
            return Rgb{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]),
                       static_cast<std::uint8_t>(v[2])};
        }
        if (digits.size() == 6) {
            int v[3];
            for (int i = 0; i < 3; ++i) {
                const int hi = hexDigit(digits[i * 2]);
                const int lo = hexDigit(digits[i * 2 + 1]);
                if (hi < 0 || lo < 0) return std::nullopt;
                v[i] = hi * 16 + lo;
            }
            return Rgb{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]),
                       static_cast<std::uint8_t>(v[2])};
        }
        return std::nullopt;
    }

    if (lower(value).rfind("rgb(", 0) == 0 && value.back() == ')') {
        std::istringstream parts(value.substr(4, value.size() - 5));
        int channel[3] = {-1, -1, -1};
        for (int i = 0; i < 3; ++i) {
            std::string field;
            if (!std::getline(parts, field, ',')) return std::nullopt;
            try { channel[i] = std::stoi(field); } catch (...) { return std::nullopt; }
            if (channel[i] < 0 || channel[i] > 255) return std::nullopt;
        }
        return Rgb{static_cast<std::uint8_t>(channel[0]),
                   static_cast<std::uint8_t>(channel[1]),
                   static_cast<std::uint8_t>(channel[2])};
    }

    const auto it = namedColors().find(lower(value));
    if (it != namedColors().end()) return it->second;
    return std::nullopt;
}

std::vector<std::string> Theme::colorKeys() {
    return {"bg", "surface", "fg", "muted", "border", "accent",
            "accent_alt", "success", "warning", "error", "selection"};
}

bool Theme::setColor(const std::string& key, const std::string& value) {
    const auto parsed = Rgb::parse(value);
    if (!parsed) return false;

    if (key == "bg") bg = *parsed;
    else if (key == "surface") surface = *parsed;
    else if (key == "fg") fg = *parsed;
    else if (key == "muted") muted = *parsed;
    else if (key == "border") border = *parsed;
    else if (key == "accent") accent = *parsed;
    else if (key == "accent_alt") accentAlt = *parsed;
    else if (key == "success") success = *parsed;
    else if (key == "warning") warning = *parsed;
    else if (key == "error") error = *parsed;
    else if (key == "selection") selection = *parsed;
    else return false;
    return true;
}

std::vector<std::string> Theme::builtinNames() {
    return {"apollo", "midnight", "nord", "gruvbox", "catppuccin", "solarized", "paper"};
}

std::optional<Theme> Theme::builtin(const std::string& name) {
    Theme t;
    const std::string key = lower(name);

    if (key == "apollo") {
        // The default: deep blue with a violet accent.
    } else if (key == "midnight") {
        t = Theme{};
        t.bg = {8, 9, 14};      t.surface = {14, 16, 24};  t.fg = {203, 211, 240};
        t.muted = {72, 80, 112}; t.border = {28, 32, 48};  t.accent = {96, 165, 250};
        t.accentAlt = {167, 139, 250}; t.selection = {30, 41, 70};
    } else if (key == "nord") {
        t = Theme{};
        t.bg = {46, 52, 64};    t.surface = {59, 66, 82};  t.fg = {216, 222, 233};
        t.muted = {106, 115, 137}; t.border = {67, 76, 94}; t.accent = {136, 192, 208};
        t.accentAlt = {180, 142, 173}; t.success = {163, 190, 140};
        t.warning = {235, 203, 139};   t.error = {191, 97, 106};
        t.selection = {67, 76, 94};
    } else if (key == "gruvbox") {
        t = Theme{};
        t.bg = {40, 40, 40};    t.surface = {50, 48, 47};  t.fg = {235, 219, 178};
        t.muted = {146, 131, 116}; t.border = {80, 73, 69}; t.accent = {131, 165, 152};
        t.accentAlt = {211, 134, 155}; t.success = {184, 187, 38};
        t.warning = {250, 189, 47};    t.error = {251, 73, 52};
        t.selection = {60, 56, 54};
    } else if (key == "catppuccin") {
        t = Theme{};
        t.bg = {30, 30, 46};    t.surface = {49, 50, 68};  t.fg = {205, 214, 244};
        t.muted = {127, 132, 156}; t.border = {69, 71, 90}; t.accent = {137, 180, 250};
        t.accentAlt = {203, 166, 247}; t.success = {166, 227, 161};
        t.warning = {249, 226, 175};   t.error = {243, 139, 168};
        t.selection = {69, 71, 90};
    } else if (key == "solarized") {
        t = Theme{};
        t.bg = {0, 43, 54};     t.surface = {7, 54, 66};   t.fg = {147, 161, 161};
        t.muted = {88, 110, 117}; t.border = {7, 54, 66};  t.accent = {38, 139, 210};
        t.accentAlt = {211, 54, 130}; t.success = {133, 153, 0};
        t.warning = {181, 137, 0};    t.error = {220, 50, 47};
        t.selection = {7, 54, 66};
    } else if (key == "paper") {
        // The one light theme, for people who work in daylight.
        t = Theme{};
        t.bg = {250, 250, 248};  t.surface = {240, 240, 236}; t.fg = {40, 42, 54};
        t.muted = {130, 134, 150}; t.border = {214, 214, 208}; t.accent = {38, 108, 200};
        t.accentAlt = {140, 80, 190}; t.success = {32, 132, 74};
        t.warning = {170, 110, 20};   t.error = {198, 40, 60};
        t.selection = {216, 228, 246};
    } else {
        return std::nullopt;
    }

    t.name = key;
    t.rebuildRamp();
    return t;
}

} // namespace apollo
