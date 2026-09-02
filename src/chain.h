#pragma once

#include <string>
#include <variant>
#include <vector>

#include "convolve.h"
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
    Channel channel = Channel::Luma;

    // Rec. 709, que é o que combina com o buffer linear.
    float weight[3] = {0.2126f, 0.7152f, 0.0722f};
    bool on_srgb = false;
};
struct ThresholdOp { float level = 0.5f; bool otsu = false; };
struct OverlayOp { float opacity = 0.5f; };
struct MorphologyOp {
    Morph operation = Morph::Erode;
    float radius = 1.0f;
};
struct ComponentsOp { float radius = 1.5f; };
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
struct BitPlaneOp { int plane = 7; };
struct ComposeOp {};
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
    Border border = Border::Clamp;
    bool flip = false;
    bool normalize = false;
};

using OpParams = std::variant<SourceOp, ExposureOp, ContrastOp, GammaOp, InvertOp, ChannelOp,
                              ThresholdOp, OverlayOp, ConvolveOp, MorphologyOp, ComponentsOp,
                              EqualizeOp, StretchOp, ClaheOp, CombineOp, CurveOp,
                              RankOp, MatchOp, BitPlaneOp, ComposeOp>;

// Operação que aceita mais de um tipo e devolve o que recebeu. Convolução não
// entra em rótulo porque interpolar índice de região não quer dizer nada;
// morfologia não entra em cor porque mínimo e máximo de RGB tratam canal por
// canal e desfazem a cor.
enum class Poly {
    None,
    ColorOrScalar,
    ScalarOrLabel,

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
    int find_input(const OpInfo& info, int k, int prefer_id) const;
    bool can_add(const OpParams& params) const;

    int add(OpParams params, int prefer_id = -1);
    void remove(int id);
    void evaluate(const Image& source);
};
