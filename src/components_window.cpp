#include "components_window.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include <imgui.h>

#include <fstream>

#include "app.h"
#include "dialog.h"
#include "chain.h"

namespace {

// A janela refaz a conta com a mesma função que a cadeia usou, e não com uma
// regra própria, senão a lista e a imagem discordam.
void recompute(ComponentsWindow& window, App& app) {
    window.all.clear();
    window.kept.clear();
    window.shapes.clear();

    const int index = app.chain.index_of(window.editing);
    if (index < 0) {
        return;
    }
    const auto* op = std::get_if<ComponentsOp>(&app.chain.stages[index].params);
    if (!op || app.chain.stages[index].inputs.empty()) {
        return;
    }
    const int source = app.chain.index_of(app.chain.stages[index].inputs[0]);
    if (source < 0 || source >= static_cast<int>(app.chain.outputs.size())) {
        return;
    }
    const Value& entrada = app.chain.outputs[static_cast<std::size_t>(source)];
    if (entrada.empty() || entrada.kind != ValueKind::Label) {
        return;
    }

    label_and_filter(entrada.label.view(), adjacency_by_radius(op->radius), op->filter,
                     &window.all, &window.kept);

    const Value& saida = app.chain.outputs[static_cast<std::size_t>(index)];
    if (!saida.empty() && saida.kind == ValueKind::Label) {
        window.shapes = describe_regions(saida.label.view());
    }
}

}  // namespace

void attach_components_window(ComponentsWindow& window, App& app, int stage_id) {
    const int index = app.chain.index_of(stage_id);
    if (index < 0 || !std::get_if<ComponentsOp>(&app.chain.stages[index].params)) {
        return;
    }
    window.editing = stage_id;
    window.open = true;
    window.seen_revision = -1;
    app.viewed = index;
    app.upload_view();
}

