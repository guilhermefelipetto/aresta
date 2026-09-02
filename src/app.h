#pragma once

#include <string>

#include "canvas.h"
#include "image.h"
#include "texture.h"

struct App {
    Image source;   // como veio do arquivo, já em linear
    Image working;  // source com os ajustes aplicados
    Texture texture;
    Canvas canvas;

    std::string path;
    std::string status;

    float exposure = 0.0f;
    float contrast = 0.0f;
    float gamma = 1.0f;

    float luma_min = 0.0f;
    float luma_max = 0.0f;
    float luma_mean = 0.0f;

    bool open(const std::string& file);
    void reset_adjustments();

    // Refaz working a partir de source e sobe pra textura. Chamar quando um
    // parâmetro mudar.
    void reapply();
};
