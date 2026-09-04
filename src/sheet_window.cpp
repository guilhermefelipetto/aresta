#include "sheet_window.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "app.h"
#include "chain.h"
#include "dialog.h"

namespace {

void apoio(const char* texto) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", texto);
    ImGui::PopStyleColor();
}

}  // namespace

void open_sheet_window(SheetWindow& window, App& app) {
    window.open = true;
    window.dirty = true;
    window.message.clear();
    window.failed = false;

    // Só o que está na tela vem marcado. Cadeia comprida marcada inteira gera
    // uma folha gigante que ninguém queria.
    window.items = sheet_items_from(app);
    const int visto = app.shown();
    for (std::size_t i = 0; i < window.items.size(); ++i) {
        window.items[i].on = (static_cast<int>(i) == visto || i == 0);
    }

    if (window.folder[0] == '\0') {
        std::snprintf(window.folder, sizeof(window.folder), "%s", downloads_folder().c_str());
    }
    if (window.name[0] == '\0') {
        const std::string base = app.path.empty()
                                     ? std::string("pipeline")
                                     : std::filesystem::path(app.path).stem().string();
        std::snprintf(window.name, sizeof(window.name), "%s-pipeline.png", base.c_str());
    }
}

void draw_sheet_window(SheetWindow& window, App& app) {
    if (!window.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1000.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Exportar pipeline", &window.open)) {
        ImGui::End();
        return;
    }

    // Estágio acrescentado ou removido enquanto a janela estava aberta.
    if (window.items.size() != app.chain.stages.size()) {
        std::vector<SheetItem> novos = sheet_items_from(app);
        for (SheetItem& novo : novos) {
            for (const SheetItem& velho : window.items) {
                if (velho.stage_id == novo.stage_id) {
                    novo.on = velho.on;
                    std::snprintf(novo.label, sizeof(novo.label), "%s", velho.label);
                    break;
                }
            }
        }
        window.items = std::move(novos);
        window.dirty = true;
    }
    if (window.seen_revision != app.revision) {
        window.seen_revision = app.revision;
        window.dirty = true;
    }

    // O rodapé fica fora das colunas que rolam, senão o botão de exportar some
    // embaixo da lista quando a cadeia é comprida.
    const float rodape = ImGui::GetFrameHeightWithSpacing() * 2.0f
                         + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float coluna = 330.0f;
    ImGui::BeginChild("controles", ImVec2(coluna, -rodape));

    ImGui::TextDisabled("Estágios");
    if (ImGui::SmallButton("todos")) {
        for (SheetItem& item : window.items) {
            item.on = true;
        }
        window.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("nenhum")) {
        for (SheetItem& item : window.items) {
            item.on = false;
        }
        window.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("legendas do resumo")) {
        const std::vector<SheetItem> padrao = sheet_items_from(app);
        for (std::size_t i = 0; i < window.items.size() && i < padrao.size(); ++i) {
            std::snprintf(window.items[i].label, sizeof(window.items[i].label), "%s",
                          padrao[i].label);
        }
        window.dirty = true;
    }

    ImGui::BeginChild("lista", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < window.items.size(); ++i) {
        SheetItem& item = window.items[i];
        const int indice = app.chain.index_of(item.stage_id);
        if (indice < 0) {
            continue;
        }
        ImGui::PushID(item.stage_id);
        if (ImGui::Checkbox("##on", &item.on)) {
            window.dirty = true;
        }
        ImGui::SameLine();
        ImGui::Text("%d %s", indice, op_info(app.chain.stages[indice].params).name);

        if (item.on) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##rotulo", item.label, sizeof(item.label))) {
                window.dirty = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("Layout");

    SheetLayout& l = window.layout;
    bool mudou = false;
    ImGui::SetNextItemWidth(-120.0f);
    mudou |= ImGui::SliderInt("colunas", &l.columns, 1, 8);
    ImGui::SetNextItemWidth(-120.0f);
    mudou |= ImGui::SliderInt("largura", &l.cell_width, 80, 900, "%d px");
    ImGui::SetNextItemWidth(-120.0f);
    mudou |= ImGui::SliderInt("espaço", &l.gap, 0, 60, "%d px");
    ImGui::SetNextItemWidth(-120.0f);
    mudou |= ImGui::SliderInt("margem", &l.margin, 0, 80, "%d px");

    int fundo = static_cast<int>(l.background);
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::Combo("fundo", &fundo, "transparente\0branco\0claro\0escuro\0")) {
        l.background = static_cast<SheetBackground>(fundo);
        mudou = true;
    }
    if (l.background == SheetBackground::Transparent) {
        apoio("PNG com alfa, pra cair num documento que já tem cor própria");
    }

    mudou |= ImGui::Checkbox("legenda embaixo", &l.labels);
    ImGui::SameLine();
    mudou |= ImGui::Checkbox("filete", &l.frame);
    if (mudou) {
        window.dirty = true;
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("previa", ImVec2(0.0f, -rodape));

    if (window.dirty) {
        window.dirty = false;
        const Sheet folha = compose_sheet(app, window.items, window.layout);
        if (folha.empty()) {
            window.preview_width = 0;
            window.preview_height = 0;
        } else {
            window.preview_width = folha.width;
            window.preview_height = folha.height;
            upload_rgba8(window.preview, folha.pixels.data(), folha.width, folha.height);
        }
    }

    if (window.preview_width == 0) {
        ImGui::TextDisabled("Marque pelo menos um estágio.");
    } else {
        ImGui::TextDisabled("%d x %d px", window.preview_width, window.preview_height);

        // O xadrez atrás só existe pra dar pra ver que o fundo é transparente.
        const ImVec2 canto = ImGui::GetCursorScreenPos();
        const ImVec2 area = ImGui::GetContentRegionAvail();
        const float escala =
            std::min(1.0f, std::min(area.x / window.preview_width,
                                    std::max(area.y, 1.0f) / window.preview_height));
        const ImVec2 tamanho(window.preview_width * escala, window.preview_height * escala);
        if (window.layout.background == SheetBackground::Transparent) {
            ImDrawList* lista = ImGui::GetWindowDrawList();
            const float lado = 8.0f;
            for (float y = 0.0f; y < tamanho.y; y += lado) {
                for (float x = 0.0f; x < tamanho.x; x += lado) {
                    const bool par = (static_cast<int>(x / lado) + static_cast<int>(y / lado)) % 2;
                    lista->AddRectFilled(
                        ImVec2(canto.x + x, canto.y + y),
                        ImVec2(canto.x + std::min(x + lado, tamanho.x),
                               canto.y + std::min(y + lado, tamanho.y)),
                        par ? IM_COL32(60, 60, 64, 255) : IM_COL32(44, 44, 48, 255));
                }
            }
        }
        ImGui::Image(static_cast<ImTextureID>(window.preview.id), tamanho);
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("pasta", window.folder, sizeof(window.folder));
    ImGui::SameLine();
    if (ImGui::Button("...")) {
        std::string escolhido;
        if (pick_open_file("Pasta de saída", window.folder, {}, &escolhido)
            == PickResult::Chose) {
            const std::filesystem::path p(escolhido);
            std::snprintf(window.folder, sizeof(window.folder), "%s",
                          p.parent_path().string().c_str());
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("arquivo", window.name, sizeof(window.name));
    ImGui::SameLine();
    if (ImGui::Button("Exportar")) {
        const Sheet folha = compose_sheet(app, window.items, window.layout);
        const std::string caminho =
            (std::filesystem::path(window.folder) / window.name).string();
        std::string erro;
        window.failed = !write_sheet(folha, caminho, &erro);
        window.message = window.failed ? erro : ("Salvo em " + caminho);
    }
    if (!window.message.empty()) {
        const ImVec4 cor = window.failed ? ImVec4(0.9f, 0.45f, 0.45f, 1.0f)
                                         : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
        ImGui::PushStyleColor(ImGuiCol_Text, cor);
        ImGui::TextWrapped("%s", window.message.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
