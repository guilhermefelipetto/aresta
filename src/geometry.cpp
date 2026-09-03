#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "parallel.h"

const char* interp_name(Interp interp) {
    switch (interp) {
        case Interp::Nearest: return "vizinho";
        case Interp::Bilinear: return "bilinear";
        case Interp::Bicubic: return "bicúbica";
    }
    return "?";
}

Affine affine_scale(float sx, float sy) {
    Affine a;
    a.m[0] = sx > 1e-6f ? 1.0f / sx : 1.0f;
    a.m[4] = sy > 1e-6f ? 1.0f / sy : 1.0f;
    return a;
}

Affine affine_rotation(float degrees, float cx, float cy, float dcx, float dcy) {
    // O ângulo entra negado porque a matriz mapeia destino pra origem, ou seja,
    // ela é a inversa da rotação que se quer ver.
    const float r = -degrees * std::numbers::pi_v<float> / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);

    Affine a;
    a.m[0] = c;
    a.m[1] = -s;
    a.m[3] = s;
    a.m[4] = c;
    a.m[2] = cx - (c * dcx - s * dcy);
    a.m[5] = cy - (s * dcx + c * dcy);
    return a;
}

namespace {

// Catmull-Rom, que é a bicúbica que passa pelos pontos amostrados.
float cubic(float t) {
    t = std::fabs(t);
    if (t <= 1.0f) {
        return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
    }
    if (t < 2.0f) {
        return -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f;
    }
    return 0.0f;
}

template <typename Fetch>
float sample(Fetch fetch, float x, float y, Interp interp) {
    switch (interp) {
        case Interp::Nearest:
            return fetch(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
        case Interp::Bilinear: {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const float fx = x - static_cast<float>(x0);
            const float fy = y - static_cast<float>(y0);
            const float a = fetch(x0, y0) * (1.0f - fx) + fetch(x0 + 1, y0) * fx;
            const float b = fetch(x0, y0 + 1) * (1.0f - fx) + fetch(x0 + 1, y0 + 1) * fx;
            return a * (1.0f - fy) + b * fy;
        }
        default: {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const float fx = x - static_cast<float>(x0);
            const float fy = y - static_cast<float>(y0);
            float total = 0.0f;
            for (int j = -1; j <= 2; ++j) {
                const float wy = cubic(static_cast<float>(j) - fy);
                if (wy == 0.0f) {
                    continue;
                }
                float linha = 0.0f;
                for (int i = -1; i <= 2; ++i) {
                    linha += fetch(x0 + i, y0 + j) * cubic(static_cast<float>(i) - fx);
                }
                total += linha * wy;
            }
            return total;
        }
    }
}

void apply(const Affine& a, float x, float y, float* sx, float* sy) {
    *sx = a.m[0] * x + a.m[1] * y + a.m[2];
    *sy = a.m[3] * x + a.m[4] * y + a.m[5];
}

}  // namespace

Image warp(ImageView src, int width, int height, const Affine& to_source, Interp interp) {
    Image out(std::max(width, 1), std::max(height, 1));
    const ImageView dst = out.view();

    parallel_for(0, dst.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < dst.width; ++x) {
                float sx = 0.0f;
                float sy = 0.0f;
                apply(to_source, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, &sx,
                      &sy);
                sx -= 0.5f;
                sy -= 0.5f;

                for (int c = 0; c < 4; ++c) {
                    const auto fetch = [&](int fx, int fy) {
                        return src.at(std::clamp(fx, 0, src.width - 1),
                                      std::clamp(fy, 0, src.height - 1))[c];
                    };
                    row[x * 4 + c] = sample(fetch, sx, sy, interp);
                }
            }
        }
    });
    return out;
}

Map<float> warp(MapView<float> src, int width, int height, const Affine& to_source,
                Interp interp) {
    Map<float> out(std::max(width, 1), std::max(height, 1));
    const MapView<float> dst = out.view();

    parallel_for(0, dst.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < dst.width; ++x) {
                float sx = 0.0f;
                float sy = 0.0f;
                apply(to_source, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, &sx,
                      &sy);
                const auto fetch = [&](int fx, int fy) {
                    return src.at(std::clamp(fx, 0, src.width - 1),
                                  std::clamp(fy, 0, src.height - 1));
                };
                row[x] = sample(fetch, sx - 0.5f, sy - 0.5f, interp);
            }
        }
    });
    return out;
}

Map<int32_t> warp(MapView<int32_t> src, int width, int height, const Affine& to_source) {
    Map<int32_t> out(std::max(width, 1), std::max(height, 1));
    const MapView<int32_t> dst = out.view();

    for (int y = 0; y < dst.height; ++y) {
        int32_t* row = dst.row(y);
        for (int x = 0; x < dst.width; ++x) {
            float sx = 0.0f;
            float sy = 0.0f;
            apply(to_source, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, &sx, &sy);
            row[x] = src.at(std::clamp(static_cast<int>(sx), 0, src.width - 1),
                            std::clamp(static_cast<int>(sy), 0, src.height - 1));
        }
    }
    return out;
}

