#pragma once

#include <cstdint>
#include <string>

#include "adjacency.h"
#include "image.h"
#include "map.h"
#include "spaces.h"

// Todas operam em espaço linear, no lugar, e não cortam o resultado. Quem
// corta é a conversão pra 8 bits, na saída.
void adjust_exposure(ImageView image, float stops);
void adjust_contrast(ImageView image, float amount);
void adjust_gamma(ImageView image, float gamma);

// Cor reflete em torno de 1, escalar em torno do próprio intervalo, e rótulo
// vira complemento binário: fundo e objeto trocam de lado.
void invert(ImageView image);
void invert(MapView<float> scalar);
void invert(MapView<int32_t> labels);

// Reduz a cor a um número por pixel. `component` de 0 a 2 escolhe dentro do
// espaço; 3 vale só no RGB e pede a luminância ponderada por `weights`.
//
// `on_srgb` também só vale no RGB: Rec. 709 é definido sobre linear, mas o
// clássico Rec. 601 (0.299/0.587/0.114) é definido sobre gama, e é o que o
// cvtColor do OpenCV faz. Os outros espaços já sabem sozinhos onde moram.
constexpr int channel_luma = 3;

Map<float> channel_of(ImageView image, Space space, int component, const float* weights,
                      bool on_srgb);

void scalar_range(MapView<float> scalar, float* lo, float* hi);

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

// Três escalares viram uma imagem, lidos como componentes do espaço escolhido.
// O inverso do `canal`.
Image compose(Space space, MapView<float> a, MapView<float> b, MapView<float> c);

// Gradiente de cor do Di Zenzo: trata os três canais como um vetor só, em vez
// de pegar o maior gradiente por canal. Onde os canais discordam de direção,
// os dois dão respostas bem diferentes.
Map<float> color_gradient(ImageView image, Space space);

// Distância até uma cor de referência, no espaço escolhido.
Map<float> color_distance(ImageView image, Space space, const float reference[3]);

// Fatia dos pixels que compartilham a faixa mais cheia. Equalização é função
// de uma variável, então tudo que entra igual sai igual: com essa fatia alta,
// ela não tem o que espalhar.
float dominant_share(MapView<float> scalar);
float dominant_share(ImageView image);