void draw_components_window(ComponentsWindow& window, App& app) {
    if (!window.open) {
        return;
    }
    if (window.editing >= 0 && app.chain.index_of(window.editing) < 0) {
        window.editing = -1;
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Componentes", &window.open)) {
        ImGui::End();
        return;
    }

    int index = app.chain.index_of(window.editing);
    if (index < 0) {
        // Sem ligação, pega o primeiro estágio de componentes que existir.
        for (std::size_t i = 0; i < app.chain.stages.size(); ++i) {
            if (std::get_if<ComponentsOp>(&app.chain.stages[i].params)) {
                window.editing = app.chain.stages[i].id;
                window.seen_revision = -1;
                index = static_cast<int>(i);
                break;
            }
        }
    }
    if (index < 0) {
        ImGui::TextDisabled("Nenhum estágio de componentes na cadeia.");
        ImGui::End();
        return;
    }

    auto* op = std::get_if<ComponentsOp>(&app.chain.stages[index].params);
    if (window.seen_revision != app.revision || window.seen_stage != index) {
        recompute(window, app);
        window.seen_revision = app.revision;
        window.seen_stage = index;
    }

    ImGui::Text("estágio %d", index);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu de %zu componentes", window.kept.size(), window.all.size());

    ImGui::Spacing();
    bool changed = false;

    ImGui::BeginChild("filtros", ImVec2(ImGui::GetContentRegionAvail().x * 0.52f, 0.0f));
    {
        ImGui::SetNextItemWidth(-120.0f);
        changed |= ImGui::SliderFloat("vizinhança", &op->radius, 1.0f, 3.0f, "raio %.2f");
        ImGui::TextDisabled("%zu vizinhos", adjacency_by_radius(op->radius).offsets.size());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-120.0f);
        changed |= ImGui::DragInt("área mínima", &op->filter.min_area, 1.0f, 0, 1 << 24, "%d px");
        ImGui::SetNextItemWidth(-120.0f);
        changed |= ImGui::DragInt("área máxima", &op->filter.max_area, 1.0f, 0, 1 << 24,
                                  op->filter.max_area > 0 ? "%d px" : "sem teto");
        ImGui::SetNextItemWidth(-120.0f);
        changed |= ImGui::DragInt("maiores", &op->filter.keep_largest, 0.2f, 0, 4096,
                                  op->filter.keep_largest > 0 ? "os %d maiores" : "todos");

        ImGui::Spacing();
        changed |= ImGui::Checkbox("descartar quem toca a borda", &op->filter.drop_border);
        changed |= ImGui::Checkbox("renumerar por área", &op->filter.renumber_by_area);

        if (op->filter.only_label > 0) {
            ImGui::Spacing();
            ImGui::Text("isolado: componente %d", op->filter.only_label);
            ImGui::SameLine();
            if (ImGui::SmallButton("soltar")) {
                op->filter.only_label = 0;
                changed = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Limpar filtros", ImVec2(-1.0f, 0.0f))) {
            op->filter = ComponentFilter{};
            changed = true;
        }

        if (!window.all.empty()) {
            int menor = window.all.front().area;
            int maior = window.all.front().area;
            for (const Region& region : window.all) {
                menor = std::min(menor, region.area);
                maior = std::max(maior, region.area);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("áreas de %d a %d px", menor, maior);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Tudo que é da direita mora num filho só, senão o SameLine vale pro
    // primeiro widget e a tabela desce pra debaixo dos filtros.
    ImGui::BeginChild("direita", ImVec2(0.0f, 0.0f));
    ImGui::Checkbox("descritores", &window.show_descriptors);
    if (window.show_descriptors) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Perímetro sai do código de cadeia: passo reto vale 1, diagonal vale raiz de 2.\n"
                "Circularidade acima de 1 aparece em amarelo: a região é pequena demais pro\n"
                "perímetro digital valer, não é forma mais redonda que um círculo.");
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("copiar CSV")) {
        ImGui::SetClipboardText(shapes_to_csv(window.shapes).c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("salvar CSV...")) {
        std::string escolhido;
        if (pick_save_file("Salvar descritores", downloads_folder() + "/descritores.csv",
                           {{"CSV", {"*.csv"}}}, &escolhido)
            == PickResult::Chose) {
            std::ofstream saida(escolhido);
            saida << shapes_to_csv(window.shapes);
        }
    }

    // Quando um rótulo está isolado pelo filtro, vale mostrar tudo dele: numa
    // tabela os sete momentos de Hu não cabem, e sozinhos eles não dizem nada.
    if (const Shape* forma = [&]() -> const Shape* {
            for (std::size_t i = 0; i < window.kept.size(); ++i) {
                if (window.kept[i].label == op->filter.only_label && i < window.shapes.size()) {
                    return &window.shapes[i];
                }
            }
            return nullptr;
        }()) {
        ImGui::Separator();
        ImGui::Text("rótulo %d", op->filter.only_label);
        ImGui::SameLine();
        ImGui::TextDisabled("centro (%.1f, %.1f), caixa %dx%d, extensão %.3f", forma->cx,
                            forma->cy, forma->x1 - forma->x0 + 1, forma->y1 - forma->y0 + 1,
                            forma->extent);
        ImGui::TextDisabled("eixos %.1f e %.1f, casco %.0f px", forma->major, forma->minor,
                            forma->hull_area);
        ImGui::TextDisabled("hu %.4g %.4g %.4g %.4g %.4g %.4g %.4g", forma->hu[0], forma->hu[1],
                            forma->hu[2], forma->hu[3], forma->hu[4], forma->hu[5], forma->hu[6]);
        ImGui::Separator();
    }

    ImGui::BeginChild("lista", ImVec2(0.0f, 0.0f));
    {
        std::unordered_map<int, int> novo;
        for (std::size_t i = 0; i < window.kept.size(); ++i) {
            novo[window.kept[i].label] = static_cast<int>(i + 1);
        }

        std::vector<Region> ordenadas = window.all;
        std::sort(ordenadas.begin(), ordenadas.end(),
                  [](const Region& a, const Region& b) { return a.area > b.area; });

        const auto forma_de = [&window](int novo_rotulo) -> const Shape* {
            const std::size_t i = static_cast<std::size_t>(novo_rotulo) - 1;
            return (novo_rotulo > 0 && i < window.shapes.size()) ? &window.shapes[i] : nullptr;
        };

        const int colunas = window.show_descriptors ? 9 : 4;
        if (ImGui::BeginTable("componentes", colunas,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("orig");
            ImGui::TableSetupColumn("novo");
            ImGui::TableSetupColumn("área");
            ImGui::TableSetupColumn("borda");
            if (window.show_descriptors) {
                ImGui::TableSetupColumn("perím");
                ImGui::TableSetupColumn("circ");
                ImGui::TableSetupColumn("solidez");
                ImGui::TableSetupColumn("excentr");
                ImGui::TableSetupColumn("ângulo");
            }
            ImGui::TableHeadersRow();

            for (const Region& region : ordenadas) {
                const auto found = novo.find(region.label);
                const bool sobrou = found != novo.end();
                ImGui::TableNextRow();
                ImGui::PushID(region.label);

                ImGui::TableNextColumn();
                char rotulo[16];
                std::snprintf(rotulo, sizeof(rotulo), "%d", region.label);
                if (ImGui::Selectable(rotulo, op->filter.only_label == region.label,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    op->filter.only_label =
                        op->filter.only_label == region.label ? 0 : region.label;
                    changed = true;
                }

                ImGui::TableNextColumn();
                if (sobrou) {
                    ImGui::Text("%d", found->second);
                } else {
                    ImGui::TextDisabled("fora");
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", region.area);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", region.touches_border ? "sim" : "");

                if (window.show_descriptors) {
                    const Shape* forma = sobrou ? forma_de(found->second) : nullptr;
                    for (int k = 0; k < 5; ++k) {
                        ImGui::TableNextColumn();
                        if (!forma) {
                            ImGui::TextDisabled("");
                            continue;
                        }
                        switch (k) {
                            case 0: ImGui::Text("%.1f", forma->perimeter); break;
                            case 1:
                                // Acima de 1 é impossível numa forma de verdade:
                                // a região é pequena demais pro perímetro valer.
                                if (forma->circularity > 1.0) {
                                    ImGui::TextColored(ImVec4(0.85f, 0.7f, 0.4f, 1.0f), "%.3f",
                                                       forma->circularity);
                                } else {
                                    ImGui::Text("%.3f", forma->circularity);
                                }
                                break;
                            case 2: ImGui::Text("%.3f", forma->solidity); break;
                            case 3: ImGui::Text("%.3f", forma->eccentricity); break;
                            case 4: ImGui::Text("%.1f", forma->orientation); break;
                        }
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    if (changed) {
        app.evaluate();
    }

    ImGui::End();
}
