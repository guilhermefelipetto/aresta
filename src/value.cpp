#include "value.h"

#include "color.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

// Sete paradas de cada mapa. Interpolar entre elas erra pouco e evita carregar
// quatro tabelas de 256 linhas.
constexpr float colormaps[5][7][3] = {
    {{0, 0, 0}, {0.167f, 0.167f, 0.167f}, {0.333f, 0.333f, 0.333f}, {0.5f, 0.5f, 0.5f},
     {0.667f, 0.667f, 0.667f}, {0.833f, 0.833f, 0.833f}, {1, 1, 1}},
    {{0.267f, 0.005f, 0.329f}, {0.283f, 0.141f, 0.458f}, {0.254f, 0.265f, 0.530f},
     {0.164f, 0.471f, 0.558f}, {0.135f, 0.659f, 0.518f}, {0.478f, 0.821f, 0.318f},
     {0.993f, 0.906f, 0.144f}},
    {{0.001f, 0.000f, 0.014f}, {0.185f, 0.068f, 0.372f}, {0.451f, 0.122f, 0.506f},
     {0.716f, 0.215f, 0.475f}, {0.933f, 0.410f, 0.353f}, {0.993f, 0.681f, 0.381f},
     {0.987f, 0.991f, 0.750f}},
    {{0.190f, 0.072f, 0.232f}, {0.223f, 0.577f, 0.902f}, {0.196f, 0.900f, 0.616f},
     {0.720f, 0.980f, 0.222f}, {0.988f, 0.717f, 0.115f}, {0.913f, 0.309f, 0.043f},
     {0.479f, 0.012f, 0.011f}},
    {{0, 0, 0}, {0.4f, 0, 0}, {0.8f, 0, 0}, {1.0f, 0.2f, 0}, {1.0f, 0.6f, 0}, {1.0f, 0.95f, 0.2f},
     {1, 1, 1}},
};

// Cor por rótulo pela razão áurea: índices vizinhos caem longe no círculo de
// matiz, que é o que faz regiões coladas ficarem distinguíveis.
void label_color(int32_t label, unsigned char* rgb) {
    if (label <= 0) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    const float hue = std::fmod(static_cast<float>(label) * 0.61803399f, 1.0f) * 6.0f;
    const int sector = static_cast<int>(hue);
    const float f = hue - static_cast<float>(sector);
    const float v = 0.95f;
    const float p = 0.25f;
    const float q = v * (1.0f - 0.75f * f);
    const float t = v * (1.0f - 0.75f * (1.0f - f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (sector) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    rgb[0] = static_cast<unsigned char>(r * 255.0f);
    rgb[1] = static_cast<unsigned char>(g * 255.0f);
    rgb[2] = static_cast<unsigned char>(b * 255.0f);
}

}  // namespace

void colormap_at(Colormap map, float t, float* rgb) {
    const auto& stops = colormaps[static_cast<int>(map)];
    t = std::clamp(t, 0.0f, 1.0f) * 6.0f;
    const int i = std::min(static_cast<int>(t), 5);
    const float f = t - static_cast<float>(i);
    for (int c = 0; c < 3; ++c) {
        rgb[c] = stops[i][c] + (stops[i + 1][c] - stops[i][c]) * f;
    }
}

const char* colormap_name(Colormap map) {
    switch (map) {
        case Colormap::Gray: return "cinza";
        case Colormap::Viridis: return "viridis";
        case Colormap::Magma: return "magma";
        case Colormap::Turbo: return "turbo";
        case Colormap::Hot: return "quente";
    }
    return "?";
}

Image pseudo_color(MapView<float> scalar, Colormap map) {
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            lo = std::min(lo, row[x]);
            hi = std::max(hi, row[x]);
        }
    }
    const float span = (hi > lo) ? (hi - lo) : 1.0f;

    Image out(scalar.width, scalar.height);
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        float* p = out.view().row(y);
        for (int x = 0; x < scalar.width; ++x, p += 4) {
            float rgb[3];
            colormap_at(map, (row[x] - lo) / span, rgb);
            for (int c = 0; c < 3; ++c) {
                p[c] = srgb_to_linear(std::clamp(rgb[c], 0.0f, 1.0f));
            }
            p[3] = 1.0f;
        }
    }
    return out;
}

