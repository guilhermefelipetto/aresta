#include "chain_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "app.h"
#include "chain.h"

namespace {

// Onde cada estágio ficou na tela, pra desenhar os fios depois que a lista
// inteira já foi submetida.
struct Row {
    int id;
    float y;
};

// Um fio por ligação. `lane` é a coluna que ele ocupa na margem: ligação entre
// estágios vizinhos fica rente, ligação que pula gente sai mais pra fora pra
// não passar por cima dos pontos do meio.
struct Link {
    int from;
    int to;
    int lane;
    bool highlight;
};

std::vector<Link> build_links(const Chain& chain, int viewed) {
    std::vector<Link> links;
    for (std::size_t i = 0; i < chain.stages.size(); ++i) {
        for (int input : chain.stages[i].inputs) {
            const int from = chain.index_of(input);
            if (from < 0) {
                continue;
            }
            links.push_back({from, static_cast<int>(i), 0,
                             static_cast<int>(i) == viewed || from == viewed});
        }
    }

    // Guloso: cada fio pega a coluna mais interna que ninguém sobreposto usa.
    std::sort(links.begin(), links.end(), [](const Link& a, const Link& b) {
        return (a.to - a.from) < (b.to - b.from);
    });
    for (std::size_t i = 0; i < links.size(); ++i) {
        for (int lane = 0;; ++lane) {
            bool livre = true;
            for (std::size_t j = 0; j < i && livre; ++j) {
                livre = links[j].lane != lane || links[j].to <= links[i].from ||
                        links[j].from >= links[i].to;
            }
            if (livre) {
                links[i].lane = lane;
                break;
            }
        }
    }
    return links;
}

void draw_links(const std::vector<Link>& links, const std::vector<Row>& rows, float dot_x) {
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (const Link& link : links) {
        if (link.from >= static_cast<int>(rows.size()) ||
            link.to >= static_cast<int>(rows.size())) {
            continue;
        }
        const float y0 = rows[static_cast<std::size_t>(link.from)].y;
        const float y1 = rows[static_cast<std::size_t>(link.to)].y;
        const ImU32 tint = link.highlight ? IM_COL32(120, 170, 240, 255) : IM_COL32(80, 80, 92, 255);

        if (link.lane == 0) {
            draw->AddLine(ImVec2(dot_x, y0), ImVec2(dot_x, y1), tint, 1.6f);
            continue;
        }
        const float bow = dot_x - 7.0f * static_cast<float>(link.lane);
        const float pull = std::min(24.0f, (y1 - y0) * 0.35f);
        draw->AddBezierCubic(ImVec2(dot_x, y0), ImVec2(bow, y0 + pull), ImVec2(bow, y1 - pull),
                             ImVec2(dot_x, y1), tint, 1.6f);
    }

    for (const Row& row : rows) {
        draw->AddCircleFilled(ImVec2(dot_x, row.y), 3.5f, IM_COL32(150, 150, 165, 255));
    }
}

}  // namespace

