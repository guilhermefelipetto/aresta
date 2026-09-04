#include "sheet.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include <imgui.h>
#include <imgui_internal.h>  // ImTextCharFromUtf8

#include "app.h"
#include "chain.h"
#include "value.h"

#include <stb_image_write.h>

namespace {

struct Cor {
    unsigned char r, g, b, a;
};

Cor cor_do_fundo(SheetBackground background) {
    switch (background) {
        case SheetBackground::Transparent: return {0, 0, 0, 0};
        case SheetBackground::White: return {255, 255, 255, 255};
        case SheetBackground::Light: return {240, 240, 242, 255};
        case SheetBackground::Dark: return {15, 15, 17, 255};
    }
    return {0, 0, 0, 0};
}

// Legenda tem que se ler contra o fundo escolhido. Num fundo transparente o
// arquivo pode cair em qualquer lugar, e cinza médio é o que menos some.
Cor cor_do_texto(SheetBackground background) {
    switch (background) {
        case SheetBackground::Transparent: return {128, 128, 134, 255};
        case SheetBackground::White: return {70, 70, 76, 255};
        case SheetBackground::Light: return {70, 70, 76, 255};
        case SheetBackground::Dark: return {154, 154, 162, 255};
    }
    return {128, 128, 128, 255};
}

Cor cor_do_filete(SheetBackground background) {
    switch (background) {
        case SheetBackground::Transparent: return {128, 128, 134, 160};
        case SheetBackground::White: return {190, 190, 196, 255};
        case SheetBackground::Light: return {200, 200, 206, 255};
        case SheetBackground::Dark: return {60, 60, 68, 255};
    }
    return {128, 128, 128, 255};
}

void pinta(Sheet& folha, int x, int y, Cor cor, float alpha) {
    if (x < 0 || y < 0 || x >= folha.width || y >= folha.height || alpha <= 0.0f) {
        return;
    }
    unsigned char* destino = &folha.pixels[(static_cast<std::size_t>(y) * folha.width + x) * 4];

    // Src-over com alfa reto. O destino pode estar transparente, então a cor
    // dele não pode entrar na conta com peso cheio.
    const float as = alpha;
    const float ad = destino[3] / 255.0f;
    const float saida = as + ad * (1.0f - as);
    if (saida <= 0.0f) {
        destino[0] = destino[1] = destino[2] = destino[3] = 0;
        return;
    }
    const unsigned char origem[3] = {cor.r, cor.g, cor.b};
    for (int c = 0; c < 3; ++c) {
        const float v = (origem[c] * as + destino[c] * ad * (1.0f - as)) / saida;
        destino[c] = static_cast<unsigned char>(std::lround(std::clamp(v, 0.0f, 255.0f)));
    }
    destino[3] = static_cast<unsigned char>(std::lround(saida * 255.0f));
}

// Média de área quando encolhe, bilinear quando estica. Encolher com bilinear
// pula pixel e serrilha, que numa figura de artigo aparece.
void redimensiona(const unsigned char* origem, int ow, int oh, unsigned char* destino, int dw,
                  int dh) {
    const bool encolhendo = dw < ow || dh < oh;
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            float soma[4] = {0, 0, 0, 0};
            if (encolhendo) {
                const int x0 = std::max(0, x * ow / dw);
                const int x1 = std::max(x0 + 1, (x + 1) * ow / dw);
                const int y0 = std::max(0, y * oh / dh);
                const int y1 = std::max(y0 + 1, (y + 1) * oh / dh);
                int n = 0;
                for (int j = y0; j < std::min(y1, oh); ++j) {
                    for (int i = x0; i < std::min(x1, ow); ++i) {
                        const unsigned char* p = &origem[(static_cast<std::size_t>(j) * ow + i) * 4];
                        for (int c = 0; c < 4; ++c) {
                            soma[c] += p[c];
                        }
                        ++n;
                    }
                }
                for (int c = 0; c < 4; ++c) {
                    soma[c] /= static_cast<float>(std::max(n, 1));
                }
            } else {
                const float fx = (x + 0.5f) * ow / dw - 0.5f;
                const float fy = (y + 0.5f) * oh / dh - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, ow - 1);
                const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, oh - 1);
                const int x1 = std::min(x0 + 1, ow - 1);
                const int y1 = std::min(y0 + 1, oh - 1);
                const float tx = std::clamp(fx - x0, 0.0f, 1.0f);
                const float ty = std::clamp(fy - y0, 0.0f, 1.0f);
                for (int c = 0; c < 4; ++c) {
                    const float a = origem[(static_cast<std::size_t>(y0) * ow + x0) * 4 + c];
                    const float b = origem[(static_cast<std::size_t>(y0) * ow + x1) * 4 + c];
                    const float d = origem[(static_cast<std::size_t>(y1) * ow + x0) * 4 + c];
                    const float e = origem[(static_cast<std::size_t>(y1) * ow + x1) * 4 + c];
                    soma[c] = (a + (b - a) * tx) * (1.0f - ty) + (d + (e - d) * tx) * ty;
                }
            }
            unsigned char* p = &destino[(static_cast<std::size_t>(y) * dw + x) * 4];
            for (int c = 0; c < 4; ++c) {
                p[c] = static_cast<unsigned char>(std::lround(std::clamp(soma[c], 0.0f, 255.0f)));
            }
        }
    }
}

