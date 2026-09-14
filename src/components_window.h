#pragma once

#include <string>
#include <vector>

#include "descriptors.h"
#include "regions.h"

struct App;

struct ComponentsWindow {
    bool open = false;
    int editing = -1;

    std::vector<Region> all;
    std::vector<Region> kept;

    // Descritores saem do mapa já rotulado, que é a saída do estágio: a
    // entrada pode ser binária, e aí tudo seria uma região só. O índice aqui é
    // o rótulo novo menos um, o mesmo de `kept`.
    std::vector<Shape> shapes;
    bool show_descriptors = false;

    int seen_revision = -1;
    int seen_stage = -1;
};

void attach_components_window(ComponentsWindow& window, App& app, int stage_id);
void draw_components_window(ComponentsWindow& window, App& app);
