#include "app.h"

#include <utility>

bool App::open(const std::string& file) {
    std::string error;
    Image loaded = load_image(file, error);
    if (loaded.empty()) {
        status = "Não abriu " + file + ": " + error;
        return false;
    }

    source = std::move(loaded);
    path = file;
    if (!probe_image(file, &source_channels, &source_bits)) {
        source_channels = 0;
        source_bits = 0;
    }
    status.clear();
    chain = Chain();
    viewed = 0;
    pinned = -1;
    canvas.needs_fit = true;
    evaluate();
    return true;
}

void App::evaluate() {
    if (source.empty()) {
        return;
    }
    chain.evaluate(source);
    upload_view();
}

int App::shown() const {
    const int fixed = chain.index_of(pinned);
    if (fixed >= 0 && fixed < static_cast<int>(chain.outputs.size())) {
        return fixed;
    }
    return viewed;
}

void App::upload_view() {
    if (chain.outputs.empty()) {
        return;
    }
    if (viewed < 0 || viewed >= static_cast<int>(chain.outputs.size())) {
        viewed = 0;
    }

    ++revision;
    const Value& value = chain.outputs[shown()];
    if (value.empty()) {
        return;
    }
    upload_rgba8(texture, to_display_rgba8(value, colormap, &view_lo, &view_hi).get(),
                 value.width(), value.height());
}
