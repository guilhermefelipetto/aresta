#include "project.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "app.h"
#include "chain.h"

namespace {

// Uma lista só pra ler e pra escrever. Duas listas separadas divergem, e num
// arquivo de projeto divergir quer dizer projeto que não abre mais. A chave é
// sem acento de propósito: ela vive no arquivo, não na tela, e não deve mudar
// junto com o texto da interface.
#define ARESTA_OPS(X)                             \
    X(SourceOp, "origem")                         \
    X(ExposureOp, "exposicao")                    \
    X(ContrastOp, "contraste")                    \
    X(GammaOp, "gama")                            \
    X(InvertOp, "inverter")                       \
    X(ChannelOp, "canal")                         \
    X(ThresholdOp, "limiar")                      \
    X(OverlayOp, "overlay")                       \
    X(ConvolveOp, "convolucao")                   \
    X(MorphologyOp, "morfologia")                 \
    X(ComponentsOp, "componentes")                \
    X(EqualizeOp, "equalizar")                    \
    X(StretchOp, "esticar")                       \
    X(ClaheOp, "clahe")                           \
    X(CombineOp, "combinar")                      \
    X(CurveOp, "curva")                           \
    X(RankOp, "ordem")                            \
    X(MatchOp, "casar-histograma")                \
    X(BitPlaneOp, "plano-de-bit")                 \
    X(ComposeOp, "compor")                        \
    X(SpectrumOp, "espectro")                     \
    X(FreqFilterOp, "filtro-freq")                \
    X(NoiseOp, "ruido")                           \
    X(MeanOp, "media")                            \
    X(AdaptiveOp, "reducao-adaptativa")           \
    X(AdaptiveMedianOp, "mediana-adaptativa")     \
    X(DegradeOp, "degradar")                      \
    X(RestoreOp, "restaurar")                     \
    X(ColorGradientOp, "gradiente-cor")           \
    X(ColorDistanceOp, "distancia-cor")           \
    X(PseudoColorOp, "pseudo-cor")                \
    X(DistanceOp, "distancia")                    \
    X(ReconstructOp, "reconstruir")               \
    X(FillHolesOp, "preencher-buracos")           \
    X(ThinOp, "afinar")                           \
    X(HitMissOp, "hit-miss")                      \
    X(CannyOp, "canny")                           \
    X(LogEdgeOp, "log-borda")                     \
    X(AdaptiveThresholdOp, "limiar-local")        \
    X(MultiOtsuOp, "multi-otsu")                  \
    X(HoughAccumulatorOp, "hough-acumulador")     \
    X(HoughLinesOp, "hough-retas")                \
    X(HoughCirclesOp, "hough-circulos")           \
    X(WatershedOp, "watershed")                   \
    X(ResizeOp, "redimensionar")                  \
    X(RotateOp, "girar")                          \
    X(CropOp, "recortar")                         \
    X(FlipOp, "espelhar")                         \
    X(QuantizeOp, "quantizar")

constexpr int kListadas = 0
#define CONTA(T, K) +1
    ARESTA_OPS(CONTA)
#undef CONTA
    ;
static_assert(kListadas == std::variant_size_v<OpParams>,
              "operação nova sem chave: ela salvaria em branco no projeto");

// Nome de enumeração vai por extenso no arquivo. Número quebraria calado no
// dia que alguém inserir um valor no meio da enum.
constexpr const char* kSpace[] = {"rgb", "hsv", "hsi", "lab", "ycbcr", "cmy"};
constexpr const char* kThresholdUnit[] = {"absoluto", "fracao", "nivel"};
constexpr const char* kMorph[] = {"erosao",  "dilatacao", "abertura", "fechamento",
                                  "gradiente", "top-hat", "black-hat"};
constexpr const char* kMetric[] = {"euclidiana", "d4", "d8"};
constexpr const char* kInterp[] = {"vizinho", "bilinear", "bicubica"};
constexpr const char* kLocalThreshold[] = {"media", "gaussiana", "sauvola"};
constexpr const char* kThin[] = {"afinar", "engrossar", "esqueleto"};
constexpr const char* kCombine[] = {"somar",   "subtrair", "abs-dif", "multiplicar",
                                    "dividir", "minimo",   "maximo",  "media"};
constexpr const char* kRank[] = {"mediana", "minimo", "maximo", "ponto-medio", "alfa-cortada"};
constexpr const char* kNoise[] = {"gaussiano",   "rayleigh",     "gama",     "exponencial",
                                  "uniforme",    "sal-e-pimenta", "periodico"};
constexpr const char* kMean[] = {"aritmetica", "geometrica", "harmonica", "contra-harmonica"};
constexpr const char* kDegradation[] = {"movimento", "turbulencia"};
constexpr const char* kRestoration[] = {"inverso", "wiener", "minimos-quadrados"};
constexpr const char* kPad[] = {"espelhar", "zero"};
constexpr const char* kFreqShape[] = {"ideal", "butterworth", "gaussiano"};
constexpr const char* kFreqKind[] = {"passa-baixa", "passa-alta", "passa-faixa", "rejeita-faixa"};
constexpr const char* kColormap[] = {"cinza", "viridis", "magma", "turbo", "hot"};
constexpr const char* kBorder[] = {"zero", "estender", "espelhar", "circular"};
constexpr const char* kConvPath[] = {"auto", "espacial", "frequencia"};

std::string sem_espaco(std::string texto) {
    std::size_t inicio = texto.find_first_not_of(" \t\r\n");
    if (inicio == std::string::npos) {
        return {};
    }
    std::size_t fim = texto.find_last_not_of(" \t\r\n");
    return texto.substr(inicio, fim - inicio + 1);
}

struct Escritor {
    std::string texto;

