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
    status.clear();
    chain = Chain();
    viewed = 0;
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

void App::upload_view() {
    if (chain.outputs.empty()) {
        return;
    }
    if (viewed < 0 || viewed >= static_cast<int>(chain.outputs.size())) {
        viewed = 0;
    }

    ++revision;
    const Value& value = chain.outputs[viewed];
    if (value.empty()) {
        return;
    }
    upload_rgba8(texture, to_display_rgba8(value, colormap, &view_lo, &view_hi).get(),
                 value.width(), value.height());
}
