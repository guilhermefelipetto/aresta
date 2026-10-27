#include "cooccurrence_window.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <imgui.h>

#include "app.h"

namespace {

// Uma frase por medida, dita em termos da matriz, que é o que está na tela.
constexpr const char* kExplica[haralick_count] = {
    "quanto a massa se afasta da diagonal, com peso no quadrado da distância",
    "o mesmo, com peso linear: um vizinho muito diferente pesa menos",
    "quanto a massa fica perto da diagonal; 1 é textura lisa",
    "soma dos quadrados: alta quando poucas células concentram tudo",
    "raiz da uniformidade, na mesma escala da probabilidade",
    "o quanto o nível do vizinho acompanha o do pixel, de -1 a 1",
    "o quanto a massa está espalhada, em bits; o contrário da uniformidade",
    "a célula mais cheia: o par de níveis que mais se repete",
};

long long assinatura(const CooccurrenceWindow& w) {
    return ((((static_cast<long long>(w.levels) * 64 + w.distance) * 8 +
              static_cast<int>(w.direction)) * 2 + w.symmetric) * 2 + w.unit_range) * 2 +
           w.log_scale;
}

void recompute(CooccurrenceWindow& window, const Value& value) {
    Map<float> luz;
    window.from_color = value.kind == ValueKind::Color;
    if (window.from_color) {
        luz = value.color.luma();
    }
    const MapView<float> fonte = window.from_color ? luz.view() : value.scalar.view();

    window.lo = 0.0f;
    window.hi = 1.0f;
    if (!window.unit_range) {
        scalar_range(fonte, &window.lo, &window.hi);
    }
    const Map<int32_t> q = quantize_levels(fonte, window.lo, window.hi, window.levels);
    window.matrix = cooccurrence(q.view(), window.levels, window.direction, window.distance,
                                 window.symmetric);
    haralick_of(q.view(), window.levels, window.direction, window.distance, window.symmetric,
                window.measures);

    // Em escala linear a diagonal de uma imagem lisa apaga todo o resto, do
    // mesmo jeito que o pico do fundo no histograma. Célula vazia fica um
    // degrau abaixo da menor cheia, pra ser a cor mais escura sem puxar a
    // escala pro infinito.
    const int L = window.levels;
    Map<float> imagem(L, L);
    double menor = 1.0;
    for (double p : window.matrix.p) {
        if (p > 0.0) {
            menor = std::min(menor, p);
        }
    }
    const float vazio = static_cast<float>(std::log10(menor)) - 0.5f;
    for (int i = 0; i < L; ++i) {
        for (int j = 0; j < L; ++j) {
            const double p = window.matrix.at(i, j);
            imagem.view().at(j, i) = window.log_scale
                                         ? (p > 0.0 ? static_cast<float>(std::log10(p)) : vazio)
                                         : static_cast<float>(p);
        }
    }
    float a = 0.0f, b = 0.0f;
    const auto rgba = to_display_rgba8(make_scalar(std::move(imagem)), Colormap::Viridis, &a, &b);
    upload_rgba8(window.texture, rgba.get(), L, L);
}

}  // namespace

