#pragma once

#include <cstdint>

#include "image.h"
#include "map.h"

// Todas operam em espaço linear, no lugar, e não cortam o resultado — quem
// corta é a conversão pra 8 bits, na saída.
void adjust_exposure(ImageView image, float stops);
void adjust_contrast(ImageView image, float amount);
void adjust_gamma(ImageView image, float gamma);

void invert(ImageView image);

Map<float> luminance_of(ImageView image);

// Acima do nível vira 1, abaixo vira 0. Um rótulo binário ainda é rótulo.
Map<int32_t> threshold(MapView<float> scalar, float level);

// Pinta os rótulos por cima da cor, no lugar. Rótulo 0 não pinta nada.
void overlay_labels(ImageView image, MapView<int32_t> labels, float opacity);