namespace {

template <typename View, typename Out>
Out crop_impl(View src, int x, int y, int width, int height) {
    width = std::clamp(width, 1, src.width);
    height = std::clamp(height, 1, src.height);
    x = std::clamp(x, 0, src.width - width);
    y = std::clamp(y, 0, src.height - height);
    return Out(width, height);
}

}  // namespace

Image crop(ImageView src, int x, int y, int width, int height) {
    Image out = crop_impl<ImageView, Image>(src, x, y, width, height);
    x = std::clamp(x, 0, src.width - out.width);
    y = std::clamp(y, 0, src.height - out.height);
    for (int j = 0; j < out.height; ++j) {
        const float* from = src.at(x, y + j);
        float* to = out.view().row(j);
        std::copy(from, from + static_cast<std::size_t>(out.width) * 4, to);
    }
    return out;
}

Map<float> crop(MapView<float> src, int x, int y, int width, int height) {
    Map<float> out = crop_impl<MapView<float>, Map<float>>(src, x, y, width, height);
    x = std::clamp(x, 0, src.width - out.width);
    y = std::clamp(y, 0, src.height - out.height);
    for (int j = 0; j < out.height; ++j) {
        for (int i = 0; i < out.width; ++i) {
            out.view().at(i, j) = src.at(x + i, y + j);
        }
    }
    return out;
}

Map<int32_t> crop(MapView<int32_t> src, int x, int y, int width, int height) {
    Map<int32_t> out = crop_impl<MapView<int32_t>, Map<int32_t>>(src, x, y, width, height);
    x = std::clamp(x, 0, src.width - out.width);
    y = std::clamp(y, 0, src.height - out.height);
    for (int j = 0; j < out.height; ++j) {
        for (int i = 0; i < out.width; ++i) {
            out.view().at(i, j) = src.at(x + i, y + j);
        }
    }
    return out;
}

namespace {

void flip_coords(bool horizontal, bool vertical, bool transpose, int w, int h, int x, int y,
                 int* sx, int* sy) {
    int fx = transpose ? y : x;
    int fy = transpose ? x : y;
    if (horizontal) {
        fx = w - 1 - fx;
    }
    if (vertical) {
        fy = h - 1 - fy;
    }
    *sx = fx;
    *sy = fy;
}

}  // namespace

Image flip(ImageView src, bool horizontal, bool vertical, bool transpose) {
    Image out(transpose ? src.height : src.width, transpose ? src.width : src.height);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = 0;
            int sy = 0;
            flip_coords(horizontal, vertical, transpose, src.width, src.height, x, y, &sx, &sy);
            const float* from = src.at(std::clamp(sx, 0, src.width - 1),
                                       std::clamp(sy, 0, src.height - 1));
            std::copy(from, from + 4, out.view().at(x, y));
        }
    }
    return out;
}

Map<float> flip(MapView<float> src, bool horizontal, bool vertical, bool transpose) {
    Map<float> out(transpose ? src.height : src.width, transpose ? src.width : src.height);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = 0;
            int sy = 0;
            flip_coords(horizontal, vertical, transpose, src.width, src.height, x, y, &sx, &sy);
            out.view().at(x, y) = src.at(std::clamp(sx, 0, src.width - 1),
                                         std::clamp(sy, 0, src.height - 1));
        }
    }
    return out;
}

Map<int32_t> flip(MapView<int32_t> src, bool horizontal, bool vertical, bool transpose) {
    Map<int32_t> out(transpose ? src.height : src.width, transpose ? src.width : src.height);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = 0;
            int sy = 0;
            flip_coords(horizontal, vertical, transpose, src.width, src.height, x, y, &sx, &sy);
            out.view().at(x, y) = src.at(std::clamp(sx, 0, src.width - 1),
                                         std::clamp(sy, 0, src.height - 1));
        }
    }
    return out;
}

namespace {

float quantize_one(float v, float lo, float span, int levels) {
    const int degrau = std::clamp(static_cast<int>((v - lo) / span * levels), 0, levels - 1);
    return lo + (static_cast<float>(degrau) + 0.5f) / levels * span;
}

}  // namespace

void quantize(ImageView image, int levels) {
    levels = std::max(levels, 2);
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            for (int c = 0; c < 3; ++c) {
                p[c] = quantize_one(std::clamp(p[c], 0.0f, 1.0f), 0.0f, 1.0f, levels);
            }
        }
    }
}

void quantize(MapView<float> scalar, int levels) {
    levels = std::max(levels, 2);
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
    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            row[x] = quantize_one(row[x], lo, span, levels);
        }
    }
}
