#pragma once

#include <cmath>

// IEC 61966-2-1, com o trecho reto perto do zero. Alfa não passa por aqui:
// canal de opacidade nunca é codificado em gama.
inline float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Luminância relativa, Rec. 709. Só faz sentido sobre valores lineares.
inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}
