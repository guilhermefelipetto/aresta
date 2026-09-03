#include "spaces.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "color.h"

namespace {

void rgb_to_hsv(const float c[3], float out[3]) {
    const float hi = std::max({c[0], c[1], c[2]});
    const float lo = std::min({c[0], c[1], c[2]});
    const float span = hi - lo;

    float h = 0.0f;
    if (span > 1e-6f) {
        if (hi == c[0]) {
            h = std::fmod((c[1] - c[2]) / span, 6.0f);
        } else if (hi == c[1]) {
            h = (c[2] - c[0]) / span + 2.0f;
        } else {
            h = (c[0] - c[1]) / span + 4.0f;
        }
        h /= 6.0f;
        if (h < 0.0f) {
            h += 1.0f;
        }
    }
    out[0] = h;
    out[1] = hi > 1e-6f ? span / hi : 0.0f;
    out[2] = hi;
}

void hsv_to_rgb(const float in[3], float c[3]) {
    const float h = std::fmod(std::fmod(in[0], 1.0f) + 1.0f, 1.0f) * 6.0f;
    const float s = std::clamp(in[1], 0.0f, 1.0f);
    const float v = in[2];
    const int sector = static_cast<int>(h);
    const float f = h - static_cast<float>(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (sector % 6) {
        case 0: c[0] = v; c[1] = t; c[2] = p; break;
        case 1: c[0] = q; c[1] = v; c[2] = p; break;
        case 2: c[0] = p; c[1] = v; c[2] = t; break;
        case 3: c[0] = p; c[1] = q; c[2] = v; break;
        case 4: c[0] = t; c[1] = p; c[2] = v; break;
        default: c[0] = v; c[1] = p; c[2] = q; break;
    }
}

// O HSI do Gonzalez: a intensidade é a média dos três, não o máximo, e o
// matiz vem do ângulo entre o pixel e o eixo cinza.
void rgb_to_hsi(const float c[3], float out[3]) {
    const float soma = c[0] + c[1] + c[2];
    const float lo = std::min({c[0], c[1], c[2]});
    const float numerador = 0.5f * ((c[0] - c[1]) + (c[0] - c[2]));
    const float denominador =
        std::sqrt((c[0] - c[1]) * (c[0] - c[1]) + (c[0] - c[2]) * (c[1] - c[2]));

    float h = 0.0f;
    if (denominador > 1e-6f) {
        h = std::acos(std::clamp(numerador / denominador, -1.0f, 1.0f));
        if (c[2] > c[1]) {
            h = 2.0f * std::numbers::pi_v<float> - h;
        }
        h /= 2.0f * std::numbers::pi_v<float>;
    }
    out[0] = h;
    out[1] = soma > 1e-6f ? 1.0f - 3.0f * lo / soma : 0.0f;
    out[2] = soma / 3.0f;
}

void hsi_to_rgb(const float in[3], float c[3]) {
    const float h = std::fmod(std::fmod(in[0], 1.0f) + 1.0f, 1.0f) * 2.0f *
                    std::numbers::pi_v<float>;
    const float s = std::clamp(in[1], 0.0f, 1.0f);
    const float i = in[2];
    const float dois_pi_terco = 2.0f * std::numbers::pi_v<float> / 3.0f;

    const auto ramo = [&](float angulo) {
        return i * (1.0f + s * std::cos(angulo) / std::max(std::cos(dois_pi_terco / 2.0f - angulo),
                                                           1e-6f));
    };

    if (h < dois_pi_terco) {
        c[2] = i * (1.0f - s);
        c[0] = ramo(h);
        c[1] = 3.0f * i - (c[0] + c[2]);
    } else if (h < 2.0f * dois_pi_terco) {
        c[0] = i * (1.0f - s);
        c[1] = ramo(h - dois_pi_terco);
        c[2] = 3.0f * i - (c[0] + c[1]);
    } else {
        c[1] = i * (1.0f - s);
        c[2] = ramo(h - 2.0f * dois_pi_terco);
        c[0] = 3.0f * i - (c[1] + c[2]);
    }
}

// D65, que é o branco do sRGB.
constexpr float white_x = 0.95047f;
constexpr float white_y = 1.0f;
constexpr float white_z = 1.08883f;

float lab_f(float t) {
    constexpr float delta = 6.0f / 29.0f;
    return t > delta * delta * delta ? std::cbrt(t)
                                     : t / (3.0f * delta * delta) + 4.0f / 29.0f;
}

float lab_f_inverse(float t) {
    constexpr float delta = 6.0f / 29.0f;
    return t > delta ? t * t * t : 3.0f * delta * delta * (t - 4.0f / 29.0f);
}

void linear_to_lab(const float c[3], float out[3]) {
    const float x = 0.4124564f * c[0] + 0.3575761f * c[1] + 0.1804375f * c[2];
    const float y = 0.2126729f * c[0] + 0.7151522f * c[1] + 0.0721750f * c[2];
    const float z = 0.0193339f * c[0] + 0.1191920f * c[1] + 0.9503041f * c[2];

    const float fx = lab_f(x / white_x);
    const float fy = lab_f(y / white_y);
    const float fz = lab_f(z / white_z);

    out[0] = 116.0f * fy - 16.0f;
    out[1] = 500.0f * (fx - fy);
    out[2] = 200.0f * (fy - fz);
}

void lab_to_linear(const float in[3], float c[3]) {
    const float fy = (in[0] + 16.0f) / 116.0f;
    const float fx = fy + in[1] / 500.0f;
    const float fz = fy - in[2] / 200.0f;

    const float x = white_x * lab_f_inverse(fx);
    const float y = white_y * lab_f_inverse(fy);
    const float z = white_z * lab_f_inverse(fz);

    c[0] = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    c[1] = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    c[2] = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
}

}  // namespace

const char* space_name(Space space) {
    switch (space) {
        case Space::RGB: return "RGB";
        case Space::HSV: return "HSV";
        case Space::HSI: return "HSI";
        case Space::Lab: return "Lab";
        case Space::YCbCr: return "YCbCr";
        case Space::CMY: return "CMY";
    }
    return "?";
}

const char* component_name(Space space, int index) {
    index = std::clamp(index, 0, 2);
    switch (space) {
        case Space::RGB: return index == 0 ? "vermelho" : (index == 1 ? "verde" : "azul");
        case Space::HSV: return index == 0 ? "matiz" : (index == 1 ? "saturação" : "valor");
        case Space::HSI: return index == 0 ? "matiz" : (index == 1 ? "saturação" : "intensidade");
        case Space::Lab: return index == 0 ? "L" : (index == 1 ? "a" : "b");
        case Space::YCbCr: return index == 0 ? "Y" : (index == 1 ? "Cb" : "Cr");
        case Space::CMY: return index == 0 ? "ciano" : (index == 1 ? "magenta" : "amarelo");
    }
    return "?";
}

void component_range(Space space, int index, float* lo, float* hi) {
    *lo = 0.0f;
    *hi = 1.0f;
    if (space == Space::Lab) {
        if (index == 0) {
            *hi = 100.0f;
        } else {
            *lo = -128.0f;
            *hi = 127.0f;
        }
    } else if (space == Space::YCbCr && index > 0) {
        *lo = -0.5f;
        *hi = 0.5f;
    }
}

void to_space(Space space, const float rgb[3], float out[3]) {
    if (space == Space::Lab) {
        linear_to_lab(rgb, out);
        return;
    }

    // Todo o resto é definido sobre o valor codificado.
    float c[3];
    for (int i = 0; i < 3; ++i) {
        c[i] = linear_to_srgb(std::clamp(rgb[i], 0.0f, 1.0f));
    }

    switch (space) {
        case Space::RGB:
            out[0] = c[0];
            out[1] = c[1];
            out[2] = c[2];
            break;
        case Space::HSV:
            rgb_to_hsv(c, out);
            break;
        case Space::HSI:
            rgb_to_hsi(c, out);
            break;
        case Space::YCbCr:
            out[0] = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            out[1] = 0.5f * (c[2] - out[0]) / (1.0f - 0.114f);
            out[2] = 0.5f * (c[0] - out[0]) / (1.0f - 0.299f);
            break;
        default:
            out[0] = 1.0f - c[0];
            out[1] = 1.0f - c[1];
            out[2] = 1.0f - c[2];
            break;
    }
}

void from_space(Space space, const float in[3], float rgb[3]) {
    if (space == Space::Lab) {
        lab_to_linear(in, rgb);
        return;
    }

    float c[3];
    switch (space) {
        case Space::RGB:
            c[0] = in[0];
            c[1] = in[1];
            c[2] = in[2];
            break;
        case Space::HSV:
            hsv_to_rgb(in, c);
            break;
        case Space::HSI:
            hsi_to_rgb(in, c);
            break;
        case Space::YCbCr:
            c[0] = in[0] + 1.402f * in[2];
            c[1] = in[0] - 0.344136f * in[1] - 0.714136f * in[2];
            c[2] = in[0] + 1.772f * in[1];
            break;
        default:
            c[0] = 1.0f - in[0];
            c[1] = 1.0f - in[1];
            c[2] = 1.0f - in[2];
            break;
    }

    for (int i = 0; i < 3; ++i) {
        rgb[i] = srgb_to_linear(std::clamp(c[i], 0.0f, 1.0f));
    }
}
