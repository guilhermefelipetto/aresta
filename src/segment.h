#pragma once

#include <cstdint>
#include <vector>

#include "map.h"

// Canny inteiro: suaviza, deriva, afina pela direção do gradiente e liga o que
// sobrou por histerese. Os dois limiares vão de 0 a 1, fração do maior
// gradiente, pra não depender do contraste da imagem.
Map<int32_t> canny(MapView<float> scalar, float sigma, float low, float high);

// Marr-Hildreth: onde o laplaciano da gaussiana troca de sinal. `slope` corta
// as trocas fracas, que aparecem em qualquer região quase lisa.
Map<int32_t> log_zero_crossings(MapView<float> scalar, float sigma, float slope);

enum class LocalThreshold { Mean, Gaussian, Sauvola };

const char* local_threshold_name(LocalThreshold kind);

// Limiar que muda de lugar pra lugar. Média e gaussiana descontam `offset` da
// média local; Sauvola usa também o desvio, o que aguenta fundo com iluminação
// desigual sem inventar borda em região lisa.
Map<int32_t> adaptive_threshold(MapView<float> scalar, LocalThreshold kind, float radius,
                                float offset, float k);

// Otsu com mais de duas classes: acha os limiares que maximizam a variância
// entre elas. Devolve os níveis escolhidos em `levels`.
Map<int32_t> multi_otsu(MapView<float> scalar, int classes, std::vector<float>* levels);