    void linha(const char* nome, const std::string& valor) {
        texto += "  ";
        texto += nome;
        texto += ' ';
        texto += valor;
        texto += '\n';
    }

    void campo(const char* nome, const float& v) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(v));
        linha(nome, buffer);
    }
    void campo(const char* nome, const int& v) { linha(nome, std::to_string(v)); }
    void campo(const char* nome, const bool& v) { linha(nome, v ? "1" : "0"); }

    // Frase vai como resto da linha, então pode ter espaço dentro.
    void frase(const char* nome, const char* v, std::size_t) { linha(nome, v); }

    template <class E, std::size_t N>
    void enumeracao(const char* nome, const E& v, const char* const (&nomes)[N]) {
        const auto i = static_cast<std::size_t>(v);
        linha(nome, i < N ? nomes[i] : "?");
    }

    void vetor(const char* nome, const float* v, int n) {
        std::string valor;
        for (int i = 0; i < n; ++i) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%s%.9g", i ? " " : "",
                          static_cast<double>(v[i]));
            valor += buffer;
        }
        linha(nome, valor);
    }

    void nucleo(const Kernel& k) {
        linha("kernel", std::to_string(k.width) + " " + std::to_string(k.height));
        for (int y = 0; y < k.height; ++y) {
            std::string valor;
            for (int x = 0; x < k.width; ++x) {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%s%.9g", x ? " " : "",
                              static_cast<double>(k.at(x, y)));
                valor += buffer;
            }
            linha(("linha" + std::to_string(y)).c_str(), valor);
        }
    }
};

using Bloco = std::vector<std::pair<std::string, std::string>>;

struct Leitor {
    const Bloco* bloco = nullptr;

    const std::string* achar(const char* nome) const {
        for (const auto& par : *bloco) {
            if (par.first == nome) {
                return &par.second;
            }
        }
        return nullptr;
    }

    // Campo que não está no arquivo fica com o padrão do struct. É isso que
    // faz projeto velho continuar abrindo depois que a operação ganha campo.
    void campo(const char* nome, float& v) {
        if (const std::string* s = achar(nome)) {
            v = std::strtof(s->c_str(), nullptr);
        }
    }
    void campo(const char* nome, int& v) {
        if (const std::string* s = achar(nome)) {
            v = std::atoi(s->c_str());
        }
    }
    void campo(const char* nome, bool& v) {
        if (const std::string* s = achar(nome)) {
            v = (*s == "1");
        }
    }
    void frase(const char* nome, char* v, std::size_t n) {
        if (const std::string* s = achar(nome)) {
            std::snprintf(v, n, "%s", s->c_str());
        }
    }

