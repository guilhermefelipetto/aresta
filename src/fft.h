#pragma once

#include <vector>

#include "image.h"
#include "map.h"

enum class Pad { Mirror, Zero };
enum class FreqShape { Ideal, Butterworth, Gaussian };
enum class FreqKind { LowPass, HighPass, BandPass, BandReject };

const char* pad_name(Pad pad);
const char* freq_shape_name(FreqShape shape);
const char* freq_kind_name(FreqKind kind);

// A radix-2 exige lado potência de dois, então o mapa é ampliado antes e
// cortado depois. A origem de frequência fica em (0,0), não no centro: quem
// quer o centro é a visualização, e ela desloca na hora de desenhar.
struct Spectrum {
    int width = 0;
    int height = 0;
    int source_width = 0;
    int source_height = 0;
    std::vector<float> re;
    std::vector<float> im;

    bool empty() const { return width == 0 || height == 0; }
};

Spectrum forward_fft(MapView<float> src, Pad pad);
Map<float> inverse_fft(const Spectrum& spectrum);

// Magnitude com a origem levada pro centro. Sem o log, o pico do DC é tão
// maior que o resto que a imagem sai preta com um ponto branco.
Map<float> spectrum_magnitude(const Spectrum& spectrum, bool logarithmic);

// `cutoff` vai de 0 a 1, fração do raio de Nyquist, pra não depender do
// tamanho da imagem. `width` só é usado pelas formas de faixa.
void apply_freq_filter(Spectrum& spectrum, FreqShape shape, FreqKind kind, float cutoff,
                       int order, float width);

// Atalhos que fazem a ida, o filtro e a volta. Em cor, canal por canal.
Image filter_frequency(ImageView image, FreqShape shape, FreqKind kind, float cutoff, int order,
                       float width, Pad pad);
Map<float> filter_frequency(MapView<float> scalar, FreqShape shape, FreqKind kind, float cutoff,
                            int order, float width, Pad pad);
