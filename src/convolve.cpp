#include "convolve.h"

#include <algorithm>

#include "parallel.h"

namespace {

// Devolve -1 quando a borda é Zero e o índice caiu fora, que é o sinal de "não
// soma nada".
inline int fold(int i, int n, Border border) {
    if (i >= 0 && i < n) {
        return i;
    }
    switch (border) {
        case Border::Zero:
            return -1;
        case Border::Clamp:
            return std::clamp(i, 0, n - 1);
        case Border::Mirror: {
            if (n == 1) {
                return 0;
            }
            const int period = 2 * n - 2;
            const int m = ((i % period) + period) % period;
            return m < n ? m : period - m;
        }
        case Border::Wrap:
            return ((i % n) + n) % n;
    }
    return 0;
}

}  // namespace

const char* border_name(Border border) {
    switch (border) {
        case Border::Zero: return "zero";
        case Border::Clamp: return "estender";
        case Border::Mirror: return "espelhar";
        case Border::Wrap: return "circular";
    }
    return "?";
}

Image convolve(ImageView src, const Kernel& kernel, Border border, bool flip) {
    Image out(src.width, src.height);
    const ImageView dst = out.view();
    const int ax = kernel.anchor_x();
    const int ay = kernel.anchor_y();

    parallel_for(0, src.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < src.width; ++x) {
                float acc[3] = {0.0f, 0.0f, 0.0f};
                for (int ky = 0; ky < kernel.height; ++ky) {
                    const int sy = fold(y + (ky - ay), src.height, border);
                    if (sy < 0) {
                        continue;
                    }
                    const float* srow = src.row(sy);
                    for (int kx = 0; kx < kernel.width; ++kx) {
                        const int sx = fold(x + (kx - ax), src.width, border);
                        if (sx < 0) {
                            continue;
                        }
                        const float weight =
                            flip ? kernel.at(kernel.width - 1 - kx, kernel.height - 1 - ky)
                                 : kernel.at(kx, ky);
                        const float* p = srow + sx * 4;
                        acc[0] += p[0] * weight;
                        acc[1] += p[1] * weight;
                        acc[2] += p[2] * weight;
                    }
                }
                float* q = row + x * 4;
                q[0] = acc[0];
                q[1] = acc[1];
                q[2] = acc[2];
                q[3] = src.at(x, y)[3];
            }
        }
    });
    return out;
}

Map<float> convolve(MapView<float> src, const Kernel& kernel, Border border, bool flip) {
    Map<float> out(src.width, src.height);
    const MapView<float> dst = out.view();
    const int ax = kernel.anchor_x();
    const int ay = kernel.anchor_y();

    parallel_for(0, src.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < src.width; ++x) {
                float acc = 0.0f;
                for (int ky = 0; ky < kernel.height; ++ky) {
                    const int sy = fold(y + (ky - ay), src.height, border);
                    if (sy < 0) {
                        continue;
                    }
                    const float* srow = src.row(sy);
                    for (int kx = 0; kx < kernel.width; ++kx) {
                        const int sx = fold(x + (kx - ax), src.width, border);
                        if (sx < 0) {
                            continue;
                        }
                        const float weight =
                            flip ? kernel.at(kernel.width - 1 - kx, kernel.height - 1 - ky)
                                 : kernel.at(kx, ky);
                        acc += srow[sx] * weight;
                    }
                }
                row[x] = acc;
            }
        }
    });
    return out;
}
