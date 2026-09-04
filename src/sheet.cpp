#include "sheet.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
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

ImFontBaked* baked(float corpo) {
    ImFont* fonte = ImGui::GetFont();
    return fonte ? fonte->GetFontBaked(corpo) : nullptr;
}

// O ImGui assa a fonte no corpo pedido, mas se o atlas estiver no modo antigo
// ele devolve o tamanho que já tinha. Aí o glifo é ampliado por esse fator:
// perde nitidez, mas a legenda nunca sai com o tamanho errado.
float estica(const ImFontBaked* fonte, float corpo) {
    return (fonte && fonte->Size > 0.0f) ? corpo / fonte->Size : 1.0f;
}

float largura_do_texto(const char* texto, float corpo) {
    ImFontBaked* fonte = baked(corpo);
    if (!fonte) {
        return 0.0f;
    }
    const float k = estica(fonte, corpo);
    float largura = 0.0f;
    for (const char* p = texto; *p;) {
        unsigned int ponto = 0;
        const int passo = ImTextCharFromUtf8(&ponto, p, nullptr);
        p += passo > 0 ? passo : 1;
        if (ponto == 0) {
            break;
        }
        if (const ImFontGlyph* glifo = fonte->FindGlyph(static_cast<ImWchar>(ponto))) {
            largura += glifo->AdvanceX * k;
        }
    }
    return largura;
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
void escreve(Sheet& folha, float x, float y, const char* texto, Cor cor, float corpo) {
    ImFontBaked* fonte = baked(corpo);
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
    const float k = estica(fonte, corpo);

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
        const int dw = std::max(1, static_cast<int>(std::lround((glifo->X1 - glifo->X0) * k)));
        const int dh = std::max(1, static_cast<int>(std::lround((glifo->Y1 - glifo->Y0) * k)));
        const int px = static_cast<int>(std::lround(x + glifo->X0 * k));
        const int py = static_cast<int>(std::lround(y + glifo->Y0 * k));
        for (int j = 0; j < dh; ++j) {
            for (int i = 0; i < dw; ++i) {
                const float u = glifo->U0 + (glifo->U1 - glifo->U0) * (i + 0.5f) / dw;
                const float v = glifo->V0 + (glifo->V1 - glifo->V0) * (j + 0.5f) / dh;
                const float alfa =
                    amostra_alfa(atlas->Pixels, aw, ah, bpp, u * aw - 0.5f, v * ah - 0.5f);
                pinta(folha, px + i, py + j, cor, alfa);
            }
        }
        x += glifo->AdvanceX * k;
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

namespace {

// O plano é a geometria da folha, sem um pixel desenhado. PNG e SVG saem do
// mesmo cálculo, senão os dois divergiriam no dia que alguém mexesse num só.
struct Celula {
    int stage_index = -1;
    int x = 0, y = 0, w = 0, h = 0;
    float label_x = 0.0f;  // centro do quadro; quem escreve decide o alinhamento
    float label_y = 0.0f;  // topo da linha de texto
    std::string label;
};

struct Plano {
    int width = 0;
    int height = 0;
    float corpo = 0.0f;   // tamanho da letra já multiplicado pela escala
    float ascent = 0.0f;
    std::vector<Celula> celulas;
};

Plano planeja(const App& app, const std::vector<SheetItem>& items, const SheetLayout& layout) {
    Plano plano;
    const int k = std::max(1, layout.scale);

    struct Fonte {
        int stage_index;
        int ow, oh;
        std::string label;
    };
    std::vector<Fonte> fontes;
    for (const SheetItem& item : items) {
        if (!item.on) {
            continue;
        }
        const int indice = app.chain.index_of(item.stage_id);
        if (indice < 0 || indice >= static_cast<int>(app.chain.outputs.size())) {
            continue;
        }
        const Value& valor = app.chain.outputs[indice];
        if (valor.empty() || valor.width() <= 0 || valor.height() <= 0) {
            continue;
        }
        fontes.push_back({indice, valor.width(), valor.height(), item.label});
    }
    if (fontes.empty()) {
        return plano;
    }

    // Colunas é teto, não largura fixa: com três quadros e o limite em cinco,
    // a folha sai com três, senão sobraria margem paga por célula vazia.
    const int pedido = layout.columns > 0 ? layout.columns : static_cast<int>(fontes.size());
    const int colunas = std::min(pedido, static_cast<int>(fontes.size()));
    const int linhas = (static_cast<int>(fontes.size()) + colunas - 1) / colunas;

    const int largura_celula = std::max(1, layout.cell_width * k);
    const int gap = layout.gap * k;
    const int margem = layout.margin * k;
    const int folga = layout.label_gap * k;

    plano.corpo = 16.0f * k;
    if (ImFontBaked* fonte = baked(plano.corpo)) {
        plano.ascent = fonte->Ascent * estica(fonte, plano.corpo);
    }
    const int faixa = layout.labels ? static_cast<int>(plano.corpo) + folga : 0;

    std::vector<int> altura(fontes.size(), 0);
    std::vector<int> altura_da_linha(linhas, 0);
    for (std::size_t i = 0; i < fontes.size(); ++i) {
        altura[i] = std::max(1, static_cast<int>(std::lround(
                                    static_cast<double>(fontes[i].oh) * largura_celula
                                    / fontes[i].ow)));
        altura_da_linha[i / colunas] = std::max(altura_da_linha[i / colunas], altura[i]);
    }

    plano.width = margem * 2 + colunas * largura_celula + (colunas - 1) * gap;
    plano.height = margem * 2 + (linhas - 1) * gap;
    for (int linha = 0; linha < linhas; ++linha) {
        plano.height += altura_da_linha[linha] + faixa;
    }

    int y = margem;
    for (int linha = 0; linha < linhas; ++linha) {
        for (int coluna = 0; coluna < colunas; ++coluna) {
            const std::size_t i = static_cast<std::size_t>(linha) * colunas + coluna;
            if (i >= fontes.size()) {
                break;
            }
            Celula celula;
            celula.stage_index = fontes[i].stage_index;
            celula.w = largura_celula;
            celula.h = altura[i];
            celula.x = margem + coluna * (largura_celula + gap);

            // Quadro mais baixo que a linha fica no meio, senão a tira ganha
            // um degrau que não quer dizer nada.
            celula.y = y + (altura_da_linha[linha] - celula.h) / 2;
            celula.label = fontes[i].label;
            celula.label_x = celula.x + largura_celula * 0.5f;
            celula.label_y = static_cast<float>(y + altura_da_linha[linha] + folga);
            plano.celulas.push_back(std::move(celula));
        }
        y += altura_da_linha[linha] + faixa + gap;
    }
    return plano;
}

}  // namespace

void sheet_size(const App& app, const std::vector<SheetItem>& items, const SheetLayout& layout,
                int* width, int* height) {
    const Plano plano = planeja(app, items, layout);
    if (width) {
        *width = plano.width;
    }
    if (height) {
        *height = plano.height;
    }
}

Sheet compose_sheet(const App& app, const std::vector<SheetItem>& items,
                    const SheetLayout& layout) {
    Sheet folha;
    const Plano plano = planeja(app, items, layout);
    if (plano.celulas.empty()) {
        return folha;
    }

    folha.width = plano.width;
    folha.height = plano.height;
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

    for (const Celula& celula : plano.celulas) {
        const Value& valor = app.chain.outputs[celula.stage_index];
        float lo = 0.0f;
        float hi = 0.0f;
        std::unique_ptr<unsigned char[]> rgba = to_display_rgba8(valor, app.colormap, &lo, &hi);
        if (!rgba) {
            continue;
        }
        std::vector<unsigned char> escalado(static_cast<std::size_t>(celula.w) * celula.h * 4);
        redimensiona(rgba.get(), valor.width(), valor.height(), escalado.data(), celula.w,
                     celula.h);

        for (int j = 0; j < celula.h; ++j) {
            for (int i = 0; i < celula.w; ++i) {
                const unsigned char* p =
                    &escalado[(static_cast<std::size_t>(j) * celula.w + i) * 4];
                pinta(folha, celula.x + i, celula.y + j, {p[0], p[1], p[2], 255}, p[3] / 255.0f);
            }
        }

        if (layout.frame) {
            for (int i = -1; i <= celula.w; ++i) {
                pinta(folha, celula.x + i, celula.y - 1, filete, filete.a / 255.0f);
                pinta(folha, celula.x + i, celula.y + celula.h, filete, filete.a / 255.0f);
            }
            for (int j = -1; j <= celula.h; ++j) {
                pinta(folha, celula.x - 1, celula.y + j, filete, filete.a / 255.0f);
                pinta(folha, celula.x + celula.w, celula.y + j, filete, filete.a / 255.0f);
            }
        }

        if (layout.labels && !celula.label.empty()) {
            const float largura = largura_do_texto(celula.label.c_str(), plano.corpo);
            escreve(folha, celula.label_x - largura * 0.5f, celula.label_y, celula.label.c_str(),
                    texto, plano.corpo);
        }
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

namespace {

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64(const unsigned char* dados, std::size_t n, std::string* saida) {
    saida->reserve(saida->size() + (n + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const unsigned int bloco = (dados[i] << 16) | (dados[i + 1] << 8) | dados[i + 2];
        *saida += kBase64[(bloco >> 18) & 63];
        *saida += kBase64[(bloco >> 12) & 63];
        *saida += kBase64[(bloco >> 6) & 63];
        *saida += kBase64[bloco & 63];
    }
    if (i < n) {
        const bool tem_dois = (i + 1 < n);
        const unsigned int bloco = (dados[i] << 16) | (tem_dois ? (dados[i + 1] << 8) : 0);
        *saida += kBase64[(bloco >> 18) & 63];
        *saida += kBase64[(bloco >> 12) & 63];
        *saida += tem_dois ? kBase64[(bloco >> 6) & 63] : '=';
        *saida += '=';
    }
}

void junta_png(void* contexto, void* dados, int tamanho) {
    auto* saida = static_cast<std::vector<unsigned char>*>(contexto);
    const auto* bytes = static_cast<const unsigned char*>(dados);
    saida->insert(saida->end(), bytes, bytes + tamanho);
}

// Legenda vem do usuário e vai pra dentro de XML.
std::string escapa(const std::string& texto) {
    std::string saida;
    for (char c : texto) {
        switch (c) {
            case '&': saida += "&amp;"; break;
            case '<': saida += "&lt;"; break;
            case '>': saida += "&gt;"; break;
            case '"': saida += "&quot;"; break;
            case '\'': saida += "&apos;"; break;
            default: saida += c;
        }
    }
    return saida;
}

std::string em_hex(Cor cor) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", cor.r, cor.g, cor.b);
    return buffer;
}

}  // namespace

bool write_sheet_svg(const App& app, const std::vector<SheetItem>& items,
                     const SheetLayout& layout, const std::string& path, std::string* error) {
    // No SVG a escala não faz sentido: a geometria é em unidade de usuário e o
    // renderizador amplia. Quem dá resolução é o pixel de origem da imagem.
    SheetLayout um = layout;
    um.scale = 1;
    const Plano plano = planeja(app, items, um);
    if (plano.celulas.empty()) {
        if (error) {
            *error = "nenhum estágio selecionado";
        }
        return false;
    }

    const Cor fundo = cor_do_fundo(layout.background);
    const Cor texto = cor_do_texto(layout.background);
    const Cor filete = cor_do_filete(layout.background);

    std::string svg;
    char cabecalho[256];
    std::snprintf(cabecalho, sizeof(cabecalho),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                  "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"%d\" height=\"%d\" "
                  "viewBox=\"0 0 %d %d\">\n",
                  plano.width, plano.height, plano.width, plano.height);
    svg += cabecalho;

    if (fundo.a > 0) {
        char rect[160];
        std::snprintf(rect, sizeof(rect),
                      "  <rect width=\"%d\" height=\"%d\" fill=\"%s\"/>\n", plano.width,
                      plano.height, em_hex(fundo).c_str());
        svg += rect;
    }

    for (const Celula& celula : plano.celulas) {
        const Value& valor = app.chain.outputs[celula.stage_index];
        float lo = 0.0f;
        float hi = 0.0f;
        std::unique_ptr<unsigned char[]> rgba = to_display_rgba8(valor, app.colormap, &lo, &hi);
        if (!rgba) {
            continue;
        }

        // O quadro entra na resolução que ele tem, não na do desenho: é isso
        // que deixa o zoom no artigo mostrar pixel de verdade.
        std::vector<unsigned char> png;
        if (!stbi_write_png_to_func(junta_png, &png, valor.width(), valor.height(), 4, rgba.get(),
                                    valor.width() * 4)) {
            continue;
        }

        char abre[224];
        std::snprintf(abre, sizeof(abre),
                      "  <image x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" "
                      "preserveAspectRatio=\"none\" xlink:href=\"data:image/png;base64,",
                      celula.x, celula.y, celula.w, celula.h);
        svg += abre;
        base64(png.data(), png.size(), &svg);
        svg += "\"/>\n";

        if (layout.frame) {
            char rect[224];
            std::snprintf(rect, sizeof(rect),
                          "  <rect x=\"%.1f\" y=\"%.1f\" width=\"%d\" height=\"%d\" "
                          "fill=\"none\" stroke=\"%s\" stroke-width=\"1\"/>\n",
                          celula.x - 0.5f, celula.y - 0.5f, celula.w + 1, celula.h + 1,
                          em_hex(filete).c_str());
            svg += rect;
        }

        if (layout.labels && !celula.label.empty()) {
            char texto_svg[320];
            std::snprintf(texto_svg, sizeof(texto_svg),
                          "  <text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" "
                          "font-family=\"Ubuntu, DejaVu Sans, Helvetica, sans-serif\" "
                          "font-size=\"%.1f\" fill=\"%s\">%s</text>\n",
                          celula.label_x, celula.label_y + plano.ascent, plano.corpo,
                          em_hex(texto).c_str(), escapa(celula.label).c_str());
            svg += texto_svg;
        }
    }

    svg += "</svg>\n";

    std::ofstream saida(path, std::ios::binary);
    if (!saida) {
        if (error) {
            *error = "não consegui escrever em " + path;
        }
        return false;
    }
    saida << svg;
    if (!saida) {
        if (error) {
            *error = "escrita incompleta em " + path;
        }
        return false;
    }
    return true;
}