float largura_do_texto(const char* texto) {
    return ImGui::CalcTextSize(texto).x;
}

// A legenda sai da fonte do próprio ImGui, glifo por glifo do atlas. Assim a
// figura sai com a mesma letra da interface e não entra dependência de
// rasterizador.
// Alfa do atlas em coordenada contínua. O atlas pode estar assado em
// densidade maior que a da tela, e aí amostrar pelo vizinho serrilha.
float amostra_alfa(const unsigned char* pixels, int aw, int ah, int bpp, float x, float y) {
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, aw - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, ah - 1);
    const int x1 = std::min(x0 + 1, aw - 1);
    const int y1 = std::min(y0 + 1, ah - 1);
    const float tx = std::clamp(x - x0, 0.0f, 1.0f);
    const float ty = std::clamp(y - y0, 0.0f, 1.0f);

    auto em = [&](int i, int j) {
        const unsigned char* p = pixels + (static_cast<std::size_t>(j) * aw + i) * bpp;
        return (bpp == 1 ? p[0] : p[3]) / 255.0f;
    };
    const float a = em(x0, y0);
    const float b = em(x1, y0);
    const float c = em(x0, y1);
    const float d = em(x1, y1);
    return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
}

// A legenda sai da fonte do próprio ImGui, glifo por glifo do atlas. Assim a
// figura sai com a mesma letra da interface e não entra dependência de
// rasterizador.
void escreve(Sheet& folha, float x, float y, const char* texto, Cor cor) {
    ImFontBaked* fonte = ImGui::GetFontBaked();
    if (!fonte) {
        return;
    }

    // Pedir glifo que ainda não foi assado pode fazer o atlas crescer e ser
    // reempacotado, o que invalida ponteiro e UV lidos antes. Assa tudo
    // primeiro, lê depois.
    for (const char* p = texto; *p;) {
        unsigned int ponto = 0;
        const int passo = ImTextCharFromUtf8(&ponto, p, nullptr);
        p += passo > 0 ? passo : 1;
        if (ponto == 0) {
            break;
        }
        fonte->FindGlyph(static_cast<ImWchar>(ponto));
    }

    const ImTextureData* atlas = ImGui::GetIO().Fonts->TexData;
    if (!atlas || !atlas->Pixels) {
        return;
    }
    const int aw = atlas->Width;
    const int ah = atlas->Height;
    const int bpp = atlas->BytesPerPixel;

    const char* p = texto;
    while (*p) {
        unsigned int ponto = 0;
        const int passo = ImTextCharFromUtf8(&ponto, p, nullptr);
        p += passo > 0 ? passo : 1;
        if (ponto == 0) {
            break;
        }
        const ImFontGlyph* glifo = fonte->FindGlyph(static_cast<ImWchar>(ponto));
        if (!glifo) {
            continue;
        }

        // O retângulo no atlas não mede o mesmo que o retângulo na folha, então
        // quem manda no laço é o destino e a origem vem por UV.
        const int dw = std::max(1, static_cast<int>(std::lround(glifo->X1 - glifo->X0)));
        const int dh = std::max(1, static_cast<int>(std::lround(glifo->Y1 - glifo->Y0)));
        const int px = static_cast<int>(std::lround(x + glifo->X0));
        const int py = static_cast<int>(std::lround(y + glifo->Y0));
        for (int j = 0; j < dh; ++j) {
            for (int i = 0; i < dw; ++i) {
                const float u = glifo->U0 + (glifo->U1 - glifo->U0) * (i + 0.5f) / dw;
                const float v = glifo->V0 + (glifo->V1 - glifo->V0) * (j + 0.5f) / dh;
                const float alfa =
                    amostra_alfa(atlas->Pixels, aw, ah, bpp, u * aw - 0.5f, v * ah - 0.5f);
                pinta(folha, px + i, py + j, cor, alfa);
            }
        }
        x += glifo->AdvanceX;
    }
}