    template <class E, std::size_t N>
    void enumeracao(const char* nome, E& v, const char* const (&nomes)[N]) {
        const std::string* s = achar(nome);
        if (!s) {
            return;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (*s == nomes[i]) {
                v = static_cast<E>(i);
                return;
            }
        }
    }

    void vetor(const char* nome, float* v, int n) {
        const std::string* s = achar(nome);
        if (!s) {
            return;
        }
        std::istringstream fluxo(*s);
        for (int i = 0; i < n; ++i) {
            float lido = 0.0f;
            if (!(fluxo >> lido)) {
                return;
            }
            v[i] = lido;
        }
    }

    void nucleo(Kernel& k) {
        const std::string* s = achar("kernel");
        if (!s) {
            return;
        }
        int w = 0;
        int h = 0;
        if (std::sscanf(s->c_str(), "%d %d", &w, &h) != 2 || w <= 0 || h <= 0) {
            return;
        }
        k.resize(w, h);
        for (int y = 0; y < h; ++y) {
            const std::string* linha = achar(("linha" + std::to_string(y)).c_str());
            if (!linha) {
                continue;
            }
            std::istringstream fluxo(*linha);
            for (int x = 0; x < w; ++x) {
                float lido = 0.0f;
                if (!(fluxo >> lido)) {
                    break;
                }
                k.at(x, y) = lido;
            }
        }
    }
};

// Uma função por operação, e as duas direções passam por ela. Escrever a
// leitura separada da escrita é como as duas saem de sincronia.
template <class V> void campos(SourceOp&, V&) {}
template <class V> void campos(InvertOp&, V&) {}
template <class V> void campos(EqualizeOp&, V&) {}
template <class V> void campos(MatchOp&, V&) {}
template <class V> void campos(HitMissOp&, V&) {}

template <class V> void campos(ExposureOp& op, V& v) { v.campo("ev", op.stops); }
template <class V> void campos(ContrastOp& op, V& v) { v.campo("quanto", op.amount); }
template <class V> void campos(GammaOp& op, V& v) { v.campo("gama", op.gamma); }
template <class V> void campos(OverlayOp& op, V& v) { v.campo("opacidade", op.opacity); }
template <class V> void campos(BitPlaneOp& op, V& v) { v.campo("bit", op.plane); }
template <class V> void campos(QuantizeOp& op, V& v) { v.campo("niveis", op.levels); }
template <class V> void campos(MultiOtsuOp& op, V& v) { v.campo("classes", op.classes); }
template <class V> void campos(ReconstructOp& op, V& v) { v.campo("raio", op.radius); }
template <class V> void campos(FillHolesOp& op, V& v) { v.campo("raio", op.radius); }
template <class V> void campos(AdaptiveMedianOp& op, V& v) { v.campo("raio-max", op.max_radius); }

