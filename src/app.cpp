#include "app.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "ops.h"

bool App::open(const std::string& file) {
    std::string error;
    Image loaded = load_image(file, error);
    if (loaded.empty()) {
        status = "Não abriu " + file + ": " + error;
        return false;
    }

    source = std::move(loaded);
    path = file;
    status.clear();
    reset_adjustments();
    canvas.needs_fit = true;
    reapply();
    return true;
}

void App::reset_adjustments() {
    exposure = 0.0f;
    contrast = 0.0f;
    gamma = 1.0f;
}

void App::reapply() {
    if (source.empty()) {
        return;
    }

    working = source.clone();
    const ImageView view = working.view();
    adjust_exposure(view, exposure);
    adjust_contrast(view, contrast);
    adjust_gamma(view, gamma);

    const Map<float> luma = working.luma();
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    const float* values = luma.data.get();
    for (std::size_t i = 0; i < luma.count(); ++i) {
        lo = std::min(lo, values[i]);
        hi = std::max(hi, values[i]);
        sum += values[i];
    }
    luma_min = lo;
    luma_max = hi;
    luma_mean = static_cast<float>(sum / static_cast<double>(luma.count()));

    upload_rgba8(texture, to_srgb8(working).get(), working.width, working.height);
}
