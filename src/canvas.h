#pragma once

struct Texture;

// Pan e zoom do canvas. O pan é o canto superior esquerdo da imagem em pixels
// de tela, relativo ao canto do painel.
struct Canvas {
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    bool needs_fit = true;
};

// Desenha dentro da janela ImGui atual, ocupando o que sobrou dela.
void draw_canvas(Canvas& canvas, const Texture& texture);