template <class V> void campos(ChannelOp& op, V& v) {
    v.enumeracao("espaco", op.space, kSpace);
    v.campo("componente", op.component);
    v.vetor("pesos", op.weight, 3);
    v.campo("srgb", op.on_srgb);
}
template <class V> void campos(ThresholdOp& op, V& v) {
    v.campo("nivel", op.level);
    v.campo("otsu", op.otsu);
    v.enumeracao("unidade", op.unit, kThresholdUnit);
    v.campo("bits", op.bits);
}
template <class V> void campos(ConvolveOp& op, V& v) {
    v.enumeracao("borda", op.border, kBorder);
    v.campo("espelhar", op.flip);
    v.campo("normalizar", op.normalize);
    v.enumeracao("caminho", op.path, kConvPath);
    v.nucleo(op.kernel);
}
template <class V> void campos(MorphologyOp& op, V& v) {
    v.enumeracao("operacao", op.operation, kMorph);
    v.campo("raio", op.radius);
}
template <class V> void campos(ComponentsOp& op, V& v) {
    v.campo("raio", op.radius);
    v.campo("area-min", op.filter.min_area);
    v.campo("area-max", op.filter.max_area);
    v.campo("maiores", op.filter.keep_largest);
    v.campo("tira-borda", op.filter.drop_border);
    v.campo("renumera", op.filter.renumber_by_area);
    v.campo("so-rotulo", op.filter.only_label);
}
template <class V> void campos(StretchOp& op, V& v) {
    v.campo("baixo", op.low);
    v.campo("alto", op.high);
}
template <class V> void campos(ClaheOp& op, V& v) {
    v.campo("pedacos", op.tiles);
    v.campo("recorte", op.clip);
}
template <class V> void campos(CombineOp& op, V& v) {
    v.enumeracao("operacao", op.operation, kCombine);
    v.campo("escala", op.scale);
}
template <class V> void campos(CurveOp& op, V& v) {
    v.campo("a", op.a);
    v.campo("b", op.b);
    v.campo("c", op.c);
    v.campo("srgb", op.on_srgb);
    v.frase("expressao", op.expression, sizeof(op.expression));
}
template <class V> void campos(RankOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kRank);
    v.campo("raio", op.radius);
    v.campo("alfa", op.alpha);
}
template <class V> void campos(ComposeOp& op, V& v) { v.enumeracao("espaco", op.space, kSpace); }
template <class V> void campos(ColorGradientOp& op, V& v) {
    v.enumeracao("espaco", op.space, kSpace);
}
template <class V> void campos(ColorDistanceOp& op, V& v) {
    v.enumeracao("espaco", op.space, kSpace);
    v.vetor("referencia", op.reference, 3);
}
template <class V> void campos(PseudoColorOp& op, V& v) { v.enumeracao("mapa", op.map, kColormap); }
template <class V> void campos(SpectrumOp& op, V& v) {
    v.enumeracao("preenchimento", op.pad, kPad);
    v.campo("log", op.logarithmic);
}
template <class V> void campos(FreqFilterOp& op, V& v) {
    v.enumeracao("forma", op.shape, kFreqShape);
    v.enumeracao("tipo", op.kind, kFreqKind);
    v.campo("corte", op.cutoff);
    v.campo("ordem", op.order);
    v.campo("largura", op.width);
    v.enumeracao("preenchimento", op.pad, kPad);
}
template <class V> void campos(NoiseOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kNoise);
    v.campo("a", op.a);
    v.campo("b", op.b);
    v.campo("c", op.c);
    v.campo("semente", op.seed);
}
template <class V> void campos(MeanOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kMean);
    v.campo("raio", op.radius);
    v.campo("q", op.q);
}
template <class V> void campos(AdaptiveOp& op, V& v) {
    v.campo("raio", op.radius);
    v.campo("variancia", op.noise_variance);
}
template <class V> void campos(DegradeOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kDegradation);
    v.campo("dx", op.dx);
    v.campo("dy", op.dy);
    v.campo("k", op.k);
    v.enumeracao("preenchimento", op.pad, kPad);
}
template <class V> void campos(RestoreOp& op, V& v) {
    v.enumeracao("metodo", op.method, kRestoration);
    v.enumeracao("tipo", op.kind, kDegradation);
    v.campo("dx", op.dx);
    v.campo("dy", op.dy);
    v.campo("k", op.k);
    v.campo("parametro", op.parameter);
    v.campo("corte", op.limit);
    v.enumeracao("preenchimento", op.pad, kPad);
}
template <class V> void campos(DistanceOp& op, V& v) {
    v.campo("dentro", op.inside);
    v.enumeracao("metrica", op.metric, kMetric);
}
template <class V> void campos(ThinOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kThin);
    v.campo("iteracoes", op.iterations);
}
template <class V> void campos(CannyOp& op, V& v) {
    v.campo("sigma", op.sigma);
    v.campo("baixo", op.low);
    v.campo("alto", op.high);
}
template <class V> void campos(LogEdgeOp& op, V& v) {
    v.campo("sigma", op.sigma);
    v.campo("inclinacao", op.slope);
}
template <class V> void campos(AdaptiveThresholdOp& op, V& v) {
    v.enumeracao("tipo", op.kind, kLocalThreshold);
    v.campo("raio", op.radius);
    v.campo("desvio", op.offset);
    v.campo("k", op.k);
}
template <class V> void campos(HoughAccumulatorOp& op, V& v) {
    v.campo("thetas", op.thetas);
    v.campo("rhos", op.rhos);
}
template <class V> void campos(HoughLinesOp& op, V& v) {
    v.campo("thetas", op.thetas);
    v.campo("rhos", op.rhos);
    v.campo("limiar", op.threshold);
    v.campo("max", op.max_lines);
}
template <class V> void campos(HoughCirclesOp& op, V& v) {
    v.campo("raio-min", op.min_radius);
    v.campo("raio-max", op.max_radius);
    v.campo("passo", op.step);
    v.campo("limiar", op.threshold);
    v.campo("max", op.max_circles);
}
template <class V> void campos(WatershedOp& op, V& v) {
    v.campo("raio", op.radius);
    v.campo("linhas", op.lines);
}
template <class V> void campos(ResizeOp& op, V& v) {
    v.campo("por-fator", op.by_scale);
    v.campo("fator", op.scale);
    v.campo("largura", op.width);
    v.campo("altura", op.height);
    v.enumeracao("interpolacao", op.interp, kInterp);
}
template <class V> void campos(RotateOp& op, V& v) {
    v.campo("graus", op.degrees);
    v.enumeracao("interpolacao", op.interp, kInterp);
    v.campo("expande", op.expand);
}
template <class V> void campos(CropOp& op, V& v) {
    v.campo("x", op.x);
    v.campo("y", op.y);
    v.campo("largura", op.width);
    v.campo("altura", op.height);
}
template <class V> void campos(FlipOp& op, V& v) {
    v.campo("horizontal", op.horizontal);
    v.campo("vertical", op.vertical);
    v.campo("transpor", op.transpose);
}

