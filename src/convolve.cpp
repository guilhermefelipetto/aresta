#include "convolve.h"

#include <algorithm>

#include "fft.h"
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

const char* conv_path_name(ConvPath path) {
    switch (path) {
        case ConvPath::Auto: return "automático";
        case ConvPath::Spatial: return "espacial";
        case ConvPath::Frequency: return "frequência";
    }
    return "?";
}

const char* border_name(Border border) {
    switch (border) {
        case Border::Zero: return "zero";
        case Border::Clamp: return "estender";
        case Border::Mirror: return "espelhar";
        case Border::Wrap: return "circular";
    }
    return "?";
}

Image convolve_spatial(ImageView src, const Kernel& kernel, Border border, bool flip) {
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

Map<float> convolve_spatial(MapView<float> src, const Kernel& kernel, Border border,
                            bool flip) {
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

namespace {

int next_power_of_two(int n) {
    int p = 1;
    while (p < n) {
        p *= 2;
    }
    return p;
}

// A FFT faz convolução circular, então a cauda do vetor precisa representar os
// índices negativos. Sem isso a borda esquerda lê o lado errado da imagem.
Map<float> pad_for_fft(MapView<float> src, int kw, int kh, Border border, int* pw, int* ph) {
    *pw = next_power_of_two(src.width + kw - 1);
    *ph = next_power_of_two(src.height + kh - 1);

    Map<float> out(*pw, *ph);
    for (int y = 0; y < *ph; ++y) {
        const int sy = fold(y >= *ph - kh ? y - *ph : y, src.height, border);
        float* row = out.view().row(y);
        for (int x = 0; x < *pw; ++x) {
            const int sx = fold(x >= *pw - kw ? x - *pw : x, src.width, border);
            row[x] = (sx < 0 || sy < 0) ? 0.0f : src.at(sx, sy);
        }
    }
    return out;
}

Map<float> convolve_frequency(MapView<float> src, const Kernel& kernel, Border border, bool flip) {
    int pw = 0;
    int ph = 0;
    const Map<float> preenchida =
        pad_for_fft(src, kernel.width, kernel.height, border, &pw, &ph);

    // Correlação equivale a convoluir com o kernel espelhado, então a posição
    // do coeficiente na grade enrolada é que decide qual das duas sai.
    Map<float> nucleo(pw, ph);
    nucleo.fill(0.0f);
    const int ax = kernel.anchor_x();
    const int ay = kernel.anchor_y();
    for (int ky = 0; ky < kernel.height; ++ky) {
        for (int kx = 0; kx < kernel.width; ++kx) {
            const int dx = flip ? (kx - ax) : (ax - kx);
            const int dy = flip ? (ky - ay) : (ay - ky);
            nucleo.view().at(((dx % pw) + pw) % pw, ((dy % ph) + ph) % ph) += kernel.at(kx, ky);
        }
    }

    Spectrum a = forward_fft(preenchida.view(), Pad::Zero);
    const Spectrum b = forward_fft(nucleo.view(), Pad::Zero);
    for (std::size_t i = 0; i < a.re.size(); ++i) {
        const float re = a.re[i] * b.re[i] - a.im[i] * b.im[i];
        const float im = a.re[i] * b.im[i] + a.im[i] * b.re[i];
        a.re[i] = re;
        a.im[i] = im;
    }
    a.source_width = src.width;
    a.source_height = src.height;
    return inverse_fft(a);
}

// O caminho direto custa w*h por pixel; o da frequência custa o mesmo pra
// qualquer kernel. Medido em 736x414, o espacial ganha até 21x21 e perde feio
// a partir daí: 41x41 leva 46 ms contra 15, e 81x81 leva 181 contra 15.
bool prefer_frequency(const Kernel& kernel, ConvPath path) {
    if (path == ConvPath::Spatial) {
        return false;
    }
    if (path == ConvPath::Frequency) {
        return true;
    }
    return kernel.width * kernel.height >= 625;
}

}  // namespace

Map<float> convolve(MapView<float> src, const Kernel& kernel, Border border, bool flip,
                    ConvPath path, bool* used_fft) {
    const bool pela_frequencia = prefer_frequency(kernel, path);
    if (used_fft) {
        *used_fft = pela_frequencia;
    }
    return pela_frequencia ? convolve_frequency(src, kernel, border, flip)
                           : convolve_spatial(src, kernel, border, flip);
}

Image convolve(ImageView src, const Kernel& kernel, Border border, bool flip, ConvPath path,
               bool* used_fft) {
    const bool pela_frequencia = prefer_frequency(kernel, path);
    if (used_fft) {
        *used_fft = pela_frequencia;
    }
    if (!pela_frequencia) {
        return convolve_spatial(src, kernel, border, flip);
    }

    Image out(src.width, src.height);
    Map<float> plano(src.width, src.height);
    for (int channel = 0; channel < 3; ++channel) {
        for (int y = 0; y < src.height; ++y) {
            const float* p = src.row(y);
            float* row = plano.view().row(y);
            for (int x = 0; x < src.width; ++x) {
                row[x] = p[x * 4 + channel];
            }
        }
        const Map<float> feito = convolve_frequency(plano.view(), kernel, border, flip);
        for (int y = 0; y < src.height; ++y) {
            const float* row = feito.view().row(y);
            float* q = out.view().row(y);
            for (int x = 0; x < src.width; ++x) {
                q[x * 4 + channel] = row[x];
            }
        }
    }
    for (int y = 0; y < src.height; ++y) {
        const float* p = src.row(y);
        float* q = out.view().row(y);
        for (int x = 0; x < src.width; ++x) {
            q[x * 4 + 3] = p[x * 4 + 3];
        }
    }
    return out;
}
