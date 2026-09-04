#pragma once

#include <string>
#include <vector>

#include "sheet.h"
#include "texture.h"

struct App;

struct SheetWindow {
    bool open = false;

    std::vector<SheetItem> items;
    SheetLayout layout;

    char folder[512] = {};
    char name[256] = {};

    // A prévia é a própria folha, então o que você vê é o arquivo. Só é
    // refeita quando alguma coisa muda, senão seria remontar tudo por quadro.
    Texture preview;
    int preview_width = 0;
    int preview_height = 0;
    bool dirty = true;
    int seen_revision = -1;

    std::string message;
    bool failed = false;
};

void open_sheet_window(SheetWindow& window, App& app);
void draw_sheet_window(SheetWindow& window, App& app);
