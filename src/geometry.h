#pragma once

#include <cstdint>

#include "image.h"
#include "map.h"

enum class Interp { Nearest, Bilinear, Bicubic };

const char* interp_name(Interp interp);

// Mapa afim do destino pra origem: é assim que se reamostra sem deixar buraco.
// Percorrer a origem e espalhar no destino deixa pixel sem escrever.
struct Affine {
    float m[6] = {1, 0, 0, 0, 1, 0};
};

Affine affine_scale(float sx, float sy);
Affine affine_rotation(float degrees, float cx, float cy, float dcx, float dcy);

Image warp(ImageView src, int width, int height, const Affine& to_source, Interp interp);
Map<float> warp(MapView<float> src, int width, int height, const Affine& to_source, Interp interp);

// Rótulo só aceita vizinho mais próximo: média de índice de região não quer
// dizer nada.
Map<int32_t> warp(MapView<int32_t> src, int width, int height, const Affine& to_source);

Image crop(ImageView src, int x, int y, int width, int height);
Map<float> crop(MapView<float> src, int x, int y, int width, int height);
Map<int32_t> crop(MapView<int32_t> src, int x, int y, int width, int height);

Image flip(ImageView src, bool horizontal, bool vertical, bool transpose);
Map<float> flip(MapView<float> src, bool horizontal, bool vertical, bool transpose);
Map<int32_t> flip(MapView<int32_t> src, bool horizontal, bool vertical, bool transpose);

// Reduz pra `levels` degraus dentro do intervalo do próprio dado.
void quantize(ImageView image, int levels);
void quantize(MapView<float> scalar, int levels);
