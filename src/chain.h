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
                              EqualizeOp, StretchOp>;

// Operação que aceita mais de um tipo e devolve o que recebeu. Convolução não
// entra em rótulo porque interpolar índice de região não quer dizer nada;
// morfologia não entra em cor porque mínimo e máximo de RGB tratam canal por
// canal e desfazem a cor.
enum class Poly { None, ColorOrScalar, ScalarOrLabel };

struct OpInfo {
    const char* name;
    int input_count;
    ValueKind inputs[2];
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

    // Id do estágio que serve de entrada k, ou -1 se nenhum serve. `prefer` é
    // tentado primeiro, pra operação nova pendurar no que está sendo olhado.
    int find_input(const OpInfo& info, int k, int prefer_id) const;
    bool can_add(const OpParams& params) const;

    int add(OpParams params, int prefer_id = -1);
    void remove(int id);
    void evaluate(const Image& source);
};
