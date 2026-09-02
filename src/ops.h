#pragma once

#include <cstdint>
#include <string>

#include "adjacency.h"
#include "image.h"
#include "map.h"

// Todas operam em espaço linear, no lugar, e não cortam o resultado. Quem
// corta é a conversão pra 8 bits, na saída.
void adjust_exposure(ImageView image, float stops);
void adjust_contrast(ImageView image, float amount);
void adjust_gamma(ImageView image, float gamma);

void invert(ImageView image);

// Reduções de cor pra um número por pixel. Luminância é a mais usada, mas não
// é a melhor sempre: objeto colorido contra fundo de brilho parecido separa
// melhor por canal ou por saturação.
enum class Channel { Luma, Red, Green, Blue, Max, Min, Saturation };

const char* channel_name(Channel channel);

// `weights` só é usado por Channel::Luma. `on_srgb` decide se a redução vê o
// valor linear guardado ou o codificado: Rec. 709 é definido sobre linear, mas
// o clássico Rec. 601 (0.299/0.587/0.114) é definido sobre gama, e é o que o
// cvtColor do OpenCV faz.
Map<float> channel_of(ImageView image, Channel channel, const float* weights, bool on_srgb);

// Acima do nível vira 1, abaixo vira 0. Um rótulo binário ainda é rótulo.
Map<int32_t> threshold(MapView<float> scalar, float level);

// Pinta os rótulos por cima da cor, no lugar. Rótulo 0 não pinta nada.
void overlay_labels(ImageView image, MapView<int32_t> labels, float opacity);

// Equaliza pela distribuição acumulada. Em cor, equaliza a luminância e
// reescala o RGB por ela, o que mexe no contraste sem torcer a cor.
void equalize(MapView<float> scalar);
void equalize(ImageView image);

// Estica o intervalo entre dois percentis pra 0..1. Percentil em vez de mínimo
// e máximo porque um pixel morto de ruído não pode definir a escala inteira.
void stretch(MapView<float> scalar, float low_percentile, float high_percentile);
void stretch(ImageView image, float low_percentile, float high_percentile);

// Equalização local com limite de recorte. Divide em `tiles` x `tiles`
// pedaços, equaliza cada um e interpola entre os quatro vizinhos, o que evita
// a emenda visível. O recorte segura o ganho em região quase uniforme, senão
// ruído de fundo liso vira textura.
void clahe(MapView<float> scalar, int tiles, float clip_limit);
void clahe(ImageView image, int tiles, float clip_limit);

enum class Combine { Add, Subtract, AbsDiff, Multiply, Divide, Min, Max, Average };

const char* combine_name(Combine operation);

void combine(ImageView a, ImageView b, Combine operation, float scale, ImageView out);
void combine(MapView<float> a, MapView<float> b, Combine operation, float scale,
             MapView<float> out);
void combine(MapView<int32_t> a, MapView<int32_t> b, Combine operation, float scale,
             MapView<int32_t> out);

// Transformação de intensidade: uma expressão em `v`, aplicada pixel a pixel,
// sem olhar vizinho. É a família do Gonzalez (negativo, log, gama, fatiamento),
// e nenhuma delas cabe num kernel, porque convolução é linear.
bool apply_curve(ImageView image, const char* expression, float a, float b, float c, bool on_srgb,
                 std::string* error);
bool apply_curve(MapView<float> scalar, const char* expression, float a, float b, float c,
                 std::string* error);

enum class Rank { Median, Min, Max, Midpoint, AlphaTrimmed };

const char* rank_name(Rank kind);

// Estatística de ordem na vizinhança. Mediana não é convolução: nenhum jogo de
// pesos produz "o do meio depois de ordenar", e é justamente isso que faz ela
// tirar sal e pimenta sem borrar a borda.
Image rank_filter(ImageView image, const Adjacency& adjacency, Rank kind, float alpha);
Map<float> rank_filter(MapView<float> scalar, const Adjacency& adjacency, Rank kind, float alpha);

// Reescreve a distribuição de `image` pra parecer com a de `reference`, em vez
// de achatar como a equalização faz.
void match_histogram(ImageView image, ImageView reference);
void match_histogram(MapView<float> scalar, MapView<float> reference);

// Quantiza em 8 bits e devolve 1 onde o bit `plane` está ligado. Plano 7 é o
// mais significativo.
Map<float> bit_plane(ImageView image, int plane);
Map<float> bit_plane(MapView<float> scalar, int plane);

// Três escalares viram os canais de uma imagem. O inverso do `canal`.
Image compose(MapView<float> r, MapView<float> g, MapView<float> b);
