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

    std::string path;
    std::string status;

    bool open(const std::string& file);
    void evaluate();
    void upload_view();
};
