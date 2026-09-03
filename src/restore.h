#pragma once

#include "adjacency.h"
#include "fft.h"
#include "image.h"
#include "map.h"

// Os modelos do Gonzalez. A semente é parâmetro, e não sorteada, senão o ruído
// muda a cada quadro e não dá pra comparar dois filtros sobre a mesma sujeira.
enum class Noise { Gaussian, Rayleigh, Gamma, Exponential, Uniform, SaltPepper, Periodic };

const char* noise_name(Noise kind);

// O que a, b e c querem dizer muda com o modelo; a UI rotula cada um.
void add_noise(ImageView image, Noise kind, float a, float b, float c, unsigned seed);
void add_noise(MapView<float> scalar, Noise kind, float a, float b, float c, unsigned seed);

enum class Mean { Arithmetic, Geometric, Harmonic, Contraharmonic };

const char* mean_name(Mean kind);

// Média geométrica preserva detalhe melhor que a aritmética; a harmônica pega
// sal e falha em pimenta; a contra-harmônica troca de lado conforme o sinal do
// Q, e com Q errado ela apaga o que devia manter.
Image mean_filter(ImageView image, const Adjacency& adjacency, Mean kind, float q);
Map<float> mean_filter(MapView<float> scalar, const Adjacency& adjacency, Mean kind, float q);

// Reduz onde o ruído domina e deixa quieto onde tem detalhe, comparando a
// variância local com a do ruído.
Image adaptive_denoise(ImageView image, const Adjacency& adjacency, float noise_variance);
Map<float> adaptive_denoise(MapView<float> scalar, const Adjacency& adjacency,
                            float noise_variance);

// Cresce a janela até a mediana deixar de ser extremo, e só então decide. Pega
// sal e pimenta denso, onde a mediana de janela fixa já desiste.
Image adaptive_median(ImageView image, float max_radius);
Map<float> adaptive_median(MapView<float> scalar, float max_radius);

enum class Degradation { Motion, Turbulence };
enum class Restoration { Inverse, Wiener, ConstrainedLS };

const char* degradation_name(Degradation kind);
const char* restoration_name(Restoration method);

// Borrado de movimento pede o deslocamento em pixels; turbulência pede a
// constante k. As duas usam frequência normalizada pelo raio de Nyquist, então
// o mesmo parâmetro quer dizer a mesma coisa em qualquer resolução.
Image degrade(ImageView image, Degradation kind, float dx, float dy, float k, Pad pad);
Map<float> degrade(MapView<float> scalar, Degradation kind, float dx, float dy, float k, Pad pad);

// Restaurar exige supor uma degradação, e é isso que o capítulo ensina: os
// parâmetros aqui têm que bater com os que sujaram a imagem, senão o resultado
// não quer dizer nada.
Image restore(ImageView image, Restoration method, Degradation kind, float dx, float dy, float k,
              float parameter, float limit, Pad pad);
Map<float> restore(MapView<float> scalar, Restoration method, Degradation kind, float dx, float dy,
                   float k, float parameter, float limit, Pad pad);
