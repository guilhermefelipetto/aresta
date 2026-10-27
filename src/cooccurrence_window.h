#pragma once

#include "cooccurrence.h"
#include "texture.h"

struct App;

// Ferramentas > Co-ocorrência: a matriz do estágio na tela, desenhada como
// imagem, e as medidas de Haralick dela. É o que mostra o que cada medida está
// medindo, coisa que o mapa de textura sozinho não deixa ver.
struct CooccurrenceWindow {
    bool open = false;

    int levels = 16;
    int distance = 1;
    Direction direction = Direction::Right;
    bool symmetric = true;
    bool unit_range = false;
    bool log_scale = true;
    int radius = 7;
    int chosen = 0;

    Cooccurrence matrix;
    double measures[haralick_count] = {};
    float lo = 0.0f;
    float hi = 1.0f;
    bool from_color = false;
    Texture texture;

    int seen_revision = -1;
    int seen_stage = -1;
    long long seen_settings = -1;
};

void draw_cooccurrence_window(CooccurrenceWindow& window, App& app);
