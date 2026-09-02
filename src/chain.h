#pragma once

#include <string>
#include <variant>
#include <vector>

#include "convolve.h"
#include "kernel.h"
#include "morphology.h"
#include "value.h"

struct SourceOp {};
struct ExposureOp { float stops = 0.0f; };
struct ContrastOp { float amount = 0.0f; };
struct GammaOp { float gamma = 1.0f; };
struct InvertOp {};
struct LuminanceOp {};
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

using OpParams = std::variant<SourceOp, ExposureOp, ContrastOp, GammaOp, InvertOp, LuminanceOp,
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
    int add(OpParams params);
    void remove(int id);
    void evaluate(const Image& source);
};
