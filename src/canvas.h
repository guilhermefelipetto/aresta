#pragma once

struct Texture;

// Pan e zoom do canvas. O pan é o canto superior esquerdo da imagem em pixels
// de tela, relativo ao canto do painel.
struct Canvas {
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    bool needs_fit = true;

    // Escritos por draw_canvas a cada quadro. A barra de status e o painel de
    // ferramentas leem daqui, com um quadro de atraso que ninguém percebe.
    float view_w = 0.0f;
    float view_h = 0.0f;
    bool hovering = false;
    int hover_x = 0;
    int hover_y = 0;
};

// Desenha dentro da janela ImGui atual, ocupando o que sobrou dela.
void draw_canvas(Canvas& canvas, const Texture& texture);

// Muda o zoom deixando parado o centro do painel.
void canvas_zoom_to(Canvas& canvas, float zoom);
