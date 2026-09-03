#pragma once

#include <string>
#include <variant>
#include <vector>

#include "convolve.h"
#include "geometry.h"
#include "fft.h"
#include "regions.h"
#include "restore.h"
#include "segment.h"
#include "ops.h"
#include "kernel.h"
#include "morphology.h"
#include "value.h"

struct SourceOp {};
struct ExposureOp { float stops = 0.0f; };
struct ContrastOp { float amount = 0.0f; };
struct GammaOp { float gamma = 1.0f; };
struct InvertOp {};
struct ChannelOp {
    Space space = Space::RGB;
    int component = channel_luma;

    // Rec. 709, que é o que combina com o buffer linear.
    float weight[3] = {0.2126f, 0.7152f, 0.0722f};
    bool on_srgb = false;
};
// O nível pode ser dito de três jeitos. Absoluto compara com o valor cru do
// mapa; fração e nível são posições dentro do intervalo dele, o que sobrevive
// a trocar um canal de luminância por um L de Lab, que vai até 100.
enum class ThresholdUnit { Absolute, Fraction, Level };

const char* threshold_unit_name(ThresholdUnit unit);

struct ThresholdOp {
    float level = 0.5f;
    bool otsu = false;
    ThresholdUnit unit = ThresholdUnit::Fraction;
    int bits = 8;
};

// Converte o que está na UI pro valor absoluto que a comparação usa.
float threshold_absolute(const ThresholdOp& op, float lo, float hi);
struct OverlayOp { float opacity = 0.5f; };
struct MorphologyOp {
    Morph operation = Morph::Erode;
    float radius = 1.0f;
};
struct DistanceOp {
    bool inside = true;
    Metric metric = Metric::Euclidean;
};
struct ResizeOp {
    bool by_scale = true;
    float scale = 0.5f;
    int width = 512;
    int height = 512;
    Interp interp = Interp::Bilinear;
};
struct RotateOp {
    float degrees = 15.0f;
    Interp interp = Interp::Bilinear;
    bool expand = true;
};
struct CropOp {
    int x = 0;
    int y = 0;
    int width = 256;
    int height = 256;
};
struct FlipOp {
    bool horizontal = true;
    bool vertical = false;
    bool transpose = false;
};
struct QuantizeOp { int levels = 8; };
struct CannyOp {
    float sigma = 1.2f;
    float low = 0.10f;
    float high = 0.25f;
};
struct LogEdgeOp {
    float sigma = 1.5f;
    float slope = 0.02f;
};
struct AdaptiveThresholdOp {
    LocalThreshold kind = LocalThreshold::Mean;
    float radius = 8.0f;
    float offset = 0.02f;
    float k = 0.2f;
};
struct MultiOtsuOp { int classes = 3; };
struct HoughAccumulatorOp {
    int thetas = 360;
    int rhos = 360;
};
struct HoughLinesOp {
    int thetas = 360;
    int rhos = 360;
    float threshold = 0.45f;
    int max_lines = 10;
};
struct HoughCirclesOp {
    float min_radius = 10.0f;
    float max_radius = 40.0f;
    float step = 2.0f;
    float threshold = 0.55f;
    int max_circles = 8;
};
struct WatershedOp {
    float radius = 1.5f;
    bool lines = true;
};

struct ReconstructOp { float radius = 1.5f; };
struct FillHolesOp { float radius = 1.5f; };
struct ThinOp {
    Thin kind = Thin::Skeleton;
    int iterations = 1;
};
struct HitMissOp {
    int pattern[9] = {-1, -1, -1, -1, 1, -1, -1, -1, -1};
};
struct ComponentsOp {
    float radius = 1.5f;
    ComponentFilter filter;
};
struct EqualizeOp {};
struct CombineOp {
    Combine operation = Combine::Subtract;
    float scale = 1.0f;
};
struct RankOp {
    Rank kind = Rank::Median;
    float radius = 1.5f;
    float alpha = 0.4f;
};
struct MatchOp {};
struct NoiseOp {
    Noise kind = Noise::Gaussian;
    float a = 0.0f;
    float b = 0.05f;
    float c = 8.0f;
    int seed = 1;
};
struct MeanOp {
    Mean kind = Mean::Arithmetic;
    float radius = 1.5f;
    float q = 1.5f;
};
struct AdaptiveOp {
    float radius = 2.0f;
    float noise_variance = 0.005f;
};
struct AdaptiveMedianOp { float max_radius = 3.5f; };
struct DegradeOp {
    Degradation kind = Degradation::Motion;
    float dx = 20.0f;
    float dy = 0.0f;
    float k = 5.0f;
    Pad pad = Pad::Mirror;
};
struct RestoreOp {
    Restoration method = Restoration::Wiener;
    Degradation kind = Degradation::Motion;
    float dx = 20.0f;
    float dy = 0.0f;
    float k = 5.0f;
    float parameter = 0.01f;
    float limit = 0.3f;
    Pad pad = Pad::Mirror;
};
struct SpectrumOp {
    Pad pad = Pad::Mirror;
    bool logarithmic = true;
};
struct FreqFilterOp {
    FreqShape shape = FreqShape::Gaussian;
    FreqKind kind = FreqKind::LowPass;
    float cutoff = 0.15f;
    int order = 2;
    float width = 0.08f;
    Pad pad = Pad::Mirror;
};
struct BitPlaneOp { int plane = 7; };
struct ColorGradientOp { Space space = Space::Lab; };
struct ColorDistanceOp {
    Space space = Space::Lab;
    float reference[3] = {0.9f, 0.5f, 0.6f};
};
struct PseudoColorOp { Colormap map = Colormap::Viridis; };
struct ComposeOp { Space space = Space::RGB; };
struct CurveOp {
    char expression[128] = "v";
    float a = 1.0f;
    float b = 1.0f;
    float c = 0.0f;
    bool on_srgb = false;
};
struct ClaheOp {
    int tiles = 8;
    float clip = 2.0f;
};
struct StretchOp {
    float low = 0.5f;
    float high = 99.5f;
};
struct ConvolveOp {
    Kernel kernel;
    Border border = Border::Zero;
    bool flip = false;
    bool normalize = false;
    ConvPath path = ConvPath::Auto;
};