const char* chave_de(const OpParams& params) {
    return std::visit(
        [](const auto& op) -> const char* {
            using T = std::decay_t<decltype(op)>;
#define CASO(Tipo, Chave) \
    if constexpr (std::is_same_v<T, Tipo>) return Chave;
            ARESTA_OPS(CASO)
#undef CASO
            return "?";
        },
        params);
}

bool params_de(const std::string& chave, Leitor& leitor, OpParams* saida) {
#define CASO(Tipo, Chave)             \
    if (chave == Chave) {             \
        Tipo op;                      \
        campos(op, leitor);           \
        *saida = std::move(op);       \
        return true;                  \
    }
    ARESTA_OPS(CASO)
#undef CASO
    return false;
}

}  // namespace

std::string project_text(const App& app) {
    std::string texto = "aresta 1\n";
    if (!app.path.empty()) {
        texto += "imagem " + app.path + "\n";
    }
    texto += "vista " + std::to_string(app.viewed) + "\n";
    texto += "fixado " + std::to_string(app.pinned) + "\n";
    texto += "compacto " + std::string(app.chain_compact ? "1" : "0") + "\n";
    {
        Escritor cabecalho;
        cabecalho.enumeracao("mapa", app.colormap, kColormap);
        texto += sem_espaco(cabecalho.texto) + "\n";
    }

    for (const Stage& stage : app.chain.stages) {
        texto += "\nestagio " + std::to_string(stage.id) + " " + chave_de(stage.params) + "\n";
        for (int entrada : stage.inputs) {
            texto += "  de " + std::to_string(entrada) + "\n";
        }
        texto += std::string("  ativo ") + (stage.enabled ? "1" : "0") + "\n";

        Escritor escritor;
        std::visit([&escritor](auto& op) { campos(op, escritor); },
                   const_cast<OpParams&>(stage.params));
        texto += escritor.texto;
    }
    return texto;
}

