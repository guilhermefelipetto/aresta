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

    int viewed = 0;  // índice do estágio selecionado pra edição

    // Id do estágio preso na tela, ou -1 pra tela seguir a seleção. Id e não
    // índice, pra fixação sobreviver a apagar estágio do meio.
    int pinned = -1;
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

    // Índice do que está na tela: o fixado quando existe, senão o selecionado.
    int shown() const;

    bool open(const std::string& file);
    void evaluate();
    void upload_view();
};