void draw_cooccurrence_window(CooccurrenceWindow& window, App& app) {
    if (!window.open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(860.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Co-ocorrência", &window.open)) {
        ImGui::End();
        return;
    }

    const bool tem = app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.outputs.size()) &&
                     !app.chain.outputs[app.viewed].empty();
    if (!tem) {
        ImGui::TextDisabled("Nenhum estágio pra medir.");
        ImGui::End();
        return;
    }
    const Value& value = app.chain.outputs[app.viewed];
    if (value.kind == ValueKind::Label) {
        ImGui::TextWrapped("O estágio %d é de rótulo. Co-ocorrência mede como os níveis de "
                           "cinza se vizinham, e índice de região não é nível de cinza.",
                           app.viewed);
        ImGui::End();
        return;
    }

    if (window.seen_revision != app.revision || window.seen_stage != app.viewed ||
        window.seen_settings != assinatura(window)) {
        recompute(window, value);
        window.seen_revision = app.revision;
        window.seen_stage = app.viewed;
        window.seen_settings = assinatura(window);
    }

    ImGui::Text("estágio %d  %s", app.viewed, op_info(app.chain.stages[app.viewed].params).name);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s%s, níveis entre %.4f e %.4f)", kind_name(value.kind),
                        window.from_color ? ", pela luminância" : "", window.lo, window.hi);

    // Esquerda: a matriz. Direita: opções e medidas.
    const float lado = std::clamp(std::min(ImGui::GetContentRegionAvail().x * 0.5f,
                                           ImGui::GetContentRegionAvail().y - 60.0f),
                                  160.0f, 560.0f);
    ImGui::BeginGroup();
    const ImVec2 canto = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(window.texture.id), ImVec2(lado, lado));
    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const int L = window.levels;
        const int j = std::clamp(static_cast<int>((mouse.x - canto.x) / lado * L), 0, L - 1);
        const int i = std::clamp(static_cast<int>((mouse.y - canto.y) / lado * L), 0, L - 1);
        ImGui::SetTooltip("pixel no nível %d, vizinho no nível %d\np = %.6f", i, j,
                          window.matrix.at(i, j));
    }
    ImGui::TextDisabled("linha: nível do pixel  coluna: nível do vizinho");
    ImGui::TextDisabled("diagonal cheia é vizinho parecido; longe dela, contraste");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushItemWidth(170.0f);
    int direcao = static_cast<int>(window.direction);
    if (ImGui::Combo("direção", &direcao,
                     "direita\0abaixo\0diagonal descendo\0diagonal subindo\0média das quatro\0")) {
        window.direction = static_cast<Direction>(direcao);
    }
    ImGui::SliderInt("distância", &window.distance, 1, 8);
    ImGui::SliderInt("níveis", &window.levels, 2, 256, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::PopItemWidth();
    ImGui::Checkbox("simétrica", &window.symmetric);
    ImGui::SameLine();
    ImGui::Checkbox("faixa fixa 0 a 1", &window.unit_range);
    ImGui::SameLine();
    ImGui::Checkbox("escala log", &window.log_scale);
    if (window.direction == Direction::Average) {
        ImGui::TextDisabled("a matriz é a média das quatro; as medidas são a média das");
        ImGui::TextDisabled("quatro medidas, que não é a mesma coisa");
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("medidas", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (int k = 0; k < haralick_count; ++k) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(haralick_name(static_cast<Haralick>(k)), window.chosen == k,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                window.chosen = k;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kExplica[k]);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.5f", window.measures[k]);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("passe o mouse numa medida pra ver o que ela olha");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderInt("raio da janela", &window.radius, 1, 32);
    char rotulo[96];
    std::snprintf(rotulo, sizeof(rotulo), "Mapa de %s",
                  haralick_name(static_cast<Haralick>(window.chosen)));
    // Estágio de textura quer escalar. Em cor, quem resolve é o canal, e a
    // cadeia já sabe pendurar a ponte sozinha.
    if (ImGui::Button(rotulo)) {
        TextureOp op;
        op.measure = static_cast<Haralick>(window.chosen);
        op.direction = window.direction;
        op.distance = window.distance;
        op.levels = window.levels;
        op.radius = window.radius;
        op.symmetric = window.symmetric;
        op.unit_range = window.unit_range;
        int fonte = app.chain.stages[app.viewed].id;
        if (value.kind == ValueKind::Color) {
            fonte = app.chain.add(ChannelOp{}, fonte);
        }
        app.viewed = app.chain.index_of(app.chain.add(op, fonte));
        app.evaluate();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("a medida escolhida, pixel a pixel");
    ImGui::EndGroup();

    ImGui::End();
}
