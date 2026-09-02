#pragma once

#include <cstdint>

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
Map<float> channel_of(ImageView image, Channel channel);

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