const char* kind_name(ValueKind kind) {
    switch (kind) {
        case ValueKind::Color: return "cor";
        case ValueKind::Scalar: return "escalar";
        case ValueKind::Label: return "rótulo";
    }
    return "?";
}

int Value::width() const {
    switch (kind) {
        case ValueKind::Color: return color.width;
        case ValueKind::Scalar: return scalar.width;
        case ValueKind::Label: return label.width;
    }
    return 0;
}

int Value::height() const {
    switch (kind) {
        case ValueKind::Color: return color.height;
        case ValueKind::Scalar: return scalar.height;
        case ValueKind::Label: return label.height;
    }
    return 0;
}

bool Value::empty() const { return width() == 0 || height() == 0; }

Value Value::clone() const {
    Value copy;
    copy.kind = kind;
    switch (kind) {
        case ValueKind::Color:
            copy.color = color.clone();
            break;
        case ValueKind::Scalar:
            if (!scalar.empty()) {
                copy.scalar = Map<float>(scalar.width, scalar.height);
                std::memcpy(copy.scalar.data.get(), scalar.data.get(),
                            scalar.count() * sizeof(float));
            }
            break;
        case ValueKind::Label:
            if (!label.empty()) {
                copy.label = Map<int32_t>(label.width, label.height);
                std::memcpy(copy.label.data.get(), label.data.get(),
                            label.count() * sizeof(int32_t));
            }
            break;
    }
    return copy;
}

Value make_color(Image image) {
    Value v;
    v.kind = ValueKind::Color;
    v.color = std::move(image);
    return v;
}

Value make_scalar(Map<float> map) {
    Value v;
    v.kind = ValueKind::Scalar;
    v.scalar = std::move(map);
    return v;
}

Value make_label(Map<int32_t> map) {
    Value v;
    v.kind = ValueKind::Label;
    v.label = std::move(map);
    return v;
}

std::unique_ptr<unsigned char[]> to_display_rgba8(const Value& value, Colormap colormap, float* lo,
                                                  float* hi) {
    *lo = 0.0f;
    *hi = 0.0f;
    if (value.empty()) {
        return nullptr;
    }

    if (value.kind == ValueKind::Color) {
        return to_srgb8(value.color);
    }

    const std::size_t total =
        static_cast<std::size_t>(value.width()) * static_cast<std::size_t>(value.height());
    std::unique_ptr<unsigned char[]> out(new unsigned char[total * 4]);

    if (value.kind == ValueKind::Scalar) {
        const float* src = value.scalar.data.get();
        float min_value = std::numeric_limits<float>::max();
        float max_value = std::numeric_limits<float>::lowest();
        for (std::size_t i = 0; i < total; ++i) {
            min_value = std::min(min_value, src[i]);
            max_value = std::max(max_value, src[i]);
        }
        *lo = min_value;
        *hi = max_value;

        const float span = (max_value > min_value) ? (max_value - min_value) : 1.0f;
        for (std::size_t i = 0; i < total; ++i) {
            const float t = (src[i] - min_value) / span;
            unsigned char* dst = out.get() + i * 4;
            float rgb[3];
            colormap_at(colormap, t, rgb);
            for (int c = 0; c < 3; ++c) {
                dst[c] = static_cast<unsigned char>(std::clamp(rgb[c], 0.0f, 1.0f) * 255.0f);
            }
            dst[3] = 255;
        }
        return out;
    }

    const int32_t* src = value.label.data.get();
    int32_t min_label = 0;
    int32_t max_label = 0;
    for (std::size_t i = 0; i < total; ++i) {
        min_label = std::min(min_label, src[i]);
        max_label = std::max(max_label, src[i]);
    }
    *lo = static_cast<float>(min_label);
    *hi = static_cast<float>(max_label);

    for (std::size_t i = 0; i < total; ++i) {
        unsigned char* dst = out.get() + i * 4;
        label_color(src[i], dst);
        dst[3] = 255;
    }
    return out;
}
