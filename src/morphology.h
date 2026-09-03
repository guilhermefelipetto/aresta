#pragma once

#include <cstdint>

#include "adjacency.h"
#include "map.h"

enum class Morph { Erode, Dilate, Open, Close, Gradient, TopHat, BlackHat };

const char* morph_name(Morph operation);

// Morfologia de tom contínuo: erosão é mínimo na vizinhança, dilatação é
// máximo. Sobre um mapa 0/1 isso recai na morfologia binária de sempre.
Map<float> morphology(MapView<float> src, const Adjacency& adjacency, Morph operation);
Map<int32_t> morphology(MapView<int32_t> src, const Adjacency& adjacency, Morph operation);

// Renumera cada componente conexa a partir de 1, mantendo o fundo em 0. Dois
// pixels só entram na mesma componente se tiverem o mesmo rótulo de entrada.
Map<int32_t> connected_components(MapView<int32_t> src, const Adjacency& adjacency, int* count);

// Distância euclidiana exata até o pixel de fundo mais próximo, pelo algoritmo
// separável do Felzenszwalb e Huttenlocher: uma passada por eixo, sem
// aproximação de chanfro.
//
// Com `inside` ligado mede de dentro do objeto até o fundo; desligado mede do
// fundo até o objeto.
Map<float> distance_transform(MapView<int32_t> labels, bool inside);

// Dilatação geodésica repetida até parar de mudar: o marcador cresce mas nunca
// escapa da máscara. É a peça de onde saem preenchimento de buraco e abertura
// por reconstrução.
Map<int32_t> reconstruct(MapView<int32_t> marker, MapView<int32_t> mask,
                         const Adjacency& adjacency);

// Reconstrói a partir da borda e inverte: o que sobra são os buracos.
Map<int32_t> fill_holes(MapView<int32_t> labels, const Adjacency& adjacency);

enum class Thin { Thin, Thicken, Skeleton };

const char* thin_name(Thin kind);

// Afinamento pelos oito gabaritos girados, aplicados em sequência. Esqueleto é
// afinar até não mudar mais.
Map<int32_t> thinning(MapView<int32_t> labels, Thin kind, int iterations);

// Casa onde o gabarito bate: 1 exige objeto, 0 exige fundo, -1 não olha.
Map<int32_t> hit_or_miss(MapView<int32_t> labels, const int pattern[9]);
