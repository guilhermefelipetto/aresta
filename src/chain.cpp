#include "chain.h"

#include <type_traits>
#include <utility>

#include "convolve.h"
#include "histogram.h"
#include "morphology.h"
#include "ops.h"

namespace {

Value apply_op(const OpParams& params, Value* const* in, std::string* note) {
    if (const auto* op = std::get_if<ExposureOp>(&params)) {
        Value out = in[0]->clone();
        adjust_exposure(out.color.view(), op->stops);
        return out;
    }
    if (const auto* op = std::get_if<ContrastOp>(&params)) {
        Value out = in[0]->clone();
        adjust_contrast(out.color.view(), op->amount);
        return out;
    }
    if (const auto* op = std::get_if<GammaOp>(&params)) {
        Value out = in[0]->clone();
        adjust_gamma(out.color.view(), op->gamma);
        return out;
    }
    if (std::get_if<InvertOp>(&params)) {
        Value out = in[0]->clone();
        invert(out.color.view());
        return out;
    }
    if (std::get_if<LuminanceOp>(&params)) {
        return make_scalar(luminance_of(in[0]->color.view()));
    }
    if (const auto* op = std::get_if<ThresholdOp>(&params)) {
        float level = op->level;
        if (op->otsu) {
            level = otsu_threshold(in[0]->scalar.view());
            *note = "Otsu escolheu " + std::to_string(level);
        }
        return make_label(threshold(in[0]->scalar.view(), level));
    }
    if (const auto* op = std::get_if<MorphologyOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        *note = std::to_string(adjacency.offsets.size()) + " vizinhos";
        if (in[0]->kind == ValueKind::Label) {
            return make_label(morphology(in[0]->label.view(), adjacency, op->operation));
        }
        return make_scalar(morphology(in[0]->scalar.view(), adjacency, op->operation));
    }
    if (std::get_if<EqualizeOp>(&params)) {
        Value out = in[0]->clone();
        if (out.kind == ValueKind::Scalar) {
            equalize(out.scalar.view());
        } else {
            equalize(out.color.view());
        }
        return out;
    }
    if (const auto* op = std::get_if<StretchOp>(&params)) {
        Value out = in[0]->clone();
        if (out.kind == ValueKind::Scalar) {
            stretch(out.scalar.view(), op->low, op->high);
        } else {
            stretch(out.color.view(), op->low, op->high);
        }
        return out;
    }
    if (const auto* op = std::get_if<ComponentsOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        int found = 0;
        Map<int32_t> labels = connected_components(in[0]->label.view(), adjacency, &found);
        *note = std::to_string(found) + " componentes com " +
                std::to_string(adjacency.offsets.size()) + " vizinhos";
        return make_label(std::move(labels));
    }
    if (const auto* op = std::get_if<ConvolveOp>(&params)) {
        Kernel kernel = op->kernel;
        if (op->normalize) {
            kernel.normalize();
        }
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(convolve(in[0]->scalar.view(), kernel, op->border, op->flip));
        }
        return make_color(convolve(in[0]->color.view(), kernel, op->border, op->flip));
    }
    if (const auto* op = std::get_if<OverlayOp>(&params)) {
        Value out = in[0]->clone();
        overlay_labels(out.color.view(), in[1]->label.view(), op->opacity);
        return out;
    }
    return Value{};
}

}  // namespace

bool poly_accepts(Poly poly, ValueKind kind) {
    switch (poly) {
        case Poly::ColorOrScalar:
            return kind == ValueKind::Color || kind == ValueKind::Scalar;
        case Poly::ScalarOrLabel:
            return kind == ValueKind::Scalar || kind == ValueKind::Label;
        case Poly::None:
            break;
    }
    return false;
}

