#pragma once

#include <cstdint>
#include <vector>

#include "adjacency.h"
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

// Acumulador de Hough para retas: colunas são o ângulo, linhas são a distância
// até a origem. Cada ponto de borda vira uma senoide, e reta na imagem vira
// pico aqui.
Map<float> hough_accumulator(MapView<int32_t> edges, int thetas, int rhos);

// As retas mais votadas, desenhadas de volta na imagem.
Map<int32_t> hough_lines(MapView<int32_t> edges, int thetas, int rhos, float threshold,
                         int max_lines);

// Círculos por votação em (x, y, raio). `step` controla de quanto em quanto o
// raio anda, porque o acumulador é tridimensional e cresce rápido.
Map<int32_t> hough_circles(MapView<int32_t> edges, float min_radius, float max_radius, float step,
                           float threshold, int max_circles);

// Inundação a partir dos marcadores, na ordem do relevo. É a IFT com custo
// fmax: cada pixel fica com o marcador cujo caminho até ele passa pelo ponto
// mais baixo possível.
//
// A máscara é o que segura a água. Sem ela a frente escorre pelo fundo e uma
// bacia só toma a imagem inteira, que é o modo clássico de o watershed
// decepcionar.
Map<int32_t> watershed(MapView<float> relief, MapView<int32_t> markers, MapView<int32_t> mask,
                       const Adjacency& adjacency, bool draw_lines);
