#pragma once

#include <string>

#include "export.h"

struct App;

struct ExportWindow {
    bool open = false;

    int stage = 0;
    bool raw = false;
    ExportFormat format = ExportFormat::Png;
    int quality = 92;

    char folder[512] = {};
    char name[256] = {};

    std::string message;
    bool failed = false;
};

// Preenche os padrões a partir do que está na tela e abre.
void open_export_window(ExportWindow& window, App& app);
void draw_export_window(ExportWindow& window, App& app);
