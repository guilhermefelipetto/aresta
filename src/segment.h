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

// Mínimos regionais do relevo, um rótulo por mínimo. Um platô é mínimo quando
// nenhum vizinho de fora dele está mais baixo, e é o platô inteiro que ganha o
// rótulo, não um pixel escolhido a dedo dentro dele.
//
// `h` é a profundidade mínima que um vale precisa ter pra contar. Com zero,
// todo mínimo entra, e num gradiente de imagem real isso quer dizer um mínimo
// por chiado: é daí que vem a super-segmentação que dá fama ruim ao
// watershed. Com `h` maior, os vales rasos são afundados antes por
// reconstrução e somem, e sobra marcador que vale a pena.
//
// `h_absolute` desligado lê `h` como fração da faixa que o relevo ocupa de
// fato, que é o que sobrevive a trocar um gradiente que vai até 0.2 por um
// canal L de Lab que vai até 100.
Map<int32_t> regional_minima(MapView<float> relief, const Adjacency& adjacency, float h,
                             bool h_absolute, int* count);

// Inundação a partir dos marcadores, na ordem do relevo. É a IFT com custo
// fmax: cada pixel fica com o marcador cujo caminho até ele passa pelo ponto
// mais baixo possível.
//
// A máscara segura a água, e pode vir vazia. Sobre um relevo binarizado ela é
// quase obrigatória: sem ela a frente escorre pelo fundo e uma bacia só toma a
// imagem inteira, que é o modo clássico de o watershed decepcionar. Sobre um
// gradiente em tom contínuo o certo é não ter máscara nenhuma, porque ali todo
// pixel pertence a alguma bacia e o que separa as bacias é o relevo. Marcador
// fora da máscara é ignorado.
//
// Confere pixel a pixel com o segmentation.watershed do scikit-image, com e
// sem linha e com e sem máscara, mas sem herdar o defeito dele: lá um pixel
// entra na fila várias vezes, e com linha ligada e marcador denso a conta
// explode.
Map<int32_t> watershed(MapView<float> relief, MapView<int32_t> markers, MapView<int32_t> mask,
                       const Adjacency& adjacency, bool draw_lines);
