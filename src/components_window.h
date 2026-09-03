#pragma once

#include <string>
#include <vector>

#include "regions.h"

struct App;

struct ComponentsWindow {
    bool open = false;
    int editing = -1;

    std::vector<Region> all;
    std::vector<Region> kept;

    int seen_revision = -1;
    int seen_stage = -1;
};

void attach_components_window(ComponentsWindow& window, App& app, int stage_id);
void draw_components_window(ComponentsWindow& window, App& app);
