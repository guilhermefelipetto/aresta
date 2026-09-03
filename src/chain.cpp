#include "chain.h"

#include <algorithm>
#include <cstdio>
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
    if (const auto* op = std::get_if<RankOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        *note = std::to_string(adjacency.offsets.size() + 1) + " amostras por pixel";
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(rank_filter(in[0]->scalar.view(), adjacency, op->kind, op->alpha));
        }
        return make_color(rank_filter(in[0]->color.view(), adjacency, op->kind, op->alpha));
    }
    if (std::get_if<MatchOp>(&params)) {
        Value out = in[0]->clone();
        if (out.kind == ValueKind::Scalar) {
            match_histogram(out.scalar.view(), in[1]->scalar.view());
        } else {
            match_histogram(out.color.view(), in[1]->color.view());
        }
        return out;
    }
    if (const auto* op = std::get_if<NoiseOp>(&params)) {
        Value out = in[0]->clone();
        if (out.kind == ValueKind::Scalar) {
            add_noise(out.scalar.view(), op->kind, op->a, op->b, op->c,
                      static_cast<unsigned>(op->seed));
        } else {
            add_noise(out.color.view(), op->kind, op->a, op->b, op->c,
                      static_cast<unsigned>(op->seed));
        }
        return out;
    }
    if (const auto* op = std::get_if<MeanOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(mean_filter(in[0]->scalar.view(), adjacency, op->kind, op->q));
        }
        return make_color(mean_filter(in[0]->color.view(), adjacency, op->kind, op->q));
    }
    if (const auto* op = std::get_if<AdaptiveOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(
                adaptive_denoise(in[0]->scalar.view(), adjacency, op->noise_variance));
        }
        return make_color(adaptive_denoise(in[0]->color.view(), adjacency, op->noise_variance));
    }
    if (const auto* op = std::get_if<AdaptiveMedianOp>(&params)) {
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(adaptive_median(in[0]->scalar.view(), op->max_radius));
        }
        return make_color(adaptive_median(in[0]->color.view(), op->max_radius));
    }
    if (const auto* op = std::get_if<DegradeOp>(&params)) {
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(
                degrade(in[0]->scalar.view(), op->kind, op->dx, op->dy, op->k, op->pad));
        }
        return make_color(degrade(in[0]->color.view(), op->kind, op->dx, op->dy, op->k, op->pad));
    }
    if (const auto* op = std::get_if<RestoreOp>(&params)) {
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(restore(in[0]->scalar.view(), op->method, op->kind, op->dx, op->dy,
                                       op->k, op->parameter, op->limit, op->pad));
        }
        return make_color(restore(in[0]->color.view(), op->method, op->kind, op->dx, op->dy, op->k,
                                  op->parameter, op->limit, op->pad));
    }
    if (const auto* op = std::get_if<SpectrumOp>(&params)) {
        const Map<float> plano = in[0]->kind == ValueKind::Scalar
                                     ? Map<float>{}
                                     : in[0]->color.luma();
        const MapView<float> fonte =
            in[0]->kind == ValueKind::Scalar ? in[0]->scalar.view() : plano.view();
        const Spectrum spectrum = forward_fft(fonte, op->pad);
        *note = std::to_string(spectrum.width) + "x" + std::to_string(spectrum.height) +
                " depois do preenchimento";
        return make_scalar(spectrum_magnitude(spectrum, op->logarithmic));
    }
    if (const auto* op = std::get_if<FreqFilterOp>(&params)) {
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(filter_frequency(in[0]->scalar.view(), op->shape, op->kind,
                                                op->cutoff, op->order, op->width, op->pad));
        }
        return make_color(filter_frequency(in[0]->color.view(), op->shape, op->kind, op->cutoff,
                                           op->order, op->width, op->pad));
    }
    if (const auto* op = std::get_if<BitPlaneOp>(&params)) {
        if (in[0]->kind == ValueKind::Scalar) {
            return make_scalar(bit_plane(in[0]->scalar.view(), op->plane));
        }
        return make_scalar(bit_plane(in[0]->color.view(), op->plane));
    }
    if (const auto* op = std::get_if<ComposeOp>(&params)) {
        return make_color(compose(op->space, in[0]->scalar.view(), in[1]->scalar.view(),
                                  in[2]->scalar.view()));
    }
    if (const auto* op = std::get_if<ColorGradientOp>(&params)) {
        return make_scalar(color_gradient(in[0]->color.view(), op->space));
    }
    if (const auto* op = std::get_if<ColorDistanceOp>(&params)) {
        return make_scalar(color_distance(in[0]->color.view(), op->space, op->reference));
    }
    if (const auto* op = std::get_if<PseudoColorOp>(&params)) {
        return make_color(pseudo_color(in[0]->scalar.view(), op->map));
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
        switch (out.kind) {
            case ValueKind::Color:
                invert(out.color.view());
                break;
            case ValueKind::Scalar:
                invert(out.scalar.view());
                break;
            case ValueKind::Label:
                invert(out.label.view());
                break;
        }
        return out;
    }
    if (const auto* op = std::get_if<ChannelOp>(&params)) {
        return make_scalar(channel_of(in[0]->color.view(), op->space, op->component, op->weight,
                                      op->on_srgb));
    }
    if (const auto* op = std::get_if<ThresholdOp>(&params)) {
        float lo = 0.0f;
        float hi = 1.0f;
        scalar_range(in[0]->scalar.view(), &lo, &hi);

        float level = 0.0f;
        char aviso[160];
        if (op->otsu) {
            level = otsu_threshold(in[0]->scalar.view());
            const float span = (hi > lo) ? (hi - lo) : 1.0f;
            std::snprintf(aviso, sizeof(aviso),
                          "Otsu escolheu %.4f, que é %.4f da faixa, ou nível %.0f de %d", level,
                          (level - lo) / span, (level - lo) / span * ((1 << op->bits) - 1),
                          (1 << op->bits) - 1);
        } else {
            level = threshold_absolute(*op, lo, hi);
            std::snprintf(aviso, sizeof(aviso), "corta em %.4f, e o mapa vai de %.4f a %.4f",
                          level, lo, hi);
        }
        *note = aviso;
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
        const float fatia = out.kind == ValueKind::Scalar ? dominant_share(out.scalar.view())
                                                         : dominant_share(out.color.view());
        if (out.kind == ValueKind::Scalar) {
            equalize(out.scalar.view());
        } else {
            equalize(out.color.view());
        }
        if (fatia > 0.25f) {
            char aviso[128];
            std::snprintf(aviso, sizeof(aviso),
                          "%.0f%% dos pixels num valor só: a curva salta aí e eles saem juntos",
                          fatia * 100.0f);
            *note = aviso;
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
    if (const auto* op = std::get_if<CannyOp>(&params)) {
        return make_label(canny(in[0]->scalar.view(), op->sigma, op->low, op->high));
    }
    if (const auto* op = std::get_if<LogEdgeOp>(&params)) {
        return make_label(log_zero_crossings(in[0]->scalar.view(), op->sigma, op->slope));
    }
    if (const auto* op = std::get_if<AdaptiveThresholdOp>(&params)) {
        return make_label(adaptive_threshold(in[0]->scalar.view(), op->kind, op->radius,
                                             op->offset, op->k));
    }
    if (const auto* op = std::get_if<MultiOtsuOp>(&params)) {
        std::vector<float> niveis;
        Map<int32_t> saida = multi_otsu(in[0]->scalar.view(), op->classes, &niveis);
        std::string texto = "cortes em";
        for (float nivel : niveis) {
            texto += " " + std::to_string(nivel).substr(0, 6);
        }
        *note = texto;
        return make_label(std::move(saida));
    }
    if (const auto* op = std::get_if<DistanceOp>(&params)) {
        return make_scalar(distance_transform(in[0]->label.view(), op->inside));
    }
    if (const auto* op = std::get_if<ReconstructOp>(&params)) {
        return make_label(reconstruct(in[0]->label.view(), in[1]->label.view(),
                                      adjacency_by_radius(op->radius)));
    }
    if (const auto* op = std::get_if<FillHolesOp>(&params)) {
        return make_label(fill_holes(in[0]->label.view(), adjacency_by_radius(op->radius)));
    }
    if (const auto* op = std::get_if<ThinOp>(&params)) {
        return make_label(thinning(in[0]->label.view(), op->kind, op->iterations));
    }
    if (const auto* op = std::get_if<HitMissOp>(&params)) {
        return make_label(hit_or_miss(in[0]->label.view(), op->pattern));
    }
    if (const auto* op = std::get_if<ComponentsOp>(&params)) {
        const Adjacency adjacency = adjacency_by_radius(op->radius);
        std::vector<Region> todas;
        std::vector<Region> sobraram;
        Map<int32_t> labels =
            label_and_filter(in[0]->label.view(), adjacency, op->filter, &todas, &sobraram);
        *note = std::to_string(sobraram.size()) + " de " + std::to_string(todas.size()) +
                " componentes, " + std::to_string(adjacency.offsets.size()) + " vizinhos";
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

const char* threshold_unit_name(ThresholdUnit unit) {
    switch (unit) {
        case ThresholdUnit::Absolute: return "absoluto";
        case ThresholdUnit::Fraction: return "fração";
        case ThresholdUnit::Level: return "nível";
    }
    return "?";
}

float threshold_absolute(const ThresholdOp& op, float lo, float hi) {
    const float span = (hi > lo) ? (hi - lo) : 1.0f;
    switch (op.unit) {
        case ThresholdUnit::Absolute:
            return op.level;
        case ThresholdUnit::Fraction:
            return lo + op.level * span;
        default:
            return lo + op.level / static_cast<float>((1 << op.bits) - 1) * span;
    }
}

bool poly_accepts(Poly poly, ValueKind kind) {
    switch (poly) {
        case Poly::ColorOrScalar:
            return kind == ValueKind::Color || kind == ValueKind::Scalar;
        case Poly::ScalarOrLabel:
            return kind == ValueKind::Scalar || kind == ValueKind::Label;
        case Poly::Any:
            return true;
        case Poly::Pair:
            return true;
        case Poly::PairTone:
            return kind != ValueKind::Label;
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
                return {"inverter", 1, {ValueKind::Color}, ValueKind::Color, Poly::Any};
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
            } else if constexpr (std::is_same_v<T, CannyOp>) {
                return {"canny", 1, {ValueKind::Scalar}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, LogEdgeOp>) {
                return {"zero-crossings", 1, {ValueKind::Scalar}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, AdaptiveThresholdOp>) {
                return {"limiar local", 1, {ValueKind::Scalar}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, MultiOtsuOp>) {
                return {"multi-Otsu", 1, {ValueKind::Scalar}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, DistanceOp>) {
                return {"distância", 1, {ValueKind::Label}, ValueKind::Scalar};
            } else if constexpr (std::is_same_v<T, ReconstructOp>) {
                return {"reconstruir", 2, {ValueKind::Label, ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, FillHolesOp>) {
                return {"preencher buracos", 1, {ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, ThinOp>) {
                return {"afinar", 1, {ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, HitMissOp>) {
                return {"hit-or-miss", 1, {ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, ComponentsOp>) {
                return {"componentes", 1, {ValueKind::Label}, ValueKind::Label};
            } else if constexpr (std::is_same_v<T, EqualizeOp>) {
                return {"equalizar", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, CombineOp>) {
                return {"combinar", 2, {ValueKind::Color, ValueKind::Color}, ValueKind::Color,
                        Poly::Pair};
            } else if constexpr (std::is_same_v<T, RankOp>) {
                return {"ordem", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, MatchOp>) {
                return {"casar histograma", 2, {ValueKind::Color, ValueKind::Color},
                        ValueKind::Color, Poly::PairTone};
            } else if constexpr (std::is_same_v<T, NoiseOp>) {
                return {"ruído", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, MeanOp>) {
                return {"média", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, AdaptiveOp>) {
                return {"redução adaptativa", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, AdaptiveMedianOp>) {
                return {"mediana adaptativa", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, DegradeOp>) {
                return {"degradar", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, RestoreOp>) {
                return {"restaurar", 1, {ValueKind::Color}, ValueKind::Color, Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, SpectrumOp>) {
                return {"espectro", 1, {ValueKind::Color}, ValueKind::Scalar,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, FreqFilterOp>) {
                return {"filtro de frequência", 1, {ValueKind::Color}, ValueKind::Color,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, BitPlaneOp>) {
                return {"plano de bit", 1, {ValueKind::Color}, ValueKind::Scalar,
                        Poly::ColorOrScalar};
            } else if constexpr (std::is_same_v<T, ColorGradientOp>) {
                return {"gradiente de cor", 1, {ValueKind::Color}, ValueKind::Scalar};
            } else if constexpr (std::is_same_v<T, ColorDistanceOp>) {
                return {"distância de cor", 1, {ValueKind::Color}, ValueKind::Scalar};
            } else if constexpr (std::is_same_v<T, PseudoColorOp>) {
                return {"pseudo-cor", 1, {ValueKind::Scalar}, ValueKind::Color};
            } else if constexpr (std::is_same_v<T, ComposeOp>) {
                return {"compor", 3, {ValueKind::Scalar, ValueKind::Scalar, ValueKind::Scalar},
                        ValueKind::Color};
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

bool Chain::can_feed(const OpInfo& info, int k, int index) const {
    if (index < 0 || index >= static_cast<int>(stages.size())) {
        return false;
    }
    const ValueKind produced = kind_of(index);
    return (info.poly != Poly::None) ? poly_accepts(info.poly, produced)
                                     : (produced == info.inputs[k]);
}

int Chain::find_input(const OpInfo& info, int k, int prefer_id, int limit) const {
    if (prefer_id >= 0) {
        const int index = index_of(prefer_id);
        if (index >= 0 && index < limit && can_feed(info, k, index)) {
            return prefer_id;
        }
    }
    for (int i = limit - 1; i >= 0; --i) {
        if (can_feed(info, k, i)) {
            return stages[i].id;
        }
    }
    return -1;
}

bool Chain::can_add(const OpParams& params) const {
    const OpInfo info = op_info(params);
    for (int k = 0; k < info.input_count; ++k) {
        if (find_input(info, k, -1, static_cast<int>(stages.size())) < 0) {
            return false;
        }
    }
    return true;
}

void Chain::wire_inputs(const OpParams& params_of_info, int prefer_id, int limit,
                        std::vector<int>* inputs) const {
    const OpInfo info = op_info(params_of_info);
    inputs->assign(static_cast<std::size_t>(info.input_count), -1);
    if (info.input_count <= 0) {
        return;
    }

    // Entradas que pedem tipos diferentes cada uma acha a sua sozinha.
    bool mesmo_tipo = true;
    for (int k = 1; k < info.input_count && mesmo_tipo; ++k) {
        mesmo_tipo = info.inputs[k] == info.inputs[0];
    }
    if (!mesmo_tipo || info.input_count == 1) {
        for (int k = 0; k < info.input_count; ++k) {
            (*inputs)[static_cast<std::size_t>(k)] = find_input(info, k, prefer_id, limit);
        }
        return;
    }

    // Compor sabe o que quer: se existirem canais do mesmo espaço, cada slot
    // pega o seu, em vez de sair chutando pela ordem da lista.
    if (const auto* compor = std::get_if<ComposeOp>(&params_of_info)) {
        bool achou_todos = true;
        std::vector<int> escolhidos(3, -1);
        for (int k = 0; k < 3 && achou_todos; ++k) {
            for (int i = limit - 1; i >= 0; --i) {
                const auto* canal = std::get_if<ChannelOp>(&stages[static_cast<std::size_t>(i)].params);
                if (canal && canal->space == compor->space && canal->component == k) {
                    escolhidos[static_cast<std::size_t>(k)] = stages[static_cast<std::size_t>(i)].id;
                    break;
                }
            }
            achou_todos = escolhidos[static_cast<std::size_t>(k)] >= 0;
        }
        if (achou_todos) {
            *inputs = escolhidos;
            return;
        }
    }

    std::vector<int> candidatos;
    for (int i = 0; i < limit; ++i) {
        if (can_feed(info, 0, i)) {
            candidatos.push_back(stages[static_cast<std::size_t>(i)].id);
        }
    }
    if (candidatos.empty()) {
        return;
    }

    const int sobra = static_cast<int>(candidatos.size()) - info.input_count;
    for (int k = 0; k < info.input_count; ++k) {
        const int at = std::max(0, sobra + k);
        (*inputs)[static_cast<std::size_t>(k)] =
            candidatos[static_cast<std::size_t>(std::min(at, static_cast<int>(candidatos.size()) - 1))];
    }
}

int Chain::add(OpParams params, int prefer_id, int position) {
    const int limit = (position < 0 || position > static_cast<int>(stages.size()))
                          ? static_cast<int>(stages.size())
                          : position;

    Stage stage;
    stage.id = next_id++;
    stage.params = std::move(params);
    wire_inputs(stage.params, prefer_id, limit, &stage.inputs);

    const int id = stage.id;
    stages.insert(stages.begin() + limit, std::move(stage));
    return id;
}

bool Chain::move_stage(int id, int delta) {
    const int from = index_of(id);
    const int to = from + delta;
    if (from <= 0 || to <= 0 || to >= static_cast<int>(stages.size())) {
        return false;
    }

    std::swap(stages[static_cast<std::size_t>(from)], stages[static_cast<std::size_t>(to)]);
    for (std::size_t i = 0; i < stages.size(); ++i) {
        for (int input : stages[i].inputs) {
            const int at = index_of(input);
            if (at >= static_cast<int>(i)) {
                std::swap(stages[static_cast<std::size_t>(from)],
                          stages[static_cast<std::size_t>(to)]);
                return false;
            }
        }
    }
    return true;
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
        Value* in[3] = {nullptr, nullptr, nullptr};

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

        if (stage.error.empty() &&
            (info.poly == Poly::Pair || info.poly == Poly::PairTone)) {
            for (int k = 1; k < info.input_count; ++k) {
                if (in[0] && in[k] && in[0]->kind != in[k]->kind) {
                    stage.error = std::string("tipos diferentes: ") + kind_name(in[0]->kind) +
                                  " e " + kind_name(in[k]->kind);
                    break;
                }
            }
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

const char* input_label(const OpParams& params, int k) {
    if (const auto* op = std::get_if<ComposeOp>(&params)) {
        return component_name(op->space, k);
    }
    if (std::get_if<OverlayOp>(&params)) {
        return k == 0 ? "cor" : "rótulos";
    }
    if (std::get_if<MatchOp>(&params)) {
        return k == 0 ? "origem" : "alvo";
    }
    if (std::get_if<CombineOp>(&params)) {
        return k == 0 ? "a" : "b";
    }
    if (std::get_if<ReconstructOp>(&params)) {
        return k == 0 ? "marcador" : "máscara";
    }
    return "entrada";
}

std::string stage_summary(const OpParams& params) {
    char buffer[192] = {};

    if (const auto* op = std::get_if<ExposureOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%+.2f EV", op->stops);
    } else if (const auto* op = std::get_if<ContrastOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%+.2f", op->amount);
    } else if (const auto* op = std::get_if<GammaOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "gama %.2f", op->gamma);
    } else if (const auto* op = std::get_if<ChannelOp>(&params)) {
        if (op->space == Space::RGB && op->component == channel_luma) {
            std::snprintf(buffer, sizeof(buffer), "luminância %.3f/%.3f/%.3f%s", op->weight[0],
                          op->weight[1], op->weight[2], op->on_srgb ? ", sRGB" : "");
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, %s", space_name(op->space),
                          component_name(op->space, op->component));
        }
    } else if (const auto* op = std::get_if<ComposeOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%s", space_name(op->space));
    } else if (const auto* op = std::get_if<ColorGradientOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "em %s", space_name(op->space));
    } else if (const auto* op = std::get_if<ColorDistanceOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "em %s, alvo %.2f %.2f %.2f", space_name(op->space),
                      op->reference[0], op->reference[1], op->reference[2]);
    } else if (const auto* op = std::get_if<ThresholdOp>(&params)) {
        if (op->otsu) {
            std::snprintf(buffer, sizeof(buffer), "Otsu");
        } else if (op->unit == ThresholdUnit::Level) {
            std::snprintf(buffer, sizeof(buffer), "acima do nível %.0f de %d", op->level,
                          (1 << op->bits) - 1);
        } else {
            std::snprintf(buffer, sizeof(buffer), "acima de %.4f (%s)", op->level,
                          threshold_unit_name(op->unit));
        }
    } else if (const auto* op = std::get_if<MorphologyOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%s, raio %.2f", morph_name(op->operation),
                      op->radius);
    } else if (const auto* op = std::get_if<CannyOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "sigma %.2f, %.3f a %.3f", op->sigma, op->low,
                      op->high);
    } else if (const auto* op = std::get_if<LogEdgeOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "sigma %.2f, corte %.3f", op->sigma, op->slope);
    } else if (const auto* op = std::get_if<AdaptiveThresholdOp>(&params)) {
        if (op->kind == LocalThreshold::Sauvola) {
            std::snprintf(buffer, sizeof(buffer), "Sauvola, raio %.0f, k %.2f", op->radius, op->k);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, raio %.0f, desconta %.3f",
                          local_threshold_name(op->kind), op->radius, op->offset);
        }
    } else if (const auto* op = std::get_if<MultiOtsuOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%d classes", op->classes);
    } else if (const auto* op = std::get_if<DistanceOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), op->inside ? "de dentro até o fundo"
                                                         : "do fundo até o objeto");
    } else if (const auto* op = std::get_if<ThinOp>(&params)) {
        if (op->kind == Thin::Skeleton) {
            std::snprintf(buffer, sizeof(buffer), "esqueleto");
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, %d rodada%s", thin_name(op->kind),
                          op->iterations, op->iterations == 1 ? "" : "s");
        }
    } else if (const auto* op = std::get_if<FillHolesOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "raio %.2f", op->radius);
    } else if (const auto* op = std::get_if<ReconstructOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "raio %.2f", op->radius);
    } else if (const auto* op = std::get_if<ComponentsOp>(&params)) {
        std::string filtros;
        const auto& f = op->filter;
        if (f.only_label > 0) {
            filtros += ", só o " + std::to_string(f.only_label);
        } else {
            if (f.min_area > 0) filtros += ", área >= " + std::to_string(f.min_area);
            if (f.max_area > 0) filtros += ", área <= " + std::to_string(f.max_area);
            if (f.keep_largest > 0) filtros += ", " + std::to_string(f.keep_largest) + " maiores";
            if (f.drop_border) filtros += ", sem borda";
        }
        std::snprintf(buffer, sizeof(buffer), "raio %.2f%s", op->radius, filtros.c_str());
    } else if (const auto* op = std::get_if<StretchOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "percentis %.2f a %.2f", op->low, op->high);
    } else if (const auto* op = std::get_if<ClaheOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%d pedaços, corte %.2f", op->tiles, op->clip);
    } else if (const auto* op = std::get_if<ConvolveOp>(&params)) {
        if (const char* conhecido = builtin_kernel_name(op->kernel)) {
            std::snprintf(buffer, sizeof(buffer), "%s, %s%s", conhecido, border_name(op->border),
                          op->flip ? ", espelhado" : "");
        } else {
            std::snprintf(buffer, sizeof(buffer), "%dx%d, soma %+.3f, %s%s", op->kernel.width,
                          op->kernel.height, op->kernel.sum(), border_name(op->border),
                          op->flip ? ", espelhado" : "");
        }
    } else if (const auto* op = std::get_if<CurveOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%s   a=%.3f b=%.3f", op->expression, op->a, op->b);
    } else if (const auto* op = std::get_if<CombineOp>(&params)) {
        if (op->scale == 1.0f) {
            std::snprintf(buffer, sizeof(buffer), "%s", combine_name(op->operation));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, escala %.3f", combine_name(op->operation),
                          op->scale);
        }
    } else if (const auto* op = std::get_if<RankOp>(&params)) {
        if (op->kind == Rank::AlphaTrimmed) {
            std::snprintf(buffer, sizeof(buffer), "%s, raio %.2f, corta %.0f%%",
                          rank_name(op->kind), op->radius, op->alpha * 100.0f);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, raio %.2f", rank_name(op->kind),
                          op->radius);
        }
    } else if (const auto* op = std::get_if<NoiseOp>(&params)) {
        switch (op->kind) {
            case Noise::Gaussian:
                std::snprintf(buffer, sizeof(buffer), "gaussiano, média %.3f, desvio %.3f, semente %d",
                              op->a, op->b, op->seed);
                break;
            case Noise::SaltPepper:
                std::snprintf(buffer, sizeof(buffer), "sal e pimenta, %.1f%% e %.1f%%, semente %d",
                              op->a * 100.0f, op->b * 100.0f, op->seed);
                break;
            case Noise::Periodic:
                std::snprintf(buffer, sizeof(buffer), "periódico, amplitude %.3f, %.0f x %.0f ciclos",
                              op->a, op->b, op->c);
                break;
            default:
                std::snprintf(buffer, sizeof(buffer), "%s, a=%.3f b=%.3f, semente %d",
                              noise_name(op->kind), op->a, op->b, op->seed);
                break;
        }
    } else if (const auto* op = std::get_if<MeanOp>(&params)) {
        if (op->kind == Mean::Contraharmonic) {
            std::snprintf(buffer, sizeof(buffer), "contra-harmônica, raio %.2f, Q %+.2f",
                          op->radius, op->q);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s, raio %.2f", mean_name(op->kind),
                          op->radius);
        }
    } else if (const auto* op = std::get_if<AdaptiveOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "raio %.2f, variância do ruído %.5f", op->radius,
                      op->noise_variance);
    } else if (const auto* op = std::get_if<AdaptiveMedianOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "raio máximo %.2f", op->max_radius);
    } else if (const auto* op = std::get_if<DegradeOp>(&params)) {
        if (op->kind == Degradation::Motion) {
            std::snprintf(buffer, sizeof(buffer), "movimento %.1f x %.1f px", op->dx, op->dy);
        } else {
            std::snprintf(buffer, sizeof(buffer), "turbulência k=%.2f", op->k);
        }
    } else if (const auto* op = std::get_if<RestoreOp>(&params)) {
        char suposto[64];
        if (op->kind == Degradation::Motion) {
            std::snprintf(suposto, sizeof(suposto), "movimento %.1f x %.1f", op->dx, op->dy);
        } else {
            std::snprintf(suposto, sizeof(suposto), "turbulência k=%.2f", op->k);
        }
        switch (op->method) {
            case Restoration::Inverse:
                std::snprintf(buffer, sizeof(buffer), "inverso, corte %.3f, supondo %s",
                              op->limit, suposto);
                break;
            case Restoration::Wiener:
                std::snprintf(buffer, sizeof(buffer), "Wiener, K %.5f, supondo %s", op->parameter,
                              suposto);
                break;
            default:
                std::snprintf(buffer, sizeof(buffer), "mín. quadrados, gama %.5f, supondo %s",
                              op->parameter, suposto);
                break;
        }
    } else if (const auto* op = std::get_if<SpectrumOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "%s%s", pad_name(op->pad),
                      op->logarithmic ? ", log" : "");
    } else if (const auto* op = std::get_if<FreqFilterOp>(&params)) {
        if (op->kind == FreqKind::BandPass || op->kind == FreqKind::BandReject) {
            std::snprintf(buffer, sizeof(buffer), "%s %s, corte %.3f, largura %.3f",
                          freq_shape_name(op->shape), freq_kind_name(op->kind), op->cutoff,
                          op->width);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s %s, corte %.3f", freq_shape_name(op->shape),
                          freq_kind_name(op->kind), op->cutoff);
        }
    } else if (const auto* op = std::get_if<BitPlaneOp>(&params)) {
        std::snprintf(buffer, sizeof(buffer), "bit %d", op->plane);
    }

    return buffer;
}
