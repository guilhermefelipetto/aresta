#pragma once

#include <string>
#include <vector>

struct App;

// Fundo da folha. Transparente serve pra jogar num documento que já tem cor
// própria; os outros dois pra figura que precisa se sustentar sozinha.
enum class SheetBackground { Transparent, White, Light, Dark };

const char* sheet_background_name(SheetBackground background);

struct SheetItem {
    int stage_id = -1;
    bool on = true;

    // Legenda embaixo do quadro. Começa no resumo do estágio e é editável,
    // porque o nome que serve na cadeia raramente é o que serve na figura.
    char label[96] = {};
};

struct SheetLayout {
    int columns = 5;        // 0 põe tudo numa linha só
    int cell_width = 280;
    int gap = 10;
    int margin = 12;
    int label_gap = 6;
    SheetBackground background = SheetBackground::Dark;
    bool labels = true;
    bool frame = false;     // filete em volta de cada quadro
};

struct Sheet {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;  // RGBA8, não pré-multiplicado

    bool empty() const { return pixels.empty(); }
};

// Monta a folha com os estágios ligados, na ordem em que aparecem na lista.
// Precisa de um contexto do ImGui vivo, porque a legenda sai da fonte dele.
Sheet compose_sheet(const App& app, const std::vector<SheetItem>& items,
                    const SheetLayout& layout);

bool write_sheet(const Sheet& sheet, const std::string& path, std::string* error);

// Preenche a lista com a cadeia inteira, legenda no resumo de cada estágio.
std::vector<SheetItem> sheet_items_from(const App& app);