struct Quadro {
    std::unique_ptr<unsigned char[]> pixels;
    int width = 0;
    int height = 0;
    std::string label;
};

}  // namespace

const char* sheet_background_name(SheetBackground background) {
    switch (background) {
        case SheetBackground::Transparent: return "transparente";
        case SheetBackground::White: return "branco";
        case SheetBackground::Light: return "claro";
        case SheetBackground::Dark: return "escuro";
    }
    return "?";
}

std::vector<SheetItem> sheet_items_from(const App& app) {
    std::vector<SheetItem> itens;
    for (const Stage& stage : app.chain.stages) {
        SheetItem item;
        item.stage_id = stage.id;
        const std::string resumo = stage_summary(stage.params);
        const std::string nome = op_info(stage.params).name;
        std::snprintf(item.label, sizeof(item.label), "%s",
                      resumo.empty() ? nome.c_str() : (nome + ", " + resumo).c_str());
        itens.push_back(item);
    }
    return itens;
}

Sheet compose_sheet(const App& app, const std::vector<SheetItem>& items,
                    const SheetLayout& layout) {
    Sheet folha;

    std::vector<Quadro> quadros;
    for (const SheetItem& item : items) {
        if (!item.on) {
            continue;
        }
        const int indice = app.chain.index_of(item.stage_id);
        if (indice < 0 || indice >= static_cast<int>(app.chain.outputs.size())) {
            continue;
        }
        const Value& valor = app.chain.outputs[indice];
        if (valor.empty()) {
            continue;
        }

        float lo = 0.0f;
        float hi = 0.0f;
        std::unique_ptr<unsigned char[]> rgba = to_display_rgba8(valor, app.colormap, &lo, &hi);
        const int ow = valor.width();
        const int oh = valor.height();
        if (!rgba || ow <= 0 || oh <= 0) {
            continue;
        }

        Quadro quadro;
        quadro.width = std::max(1, layout.cell_width);
        quadro.height = std::max(1, static_cast<int>(std::lround(
                                        static_cast<double>(oh) * quadro.width / ow)));
        quadro.pixels = std::make_unique<unsigned char[]>(
            static_cast<std::size_t>(quadro.width) * quadro.height * 4);
        redimensiona(rgba.get(), ow, oh, quadro.pixels.get(), quadro.width, quadro.height);
        quadro.label = item.label;
        quadros.push_back(std::move(quadro));
    }

    if (quadros.empty()) {
        return folha;
    }

    // Colunas é teto, não largura fixa: com três quadros e o limite em cinco,
    // a folha sai com três, senão sobraria margem paga por célula vazia.
    const int pedido = layout.columns > 0 ? layout.columns : static_cast<int>(quadros.size());
    const int colunas = std::min(pedido, static_cast<int>(quadros.size()));
    const int linhas = (static_cast<int>(quadros.size()) + colunas - 1) / colunas;
    const float altura_texto = ImGui::GetTextLineHeight();
    const int faixa = layout.labels ? static_cast<int>(altura_texto) + layout.label_gap : 0;

    std::vector<int> altura_da_linha(linhas, 0);
    for (std::size_t i = 0; i < quadros.size(); ++i) {
        const int linha = static_cast<int>(i) / colunas;
        altura_da_linha[linha] = std::max(altura_da_linha[linha], quadros[i].height);
    }

    folha.width = layout.margin * 2 + colunas * layout.cell_width + (colunas - 1) * layout.gap;
    folha.height = layout.margin * 2;
    for (int linha = 0; linha < linhas; ++linha) {
        folha.height += altura_da_linha[linha] + faixa;
    }
    folha.height += (linhas - 1) * layout.gap;

    const Cor fundo = cor_do_fundo(layout.background);
    folha.pixels.assign(static_cast<std::size_t>(folha.width) * folha.height * 4, 0);
    for (std::size_t i = 0; i < folha.pixels.size(); i += 4) {
        folha.pixels[i + 0] = fundo.r;
        folha.pixels[i + 1] = fundo.g;
        folha.pixels[i + 2] = fundo.b;
        folha.pixels[i + 3] = fundo.a;
    }

    const Cor texto = cor_do_texto(layout.background);
    const Cor filete = cor_do_filete(layout.background);

    int y = layout.margin;
    for (int linha = 0; linha < linhas; ++linha) {
        for (int coluna = 0; coluna < colunas; ++coluna) {
            const std::size_t i = static_cast<std::size_t>(linha) * colunas + coluna;
            if (i >= quadros.size()) {
                break;
            }
            const Quadro& quadro = quadros[i];
            const int x = layout.margin + coluna * (layout.cell_width + layout.gap);

            // Quadro mais baixo que a linha fica no meio, senão a tira ganha
            // um degrau que não quer dizer nada.
            const int topo = y + (altura_da_linha[linha] - quadro.height) / 2;

            for (int j = 0; j < quadro.height; ++j) {
                for (int k = 0; k < quadro.width; ++k) {
                    const unsigned char* p =
                        &quadro.pixels[(static_cast<std::size_t>(j) * quadro.width + k) * 4];
                    pinta(folha, x + k, topo + j, {p[0], p[1], p[2], 255}, p[3] / 255.0f);
                }
            }

            if (layout.frame) {
                for (int k = -1; k <= quadro.width; ++k) {
                    pinta(folha, x + k, topo - 1, filete, filete.a / 255.0f);
                    pinta(folha, x + k, topo + quadro.height, filete, filete.a / 255.0f);
                }
                for (int j = -1; j <= quadro.height; ++j) {
                    pinta(folha, x - 1, topo + j, filete, filete.a / 255.0f);
                    pinta(folha, x + quadro.width, topo + j, filete, filete.a / 255.0f);
                }
            }

            if (layout.labels && !quadro.label.empty()) {
                const float largura = largura_do_texto(quadro.label.c_str());
                escreve(folha, x + (quadro.width - largura) * 0.5f,
                        y + altura_da_linha[linha] + layout.label_gap, quadro.label.c_str(), texto);
            }
        }
        y += altura_da_linha[linha] + faixa + layout.gap;
    }

    return folha;
}

bool write_sheet(const Sheet& sheet, const std::string& path, std::string* error) {
    if (sheet.empty()) {
        if (error) {
            *error = "nenhum estágio selecionado";
        }
        return false;
    }
    if (!stbi_write_png(path.c_str(), sheet.width, sheet.height, 4, sheet.pixels.data(),
                        sheet.width * 4)) {
        if (error) {
            *error = "não consegui escrever em " + path;
        }
        return false;
    }
    return true;
}
