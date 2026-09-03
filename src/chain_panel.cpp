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
        {"plano de bit", BitPlaneOp{}, "um estágio de cor ou escalar"},
        {nullptr, SourceOp{}, nullptr},
        {"espectro", SpectrumOp{}, "um estágio de cor ou escalar"},
        {"filtro de frequência", FreqFilterOp{}, "um estágio de cor ou escalar"},
        {"compor", ComposeOp{}, "três estágios escalares"},
        {"alongar", StretchOp{}, "um estágio de cor ou escalar"},
        {nullptr, SourceOp{}, nullptr},
        {"canal", ChannelOp{}, "um estágio de cor"},
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
            int input = viewed_id;
            if (bridged) {
                input = app.chain.add(bridge, viewed_id);
            }
            app.viewed = app.chain.index_of(app.chain.add(entry.params, input));
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
                char preview[96];
                if (current_idx >= 0) {
                    std::snprintf(preview, sizeof(preview), "entrada: %d %s", current_idx,
                                  op_info(app.chain.stages[current_idx].params).name);
                } else {
                    std::snprintf(preview, sizeof(preview), "entrada: (nada)");
                }

                ImGui::PushID(k);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##entrada", preview)) {
                    for (std::size_t j = 0; j < i; ++j) {
                        const ValueKind produced = op_info(app.chain.stages[j].params).output;
                        const bool serve = (info.poly != Poly::None)
                                               ? poly_accepts(info.poly, produced)
                                               : (produced == info.inputs[k]);
                        if (!serve) {
                            continue;
                        }
                        char option[96];
                        std::snprintf(option, sizeof(option), "%zu %s", j,
                                      op_info(app.chain.stages[j].params).name);
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
                    int channel = static_cast<int>(op->channel);
                    if (ImGui::Combo("##p", &channel,
                                     "luminância\0vermelho\0verde\0azul\0máximo\0mínimo\0"
                                     "saturação\0")) {
                        op->channel = static_cast<Channel>(channel);
                        dirty = true;
                    }

                    if (op->channel == Channel::Luma) {
                        struct Preset {
                            const char* label;
                            float weight[3];
                            bool on_srgb;
                        };
                        // Cada padrão é o par peso mais espaço, não só os
                        // números: 601 sobre linear não é o luma que a
                        // literatura chama de 601.
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
                        const char* preview = chosen >= 0 ? presets[chosen].label : "à mão";
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::BeginCombo("##pesos", preview)) {
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
                        ImGui::TextDisabled("soma %.4f", op->weight[0] + op->weight[1] +
                                                             op->weight[2]);
                    }
                } else if (auto* op = std::get_if<RankOp>(&stage.params)) {
                    int kind = static_cast<int>(op->kind);
                    if (ImGui::Combo("##p", &kind,
                                     "mediana\0mínimo\0máximo\0ponto médio\0"
                                     "média alfa-cortada\0")) {
                        op->kind = static_cast<Rank>(kind);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::SliderFloat("##raio", &op->radius, 1.0f, 5.0f, "raio %.2f");
                    if (op->kind == Rank::AlphaTrimmed) {
                        ImGui::SetNextItemWidth(-1.0f);
                        dirty |= ImGui::SliderFloat("##alfa", &op->alpha, 0.0f, 0.9f,
                                                    "corta %.0f%%");
                    }
                } else if (auto* op = std::get_if<SpectrumOp>(&stage.params)) {
                    int pad = static_cast<int>(op->pad);
                    if (ImGui::Combo("##p", &pad, "espelhar\0zero\0")) {
                        op->pad = static_cast<Pad>(pad);
                        dirty = true;
                    }
                    dirty |= ImGui::Checkbox("escala log", &op->logarithmic);
                } else if (auto* op = std::get_if<FreqFilterOp>(&stage.params)) {
                    int shape = static_cast<int>(op->shape);
                    if (ImGui::Combo("##p", &shape, "ideal\0Butterworth\0gaussiano\0")) {
                        op->shape = static_cast<FreqShape>(shape);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    int kind = static_cast<int>(op->kind);
                    if (ImGui::Combo("##k", &kind,
                                     "passa-baixa\0passa-alta\0passa-faixa\0rejeita-faixa\0")) {
                        op->kind = static_cast<FreqKind>(kind);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::SliderFloat("##corte", &op->cutoff, 0.005f, 1.0f,
                                                "corte %.3f", ImGuiSliderFlags_Logarithmic);
                    if (op->shape == FreqShape::Butterworth) {
                        ImGui::SetNextItemWidth(-1.0f);
                        dirty |= ImGui::SliderInt("##ordem", &op->order, 1, 8, "ordem %d");
                    }
                    if (op->kind == FreqKind::BandPass || op->kind == FreqKind::BandReject) {
                        ImGui::SetNextItemWidth(-1.0f);
                        dirty |= ImGui::SliderFloat("##larg", &op->width, 0.005f, 0.5f,
                                                    "largura %.3f", ImGuiSliderFlags_Logarithmic);
                    }
                } else if (auto* op = std::get_if<NoiseOp>(&stage.params)) {
                    int kind = static_cast<int>(op->kind);
                    if (ImGui::Combo("##p", &kind,
                                     "gaussiano\0rayleigh\0gama\0exponencial\0uniforme\0"
                                     "sal e pimenta\0periódico\0")) {
                        op->kind = static_cast<Noise>(kind);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-90.0f);
                    switch (op->kind) {
                        case Noise::Gaussian:
                            dirty |= ImGui::DragFloat("média", &op->a, 0.002f, -1.0f, 1.0f, "%.3f");
                            ImGui::SetNextItemWidth(-90.0f);
                            dirty |= ImGui::DragFloat("desvio", &op->b, 0.002f, 0.0f, 1.0f, "%.3f");
                            break;
                        case Noise::SaltPepper:
                            dirty |= ImGui::DragFloat("pimenta", &op->a, 0.002f, 0.0f, 0.5f, "%.3f");
                            ImGui::SetNextItemWidth(-90.0f);
                            dirty |= ImGui::DragFloat("sal", &op->b, 0.002f, 0.0f, 0.5f, "%.3f");
                            break;
                        case Noise::Periodic:
                            dirty |= ImGui::DragFloat("amplitude", &op->a, 0.002f, 0.0f, 1.0f, "%.3f");
                            ImGui::SetNextItemWidth(-90.0f);
                            dirty |= ImGui::DragFloat("ciclos x", &op->b, 0.2f, 0.0f, 128.0f, "%.0f");
                            ImGui::SetNextItemWidth(-90.0f);
                            dirty |= ImGui::DragFloat("ciclos y", &op->c, 0.2f, 0.0f, 128.0f, "%.0f");
                            break;
                        default:
                            dirty |= ImGui::DragFloat("a", &op->a, 0.01f, 0.0f, 40.0f, "%.3f");
                            ImGui::SetNextItemWidth(-90.0f);
                            dirty |= ImGui::DragFloat("b", &op->b, 0.01f, 0.0f, 40.0f, "%.3f");
                            break;
                    }
                    if (op->kind != Noise::Periodic) {
                        ImGui::SetNextItemWidth(-90.0f);
                        dirty |= ImGui::DragInt("semente", &op->seed, 0.2f, 1, 9999);
                    }
                } else if (auto* op = std::get_if<MeanOp>(&stage.params)) {
                    int kind = static_cast<int>(op->kind);
                    if (ImGui::Combo("##p", &kind,
                                     "aritmética\0geométrica\0harmônica\0contra-harmônica\0")) {
                        op->kind = static_cast<Mean>(kind);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::SliderFloat("##raio", &op->radius, 1.0f, 5.0f, "raio %.2f");
                    if (op->kind == Mean::Contraharmonic) {
                        ImGui::SetNextItemWidth(-1.0f);
                        dirty |= ImGui::SliderFloat("##q", &op->q, -4.0f, 4.0f, "Q %+.2f");
                        ImGui::TextDisabled("Q > 0 tira pimenta, Q < 0 tira sal");
                    }
                } else if (auto* op = std::get_if<AdaptiveOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->radius, 1.0f, 5.0f, "raio %.2f");
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::DragFloat("##var", &op->noise_variance, 0.0002f, 0.0f, 1.0f,
                                              "variância %.5f");
                } else if (auto* op = std::get_if<AdaptiveMedianOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->max_radius, 1.5f, 6.0f,
                                                "raio máximo %.2f");
                } else if (auto* op = std::get_if<BitPlaneOp>(&stage.params)) {
                    dirty |= ImGui::SliderInt("##p", &op->plane, 0, 7, "bit %d");
                } else if (auto* op = std::get_if<CombineOp>(&stage.params)) {
                    int operation = static_cast<int>(op->operation);
                    if (ImGui::Combo("##p", &operation,
                                     "somar\0subtrair\0diferença absoluta\0multiplicar\0"
                                     "dividir\0mínimo\0máximo\0média\0")) {
                        op->operation = static_cast<Combine>(operation);
                        dirty = true;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::DragFloat("##escala", &op->scale, 0.01f, -8.0f, 8.0f,
                                              "escala %.3f");
                } else if (auto* op = std::get_if<CurveOp>(&stage.params)) {
                    dirty |= ImGui::InputText("##p", op->expression, sizeof(op->expression));
                } else if (auto* op = std::get_if<ClaheOp>(&stage.params)) {
                    dirty |= ImGui::SliderInt("##p", &op->tiles, 2, 32, "%d pedaços por lado");
                    ImGui::SetNextItemWidth(-1.0f);
                    dirty |= ImGui::SliderFloat("##clip", &op->clip, 1.0f, 8.0f, "recorte %.2f");
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