using OpParams = std::variant<SourceOp, ExposureOp, ContrastOp, GammaOp, InvertOp, ChannelOp,
                              ThresholdOp, OverlayOp, ConvolveOp, MorphologyOp, ComponentsOp,
                              EqualizeOp, StretchOp, ClaheOp, CombineOp, CurveOp,
                              RankOp, MatchOp, BitPlaneOp, ComposeOp, SpectrumOp,
                              FreqFilterOp, NoiseOp, MeanOp, AdaptiveOp,
                              AdaptiveMedianOp, DegradeOp, RestoreOp, ColorGradientOp,
                              ColorDistanceOp, PseudoColorOp, DistanceOp,
                              ReconstructOp, FillHolesOp, ThinOp, HitMissOp, CannyOp,
                              LogEdgeOp, AdaptiveThresholdOp, MultiOtsuOp,
                              HoughAccumulatorOp, HoughLinesOp, HoughCirclesOp, WatershedOp,
                              ResizeOp, RotateOp, CropOp, FlipOp, QuantizeOp>;

// Operação que aceita mais de um tipo e devolve o que recebeu. Convolução não
// entra em rótulo porque interpolar índice de região não quer dizer nada;
// morfologia não entra em cor porque mínimo e máximo de RGB tratam canal por
// canal e desfazem a cor.
enum class Poly {
    None,
    ColorOrScalar,
    ScalarOrLabel,

    // Aceita qualquer tipo e devolve o mesmo.
    Any,

    // Entradas de qualquer tipo, contanto que sejam todas o mesmo. A checagem
    // de igualdade fica na avaliação, porque na hora de ligar ainda não se sabe
    // o que cada lado vai produzir.
    Pair,

    // Igual ao Pair, mas sem rótulo: casar histograma de mapa de índice de
    // região não quer dizer nada.
    PairTone,
};

struct OpInfo {
    const char* name;
    int input_count;
    ValueKind inputs[3];
    ValueKind output;
    Poly poly = Poly::None;
};

bool poly_accepts(Poly poly, ValueKind kind);

// Linha curta com os parâmetros que distinguem duas operações do mesmo nome.
// Com três convoluções empilhadas, "convolução" três vezes não ajuda ninguém.
std::string stage_summary(const OpParams& params);

// Nome do slot k. "entrada" não diz nada quando a operação tem três delas e a
// ordem importa.
const char* input_label(const OpParams& params, int k);

struct Chain;

// Quando falta o tipo intermediário, diz qual operação resolveria em um passo
// só. Uma ponte, nunca duas: montar cadeia inteira no lugar de alguém é magia
// demais pra uma bancada.
bool bridge_for(const Chain& chain, const OpParams& params, OpParams* bridge);

OpInfo op_info(const OpParams& params);

struct Stage {
    int id = 0;
    OpParams params;

    // Ids, não posições: apagar um estágio do meio não pode reescrever a
    // referência de todo mundo que vem depois.
    std::vector<int> inputs;
    bool enabled = true;

    std::string error;

    // Quanto essa operação levou na última avaliação, e quanto custou tudo que
    // esse resultado precisou. Numa cadeia reta os dois andam juntos; com
    // ramo, o acumulado só conta quem alimenta esse estágio.
    double ms = 0.0;
    double ms_total = 0.0;

    // Recado informativo do próprio operador, tipo o nível que o Otsu escolheu
    // ou quantas componentes saíram. Limpo a cada avaliação.
    std::string note;
};

struct Chain {
    std::vector<Stage> stages;
    std::vector<Value> outputs;
    int next_id = 1;

    Chain();

    int index_of(int id) const;

    // O tipo que o estágio produziu de fato, que só coincide com o declarado
    // quando a operação não é polimórfica: morfologia declara escalar mas
    // devolve rótulo quando recebe rótulo.
    ValueKind kind_of(int index) const;

    // Id do estágio que serve de entrada k, ou -1 se nenhum serve. `prefer` é
    // tentado primeiro, pra operação nova pendurar no que está sendo olhado.
    // Uma pergunta, um lugar. Ter isso escrito duas vezes já deixou o combo
    // filtrando por um critério e a ligação automática por outro.
    bool can_feed(const OpInfo& info, int k, int index) const;

    int find_input(const OpInfo& info, int k, int prefer_id, int limit) const;

    // Quando a operação tem várias entradas do mesmo tipo, ligar as três no
    // mesmo estágio nunca é o que a pessoa queria. Espalha pelos últimos.
    void wire_inputs(const OpParams& params, int prefer_id, int limit,
                     std::vector<int>* inputs) const;
    bool can_add(const OpParams& params) const;

    // `position` é onde o estágio entra; -1 põe no fim. Só estágios acima da
    // posição contam como candidatos a entrada, senão a ligação apontaria pra
    // frente e a cadeia deixaria de ser avaliável em uma passada.
    int add(OpParams params, int prefer_id = -1, int position = -1);

    // Troca com o vizinho. Recusa quando a troca faria alguém depender de quem
    // vem depois.
    bool move_stage(int id, int delta);
    void remove(int id);
    void evaluate(const Image& source);
};
