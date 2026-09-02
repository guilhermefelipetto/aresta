#pragma once

#include <string>

#include "canvas.h"
#include "chain.h"
#include "image.h"
#include "texture.h"
#include "value.h"

struct App {
    Image source;
    Chain chain;
    Texture texture;
    Canvas canvas;

    int viewed = 0;  // índice na lista de estágios, não id
    Colormap colormap = Colormap::Gray;
    float view_lo = 0.0f;
    float view_hi = 0.0f;

    // Sobe a cada reavaliação. Quem cacheia coisa derivada da cadeia compara
    // com isso pra saber se precisa refazer.
    int revision = 0;

    // Cadeia comprida vira parede de slider. Com isso ligado, só o estágio
    // selecionado abre os parâmetros.
    bool chain_compact = true;

    std::string path;
    std::string status;

    bool open(const std::string& file);
    void evaluate();
    void upload_view();
};
