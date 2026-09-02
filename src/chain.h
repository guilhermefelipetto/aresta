#pragma once

#include <string>
#include <variant>
#include <vector>

#include "value.h"

struct SourceOp {};
struct ExposureOp { float stops = 0.0f; };
struct ContrastOp { float amount = 0.0f; };
struct GammaOp { float gamma = 1.0f; };
struct InvertOp {};
struct LuminanceOp {};
struct ThresholdOp { float level = 0.5f; };
struct OverlayOp { float opacity = 0.5f; };

using OpParams = std::variant<SourceOp, ExposureOp, ContrastOp, GammaOp, InvertOp, LuminanceOp,
                              ThresholdOp, OverlayOp>;

struct OpInfo {
    const char* name;
    int input_count;
    ValueKind inputs[2];
    ValueKind output;
};

OpInfo op_info(const OpParams& params);

struct Stage {
    int id = 0;
    OpParams params;

    // Ids, não posições: apagar um estágio do meio não pode reescrever a
    // referência de todo mundo que vem depois.
    std::vector<int> inputs;
    bool enabled = true;

    std::string error;
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
