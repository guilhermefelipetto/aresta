#pragma once

#include "histogram.h"

struct App;

struct HistogramWindow {
    bool open = false;

    Histogram histogram;
    bool visible[4] = {true, true, true, true};
    bool log_scale = true;
    bool srgb = true;
    int bins = 256;

    float otsu = 0.0f;
    bool has_otsu = false;

    float mean = 0.0f;
    float median = 0.0f;
    float deviation = 0.0f;
    float entropy = 0.0f;

    float stretch_low = 0.5f;
    float stretch_high = 99.5f;

    int seen_revision = -1;
    int seen_stage = -1;
    bool seen_srgb = true;
    int seen_bins = 0;
};

void draw_histogram_window(HistogramWindow& window, App& app);
