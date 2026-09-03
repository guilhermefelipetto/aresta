#include "components_window.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include <imgui.h>

#include "app.h"
#include "chain.h"

namespace {

// A janela refaz a conta com a mesma função que a cadeia usou, e não com uma
// regra própria, senão a lista e a imagem discordam.
void recompute(ComponentsWindow& window, App& app) {
    window.all.clear();
    window.kept.clear();

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

    ImGui::BeginChild("lista", ImVec2(0.0f, 0.0f));
    {
        std::unordered_map<int, int> novo;
        for (std::size_t i = 0; i < window.kept.size(); ++i) {
            novo[window.kept[i].label] = static_cast<int>(i + 1);
        }

        std::vector<Region> ordenadas = window.all;
        std::sort(ordenadas.begin(), ordenadas.end(),
                  [](const Region& a, const Region& b) { return a.area > b.area; });

        if (ImGui::BeginTable("componentes", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("orig");
            ImGui::TableSetupColumn("novo");
            ImGui::TableSetupColumn("área");
            ImGui::TableSetupColumn("borda");
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

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (changed) {
        app.evaluate();
    }

    ImGui::End();
}