bool draw_operation_items(App& app) {
    struct Entry {
        const char* label;
        OpParams params;
        const char* needs;
    };

    const Entry entries[] = {
        {"exposição", ExposureOp{}, "um estágio de cor"},
        {"contraste", ContrastOp{}, "um estágio de cor"},
        {"gama", GammaOp{}, "um estágio de cor"},
        {"inverter", InvertOp{}, "um estágio de cor"},
        {nullptr, SourceOp{}, nullptr},
        {"convolução", ConvolveOp{}, "um estágio de cor ou escalar"},
        {"equalizar", EqualizeOp{}, "um estágio de cor ou escalar"},
        {"clahe", ClaheOp{}, "um estágio de cor ou escalar"},
        {"curva", CurveOp{}, "um estágio de cor ou escalar"},
        {"combinar", CombineOp{}, "dois estágios do mesmo tipo"},
        {"casar histograma", MatchOp{}, "dois estágios de cor ou escalar"},
        {"ordem", RankOp{}, "um estágio de cor ou escalar"},
        {"ruído", NoiseOp{}, "um estágio de cor ou escalar"},
        {"média", MeanOp{}, "um estágio de cor ou escalar"},
        {"redução adaptativa", AdaptiveOp{}, "um estágio de cor ou escalar"},
        {"mediana adaptativa", AdaptiveMedianOp{}, "um estágio de cor ou escalar"},
        {"degradar", DegradeOp{}, "um estágio de cor ou escalar"},
        {"restaurar", RestoreOp{}, "um estágio de cor ou escalar"},
        {"plano de bit", BitPlaneOp{}, "um estágio de cor ou escalar"},
        {nullptr, SourceOp{}, nullptr},
        {"espectro", SpectrumOp{}, "um estágio de cor ou escalar"},
        {"filtro de frequência", FreqFilterOp{}, "um estágio de cor ou escalar"},
        {"compor", ComposeOp{}, "três estágios escalares"},
        {"alongar", StretchOp{}, "um estágio de cor ou escalar"},
        {nullptr, SourceOp{}, nullptr},
        {"canal", ChannelOp{}, "um estágio de cor"},
        {"gradiente de cor", ColorGradientOp{}, "um estágio de cor"},
        {"distância de cor", ColorDistanceOp{}, "um estágio de cor"},
        {"pseudo-cor", PseudoColorOp{}, "um estágio escalar"},
        {"threshold", ThresholdOp{}, "um estágio escalar, tipo luminância"},
        {"morfologia", MorphologyOp{}, "um estágio escalar ou de rótulo"},
        {"componentes", ComponentsOp{}, "um estágio de rótulo, tipo threshold"},
        {"overlay", OverlayOp{}, "um estágio de cor e um de rótulo"},
    };

    const int viewed_id = (app.viewed >= 0 &&
                           app.viewed < static_cast<int>(app.chain.stages.size()))
                              ? app.chain.stages[app.viewed].id
                              : -1;

    bool added = false;
    for (const Entry& entry : entries) {
        if (!entry.label) {
            ImGui::Separator();
            continue;
        }

        const bool ready = app.chain.can_add(entry.params);
        OpParams bridge = SourceOp{};
        const bool bridged = !ready && bridge_for(app.chain, entry.params, &bridge);
        const char* via = bridged ? op_info(bridge).name : nullptr;
        char shortcut[48] = {};
        if (via) {
            std::snprintf(shortcut, sizeof(shortcut), "via %s", via);
        }

        if (ImGui::MenuItem(entry.label, via ? shortcut : nullptr, false, ready || bridged)) {
            // Entra logo depois do que está selecionado, não no fim: quem
            // acrescenta operação quer continuar dali.
            int depois = app.viewed + 1;
            int input = viewed_id;
            if (bridged) {
                input = app.chain.add(bridge, viewed_id, depois);
                ++depois;
            }
            app.viewed = app.chain.index_of(app.chain.add(entry.params, input, depois));
            added = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (bridged) {
                ImGui::SetTooltip("acrescenta um estágio de %s antes, e deixa ele na cadeia\n"
                                  "pra você trocar o canal ou o limiar depois",
                                  via);
            } else if (!ready) {
                ImGui::SetTooltip("precisa de %s antes", entry.needs);
            }
        }
    }
    return added;
}



