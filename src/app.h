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

    // Id do estágio que pediu pra ser editado na janela dele. O painel só
    // levanta a mão; quem sabe qual janela abrir é o laço principal.
    int edit_request = -1;

    std::string path;
    std::string status;

    int source_channels = 0;
    int source_bits = 0;

    // Índice do que está na tela: o fixado quando existe, senão o selecionado.
    int shown() const;

    // Abrir imagem nova zera a cadeia, que é o certo quando você troca de
    // trabalho. Quem está carregando projeto ou religando a imagem que faltou
    // pede pra manter, senão a imagem entra e leva o projeto junto.
    bool open(const std::string& file, bool keep_chain = false);
    void evaluate();
    void upload_view();
};
