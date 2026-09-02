#include "chain.h"

#include <type_traits>
#include <utility>

#include "convolve.h"
#include "histogram.h"
#include "morphology.h"
#include "ops.h"

namespace {

Value apply_op(const OpParams& params, Value* const* in, std::string* note) {
    if (const auto* op = std::get_if<CombineOp>(&params)) {
        Value out = in[0]->clone();
        switch (out.kind) {
            case ValueKind::Color:
                combine(in[0]->color.view(), in[1]->color.view(), op->operation, op->scale,
                        out.color.view());
                break;
            case ValueKind::Scalar:
                combine(in[0]->scalar.view(), in[1]->scalar.view(), op->operation, op->scale,
                        out.scalar.view());
                break;
            case ValueKind::Label:
                combine(in[0]->label.view(), in[1]->label.view(), op->operation, op->scale,
                        out.label.view());
                break;
        }
        *note = combine_name(op->operation);
        return out;
    }
    if (const auto* op = std::get_if<CurveOp>(&params)) {
        Value out = in[0]->clone();
        std::string error;
        const bool ok = out.kind == ValueKind::Scalar
                            ? apply_curve(out.scalar.view(), op->expression, op->a, op->b, op->c,
                                          &error)
                            : apply_curve(out.color.view(), op->expression, op->a, op->b, op->c,
                                          op->on_srgb, &error);
        if (!ok) {
            *note = error;
        }
        return out;
    }
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
    if (const auto* op = std::get_if<ChannelOp>(&params)) {
        return make_scalar(
            channel_of(in[0]->color.view(), op->channel, op->weight, op->on_srgb));
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
    if (const auto* op = std::get_if<ClaheOp>(&params)) {
        Value out = in[0]->clone();
        if (out.kind == ValueKind::Scalar) {
            clahe(out.scalar.view(), op->tiles, op->clip);
        } else {
            clahe(out.color.view(), op->tiles, op->clip);
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
        case Poly::Pair:
            return true;
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
            } else if constexpr (std::is_same_v<T, ChannelOp>) {
                return {"canal", 1, {ValueKind::Color}, ValueKind::Scalar};
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
            } else if constexpr (std::is_same_v<T, CombineOp>) {
                return {"combinar", 2, {ValueKind::Color, ValueKind::Color}, ValueKind::Color,
                        Poly::Pair};
            } else if constexpr (std::is_same_v<T, CurveOp>) {
                return {"curva", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, ClaheOp>) {
                return {"clahe", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
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

ValueKind Chain::kind_of(int index) const {
    if (index < 0 || index >= static_cast<int>(stages.size())) {
        return ValueKind::Color;
    }
    if (index < static_cast<int>(outputs.size()) && !outputs[index].empty()) {
        return outputs[index].kind;
    }
    return op_info(stages[index].params).output;
}

int Chain::find_input(const OpInfo& info, int k, int prefer_id) const {
    const auto serves = [&](int index) {
        const ValueKind produced = kind_of(index);
        return (info.poly != Poly::None) ? poly_accepts(info.poly, produced)
                                         : (produced == info.inputs[k]);
    };

    if (prefer_id >= 0) {
        const int index = index_of(prefer_id);
        if (index >= 0 && serves(index)) {
            return prefer_id;
        }
    }
    for (int i = static_cast<int>(stages.size()) - 1; i >= 0; --i) {
        if (serves(i)) {
            return stages[i].id;
        }
    }
    return -1;
}

bool Chain::can_add(const OpParams& params) const {
    const OpInfo info = op_info(params);
    for (int k = 0; k < info.input_count; ++k) {
        if (find_input(info, k, -1) < 0) {
            return false;
        }
    }
    return true;
}

int Chain::add(OpParams params, int prefer_id) {
    Stage stage;
    stage.id = next_id++;
    stage.params = std::move(params);

    const OpInfo info = op_info(stage.params);
    for (int k = 0; k < info.input_count; ++k) {
        stage.inputs.push_back(find_input(info, k, prefer_id));
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

        if (stage.error.empty() && info.poly == Poly::Pair && in[0] && in[1] &&
            in[0]->kind != in[1]->kind) {
            stage.error = std::string("tipos diferentes: ") + kind_name(in[0]->kind) + " e " +
                          kind_name(in[1]->kind);
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

bool bridge_for(const Chain& chain, const OpParams& params, OpParams* bridge) {
    const OpInfo info = op_info(params);
    if (info.input_count != 1 || chain.can_add(params)) {
        return false;
    }

    // Otsu na ponte porque um limiar automático dá resultado na hora; trocar
    // pra manual depois é um clique.
    const OpParams candidates[] = {ChannelOp{}, ThresholdOp{0.5f, true}};
    for (const OpParams& candidate : candidates) {
        if (!chain.can_add(candidate)) {
            continue;
        }
        const ValueKind produced = op_info(candidate).output;
        const bool serve = (info.poly != Poly::None) ? poly_accepts(info.poly, produced)
                                                     : (produced == info.inputs[0]);
        if (serve) {
            *bridge = candidate;
            return true;
        }
    }
    return false;
}