bool save_project(const App& app, const std::string& file, std::string* error) {
    const std::string texto = project_text(app);

    std::ofstream saida(file, std::ios::binary);
    if (!saida) {
        if (error) {
            *error = "não consegui escrever em " + file;
        }
        return false;
    }
    saida << texto;
    if (!saida) {
        if (error) {
            *error = "escrita incompleta em " + file;
        }
        return false;
    }
    return true;
}

ProjectLoad load_project(App& app, const std::string& file) {
    ProjectLoad resultado;

    std::ifstream entrada(file, std::ios::binary);
    if (!entrada) {
        resultado.error = "não consegui abrir " + file;
        return resultado;
    }

    std::string primeira;
    if (!std::getline(entrada, primeira) || sem_espaco(primeira).rfind("aresta ", 0) != 0) {
        resultado.error = file + " não parece um projeto do aresta";
        return resultado;
    }

    std::string imagem;
    int vista = 0;
    int fixado = -1;
    bool compacto = true;
    Colormap mapa = Colormap::Gray;

    struct Lido {
        int id = 0;
        std::string chave;
        std::vector<int> entradas;
        bool ativo = true;
        Bloco bloco;
    };
    std::vector<Lido> lidos;

    std::string linha;
    while (std::getline(entrada, linha)) {
        const std::string limpa = sem_espaco(linha);
        if (limpa.empty() || limpa[0] == '#') {
            continue;
        }
        const std::size_t espaco = limpa.find(' ');
        const std::string nome = limpa.substr(0, espaco);
        const std::string valor = (espaco == std::string::npos) ? "" : limpa.substr(espaco + 1);

        if (nome == "estagio") {
            Lido novo;
            const std::size_t corte = valor.find(' ');
            novo.id = std::atoi(valor.c_str());
            novo.chave = (corte == std::string::npos) ? "" : sem_espaco(valor.substr(corte + 1));
            lidos.push_back(std::move(novo));
            continue;
        }
        if (lidos.empty()) {
            if (nome == "imagem") {
                imagem = valor;
            } else if (nome == "vista") {
                vista = std::atoi(valor.c_str());
            } else if (nome == "fixado") {
                fixado = std::atoi(valor.c_str());
            } else if (nome == "compacto") {
                compacto = (valor == "1");
            } else if (nome == "mapa") {
                Bloco b{{"mapa", valor}};
                Leitor leitor{&b};
                leitor.enumeracao("mapa", mapa, kColormap);
            }
            continue;
        }

        Lido& atual = lidos.back();
        if (nome == "de") {
            atual.entradas.push_back(std::atoi(valor.c_str()));
        } else if (nome == "ativo") {
            atual.ativo = (valor == "1");
        } else {
            atual.bloco.emplace_back(nome, valor);
        }
    }

    if (lidos.empty()) {
        resultado.error = "o projeto não tem estágio nenhum";
        return resultado;
    }

    Chain nova;
    nova.stages.clear();
    nova.next_id = 1;
    for (Lido& lido : lidos) {
        Stage stage;
        stage.id = lido.id;
        stage.inputs = lido.entradas;
        stage.enabled = lido.ativo;
        Leitor leitor{&lido.bloco};
        if (!params_de(lido.chave, leitor, &stage.params)) {
            resultado.error = "operação desconhecida no projeto: " + lido.chave;
            return resultado;
        }
        nova.next_id = std::max(nova.next_id, stage.id + 1);
        nova.stages.push_back(std::move(stage));
    }

    // A imagem entra antes da cadeia, porque abrir imagem zera a cadeia. Se ela
    // sumiu o projeto abre do mesmo jeito: o trabalho é a cadeia, o arquivo de
    // entrada é só onde ela apontava.
    if (!imagem.empty() && !app.open(imagem, true)) {
        resultado.missing_image = true;
        resultado.wanted_image = imagem;
        app.status.clear();
    }

    app.chain = std::move(nova);
    app.pinned = fixado;
    app.chain_compact = compacto;
    app.colormap = mapa;
    app.viewed = 0;

    if (vista >= 0 && vista < static_cast<int>(app.chain.stages.size())) {
        app.viewed = vista;
    }
    app.evaluate();
    app.upload_view();

    resultado.ok = true;
    return resultado;
}