void draw_chain_panel(App& app, bool dirty_from_outside) {
    ImGui::Begin("Cadeia");
    if (app.source.empty()) {
        ImGui::TextDisabled("Nenhuma imagem.");
    } else {
        bool dirty = dirty_from_outside;

        if (ImGui::Button("Adicionar operação", ImVec2(-1.0f, 0.0f))) {
            ImGui::OpenPopup("adicionar");
        }
        if (ImGui::BeginPopup("adicionar")) {
            if (draw_operation_items(app)) {
                dirty = true;
            }
            ImGui::EndPopup();
        }

        ImGui::Checkbox("abrir só o selecionado", &app.chain_compact);

        ImGui::Spacing();
        int to_remove = -1;

        const std::vector<Link> links = build_links(app.chain, app.viewed);
        int lanes = 0;
        for (const Link& link : links) {
            lanes = std::max(lanes, link.lane);
        }
        const float gutter = 14.0f + 7.0f * static_cast<float>(lanes);
        const float panel_x = ImGui::GetCursorScreenPos().x;
        std::vector<Row> rows;
        rows.reserve(app.chain.stages.size());

        for (std::size_t i = 0; i < app.chain.stages.size(); ++i) {
            Stage& stage = app.chain.stages[i];
            const OpInfo info = op_info(stage.params);
            ImGui::PushID(stage.id);

            const ValueKind produced = (i < app.chain.outputs.size() &&
                                        !app.chain.outputs[i].empty())
                                           ? app.chain.outputs[i].kind
                                           : info.output;
            char label[160];
            std::snprintf(label, sizeof(label), "%zu   %-11s -> %s", i, info.name,
                          kind_name(produced));
            ImGui::Indent(gutter);

            const bool is_viewed = app.viewed == static_cast<int>(i);
            const bool is_pinned = app.pinned == stage.id;
            const float pin_width = 38.0f;

            if (ImGui::Selectable(label, is_viewed,
                                  ImGuiSelectableFlags_None,
                                  ImVec2(ImGui::GetContentRegionAvail().x - pin_width, 0.0f))) {
                app.viewed = static_cast<int>(i);
                app.upload_view();
            }
            if (is_viewed && dirty) {
                ImGui::SetScrollHereY(0.6f);
            }
            rows.push_back({stage.id,
                            (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f});

            ImGui::SameLine();
            if (is_pinned) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            }
            if (ImGui::SmallButton("ver")) {
                app.pinned = is_pinned ? -1 : stage.id;
                app.upload_view();
            }
            if (is_pinned) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(is_pinned ? "solta a tela, ela volta a seguir a seleção"
                                            : "prende a tela neste estágio");
            }

            ImGui::Indent();
            const bool aberto = is_viewed || !app.chain_compact;

            const std::string resumo = stage_summary(stage.params);
            if (!resumo.empty()) {
                ImGui::TextDisabled("%s", resumo.c_str());
            }

            for (int k = 0; aberto && k < info.input_count; ++k) {
                const int current =
                    (k < static_cast<int>(stage.inputs.size())) ? stage.inputs[k] : -1;
                const int current_idx = app.chain.index_of(current);
                const char* slot = input_label(stage.params, k);

                char preview[128];
                if (current_idx >= 0) {
                    std::snprintf(preview, sizeof(preview), "%s: %d %s", slot, current_idx,
                                  op_info(app.chain.stages[current_idx].params).name);
                } else {
                    std::snprintf(preview, sizeof(preview), "%s: (nada)", slot);
                }

                ImGui::PushID(k);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##entrada", preview)) {
                    for (std::size_t j = 0; j < i; ++j) {
                        if (!app.chain.can_feed(info, k, static_cast<int>(j))) {
                            continue;
                        }

                        std::string detalhe = stage_summary(app.chain.stages[j].params);
                        if (detalhe.empty() && app.chain.stages[j].inputs.size() == 1) {
                            const int veio = app.chain.index_of(app.chain.stages[j].inputs[0]);
                            if (veio >= 0) {
                                detalhe = "de " + std::to_string(veio) + " " +
                                          op_info(app.chain.stages[veio].params).name;
                            }
                        }

                        char option[192];
                        std::snprintf(option, sizeof(option), "%zu %s%s%s", j,
                                      op_info(app.chain.stages[j].params).name,
                                      detalhe.empty() ? "" : "   ", detalhe.c_str());
                        if (ImGui::Selectable(option, app.chain.stages[j].id == current)) {
                            if (k < static_cast<int>(stage.inputs.size())) {
                                stage.inputs[k] = app.chain.stages[j].id;
                                dirty = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }

            if (aberto) {
                ImGui::SetNextItemWidth(-1.0f);
                if (auto* op = std::get_if<ExposureOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->stops, -3.0f, 3.0f, "%.2f EV");
                } else if (auto* op = std::get_if<ContrastOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->amount, -0.9f, 2.0f, "%.2f");
                } else if (auto* op = std::get_if<GammaOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->gamma, 0.2f, 4.0f, "%.2f");
                } else if (auto* op = std::get_if<ThresholdOp>(&stage.params)) {
                    ImGui::BeginDisabled(op->otsu);
                    dirty |= ImGui::DragFloat("##p", &op->level, 0.002f, -20.0f, 20.0f, "%.4f");
                    ImGui::EndDisabled();
                    dirty |= ImGui::Checkbox("Otsu", &op->otsu);
                } else if (auto* op = std::get_if<OverlayOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->opacity, 0.0f, 1.0f, "%.2f");
                } else if (auto* op = std::get_if<ChannelOp>(&stage.params)) {
                    int space = static_cast<int>(op->space);
                    if (ImGui::Combo("##p", &space, "RGB\0HSV\0HSI\0Lab\0YCbCr\0CMY\0")) {
                        op->space = static_cast<Space>(space);
                        if (op->space != Space::RGB && op->component == channel_luma) {
                            op->component = 0;
                        }
                        dirty = true;
                    }

                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##comp",
                                          op->component == channel_luma
                                              ? "luminância"
                                              : component_name(op->space, op->component))) {
                        for (int i = 0; i < 3; ++i) {
                            if (ImGui::Selectable(component_name(op->space, i),
                                                  op->component == i)) {
                                op->component = i;
                                dirty = true;
                            }
                        }
                        if (op->space == Space::RGB &&
                            ImGui::Selectable("luminância", op->component == channel_luma)) {
                            op->component = channel_luma;
                            dirty = true;
                        }
                        ImGui::EndCombo();
                    }

                    if (op->space == Space::RGB && op->component == channel_luma) {
                        struct Preset {
                            const char* label;
                            float weight[3];
                            bool on_srgb;
                        };
                        static const Preset presets[] = {
                            {"Rec. 709 (linear)", {0.2126f, 0.7152f, 0.0722f}, false},
                            {"Rec. 601 (gama)", {0.299f, 0.587f, 0.114f}, true},
                            {"Rec. 2020 (linear)", {0.2627f, 0.6780f, 0.0593f}, false},
                            {"média simples", {1.0f / 3, 1.0f / 3, 1.0f / 3}, false},
                        };
                        int chosen = -1;
                        for (int p = 0; p < 4; ++p) {
                            if (op->on_srgb == presets[p].on_srgb &&
                                std::fabs(op->weight[0] - presets[p].weight[0]) < 1e-4f &&
                                std::fabs(op->weight[1] - presets[p].weight[1]) < 1e-4f &&
                                std::fabs(op->weight[2] - presets[p].weight[2]) < 1e-4f) {
                                chosen = p;
                                break;
                            }
                        }
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo("##pesos",
                                              chosen >= 0 ? presets[chosen].label : "à mão")) {
                            for (int p = 0; p < 4; ++p) {
                                if (ImGui::Selectable(presets[p].label, chosen == p)) {
                                    for (int ch = 0; ch < 3; ++ch) {
                                        op->weight[ch] = presets[p].weight[ch];
                                    }
                                    op->on_srgb = presets[p].on_srgb;
                                    dirty = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SetNextItemWidth(-1.0f);
                        dirty |= ImGui::DragFloat3("##w", op->weight, 0.002f, -2.0f, 2.0f, "%.4f");
                        dirty |= ImGui::Checkbox("sobre sRGB", &op->on_srgb);
                        ImGui::SameLine();
                        ImGui::TextDisabled("soma %.4f",
                                            op->weight[0] + op->weight[1] + op->weight[2]);
                    } else {
                        float lo = 0.0f;
                        float hi = 1.0f;
                        component_range(op->space, op->component, &lo, &hi);
                        ImGui::TextDisabled("faixa natural %.0f a %.0f", lo, hi);
                    }
                } else if (auto* op = std::get_if<ComposeOp>(&stage.params)) {
                    int space = static_cast<int>(op->space);
                    if (ImGui::Combo("##p", &space, "RGB\0HSV\0HSI\0Lab\0YCbCr\0CMY\0")) {
                        op->space = static_cast<Space>(space);
                        dirty = true;
                    }
                    ImGui::TextDisabled("%s, %s, %s", component_name(op->space, 0),
                                        component_name(op->space, 1),
                                        component_name(op->space, 2));
                } else if (auto* op = std::get_if<ColorGradientOp>(&stage.params)) {
                    int space = static_cast<int>(op->space);
                    if (ImGui::Combo("##p", &space, "RGB\0HSV\0HSI\0Lab\0YCbCr\0CMY\0")) {
                        op->space = static_cast<Space>(space);
                        dirty = true;
                    }
                } else if (auto* op = std::get_if<ColorDistanceOp>(&stage.params)) {
                    int space = static_cast<int>(op->space);
                    if (ImGui::Combo("##p", &space, "RGB\0HSV\0HSI\0Lab\0YCbCr\0CMY\0")) {
                        op->space = static_cast<Space>(space);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::ColorEdit3("##alvo", op->reference,
                                               ImGuiColorEditFlags_NoInputs |
                                                   ImGuiColorEditFlags_PickerHueWheel);
                    ImGui::TextDisabled("a cor de referência é dada em sRGB");
                } else if (auto* op = std::get_if<PseudoColorOp>(&stage.params)) {
                    int map = static_cast<int>(op->map);
                    if (ImGui::Combo("##p", &map, "cinza\0viridis\0magma\0turbo\0quente\0")) {
                        op->map = static_cast<Colormap>(map);
                        dirty = true;
                    }
                } else if (auto* op = std::get_if<StretchOp>(&stage.params)) {
                    dirty |= ImGui::DragFloatRange2("##p", &op->low, &op->high, 0.05f, 0.0f, 100.0f,
                                                    "%.2f", "%.2f");
                } else if (auto* op = std::get_if<ComponentsOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->radius, 1.0f, 3.0f, "raio %.2f");
                    ImGui::TextDisabled("filtrar em Ferramentas > Componentes");
                } else if (auto* op = std::get_if<MorphologyOp>(&stage.params)) {
                    int operation = static_cast<int>(op->operation);
                    if (ImGui::Combo("##p", &operation,
                                     "erosão\0dilatação\0abertura\0fechamento\0gradiente\0"
                                     "top-hat\0black-hat\0")) {
                        op->operation = static_cast<Morph>(operation);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::SliderFloat("##raio", &op->radius, 1.0f, 6.0f, "raio %.2f");
            }

            }

            if (aberto && stage.id != 0) {
                if (ImGui::Checkbox("ativo", &stage.enabled)) {
                    dirty = true;
                }
                const bool tem_janela = std::get_if<ConvolveOp>(&stage.params) ||
                                        std::get_if<CurveOp>(&stage.params) ||
                                        std::get_if<ComponentsOp>(&stage.params);
                if (tem_janela) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("editar")) {
                        app.edit_request = stage.id;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("sobe")) {
                    if (app.chain.move_stage(stage.id, -1)) {
                        app.viewed = app.chain.index_of(stage.id);
                        dirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("desce")) {
                    if (app.chain.move_stage(stage.id, 1)) {
                        app.viewed = app.chain.index_of(stage.id);
                        dirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("remover")) {
                    to_remove = stage.id;
                }
            }

            if (!stage.error.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", stage.error.c_str());
            } else if (!stage.note.empty()) {
                ImGui::TextDisabled("%s", stage.note.c_str());
            }

            ImGui::Unindent();
            ImGui::Unindent(gutter);
            ImGui::Separator();
            ImGui::PopID();
        }

        draw_links(links, rows, panel_x + gutter - 9.0f);

        if (to_remove >= 0) {
            if (app.pinned == to_remove) {
                app.pinned = -1;
            }
            app.chain.remove(to_remove);
            if (app.viewed >= static_cast<int>(app.chain.stages.size())) {
                app.viewed = static_cast<int>(app.chain.stages.size()) - 1;
            }
            dirty = true;
        }
        if (dirty) {
            app.evaluate();
        }
    }
    ImGui::End();
}
