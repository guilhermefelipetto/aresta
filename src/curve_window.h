#pragma once

#include <string>

struct App;

struct CurveWindow {
    bool open = false;

    char expression[128] = "a * v^b";
    float a = 1.0f;
    float b = 0.45f;
    float c = 0.0f;
    bool on_srgb = true;

    std::string error;
    int editing = -1;
    std::string message;
};

void attach_curve_window(CurveWindow& window, App& app, int stage_id);

void draw_curve_window(CurveWindow& window, App& app);
