#include "app.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

bool App::open(const std::string& file, bool keep_chain) {
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
    if (!keep_chain) {
        chain = Chain();
        viewed = 0;
        pinned = -1;
    }
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

std::string App::label() const {
    if (!project_path.empty()) {
        return std::filesystem::path(project_path).filename().string();
    }
    if (!path.empty()) {
        return std::filesystem::path(path).filename().string();
    }
    return "sem título";
}

Workspace::Workspace() { docs.push_back(std::make_unique<App>()); }

App& Workspace::doc() {
    active = std::clamp(active, 0, static_cast<int>(docs.size()) - 1);
    return *docs[active];
}

const App& Workspace::doc() const {
    const int i = std::clamp(active, 0, static_cast<int>(docs.size()) - 1);
    return *docs[i];
}

bool Workspace::doc_is_pristine() const {
    const App& d = doc();
    return d.source.empty() && d.chain.stages.size() <= 1 && d.project_path.empty();
}

App& Workspace::open_tab() {
    if (doc_is_pristine()) {
        return doc();
    }
    docs.push_back(std::make_unique<App>());
    active = static_cast<int>(docs.size()) - 1;
    return *docs.back();
}

void Workspace::close_tab(int index) {
    if (index < 0 || index >= static_cast<int>(docs.size())) {
        return;
    }
    docs.erase(docs.begin() + index);
    if (docs.empty()) {
        docs.push_back(std::make_unique<App>());
    }
    if (active >= static_cast<int>(docs.size())) {
        active = static_cast<int>(docs.size()) - 1;
    }
}