OpInfo op_info(const OpParams& params) {
    return std::visit(
        [](auto&& op) -> OpInfo {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, SourceOp>) {
                return {"origem", 0, {}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, ExposureOp>) {
                return {"exposição", 1, {ValueKind::Color}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, ContrastOp>) {
                return {"contraste", 1, {ValueKind::Color}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, GammaOp>) {
                return {"gama", 1, {ValueKind::Color}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, InvertOp>) {
                return {"inverter", 1, {ValueKind::Color}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, LuminanceOp>) {
                return {"luminância", 1, {ValueKind::Color}, ValueKind::Scalar};
            } else if constexpr (std::is_same_v<T, ThresholdOp>) {
                return {"threshold", 1, {ValueKind::Scalar}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, ConvolveOp>) {
                return {"convolução", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, MorphologyOp>) {
                return {"morfologia", 1, {ValueKind::Scalar}, ValueKind::Scalar,
                        Poly::ScalarOrLabel};
            } else if constexpr (std::is_same_v<T, ComponentsOp>) {
                return {"componentes", 1, {ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, EqualizeOp>) {
                return {"equalizar", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, StretchOp>) {
                return {"alongar", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else {
                return {"overlay", 2, {ValueKind::Color, ValueKind::Label}, ValueKind::Color};
            }
        },
        params);
}

Chain::Chain() {
    Stage source;
    source.id = 0;
    source.params = SourceOp{};
    stages.push_back(std::move(source));
}

int Chain::index_of(int id) const {
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (stages[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Chain::add(OpParams params) {
    Stage stage;
    stage.id = next_id++;
    stage.params = std::move(params);

    const OpInfo info = op_info(stage.params);
    // Liga por padrão no último estágio cujo tipo bate; é quase sempre o que a
    // pessoa queria, e quando não é, o combo está ali do lado.
    for (int k = 0; k < info.input_count; ++k) {
        int chosen = 0;
        for (int i = static_cast<int>(stages.size()) - 1; i >= 0; --i) {
            const ValueKind produced = op_info(stages[i].params).output;
            const bool serve = (info.poly != Poly::None) ? poly_accepts(info.poly, produced)
                                                         : (produced == info.inputs[k]);
            if (serve) {
                chosen = stages[i].id;
                break;
            }
        }
        stage.inputs.push_back(chosen);
    }

    stages.push_back(std::move(stage));
    return stages.back().id;
}

void Chain::remove(int id) {
    if (id == 0) {
        return;
    }
    const int idx = index_of(id);
    if (idx < 0) {
        return;
    }
    stages.erase(stages.begin() + idx);
    for (Stage& stage : stages) {
        for (int& input : stage.inputs) {
            if (input == id) {
                input = -1;
            }
        }
    }
}

void Chain::evaluate(const Image& source) {
    outputs.clear();
    outputs.resize(stages.size());

    for (std::size_t i = 0; i < stages.size(); ++i) {
        Stage& stage = stages[i];
        stage.error.clear();
        stage.note.clear();

        if (std::get_if<SourceOp>(&stage.params)) {
            outputs[i] = make_color(source.clone());
            continue;
        }

        const OpInfo info = op_info(stage.params);
        Value* in[2] = {nullptr, nullptr};

        for (int k = 0; k < info.input_count; ++k) {
            const int id = (k < static_cast<int>(stage.inputs.size())) ? stage.inputs[k] : -1;
            const int idx = index_of(id);
            if (idx < 0 || idx >= static_cast<int>(i)) {
                stage.error = "entrada não ligada";
                break;
            }
            if (outputs[idx].empty()) {
                stage.error = "entrada vazia";
                break;
            }
            if (info.poly != Poly::None) {
                if (!poly_accepts(info.poly, outputs[idx].kind)) {
                    stage.error = std::string("não roda sobre ") + kind_name(outputs[idx].kind);
                    break;
                }
            } else if (outputs[idx].kind != info.inputs[k]) {
                stage.error = std::string("espera ") + kind_name(info.inputs[k]) + ", recebeu " +
                              kind_name(outputs[idx].kind);
                break;
            }
            in[k] = &outputs[idx];
        }

        if (!stage.error.empty()) {
            continue;
        }
        if (!stage.enabled) {
            outputs[i] = in[0]->clone();
            continue;
        }
        outputs[i] = apply_op(stage.params, in, &stage.note);
    }
}
