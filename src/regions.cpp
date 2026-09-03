#include "regions.h"

#include <algorithm>
#include <unordered_map>

#include "morphology.h"

std::vector<Region> measure_regions(MapView<int32_t> labels) {
    int highest = 0;
    for (int y = 0; y < labels.height; ++y) {
        const int32_t* row = labels.row(y);
        for (int x = 0; x < labels.width; ++x) {
            highest = std::max(highest, row[x]);
        }
    }
    if (highest <= 0) {
        return {};
    }

    std::vector<Region> regions(static_cast<std::size_t>(highest));
    for (int i = 0; i < highest; ++i) {
        regions[static_cast<std::size_t>(i)].label = i + 1;
    }

    for (int y = 0; y < labels.height; ++y) {
        const bool linha_de_borda = y == 0 || y == labels.height - 1;
        for (int x = 0; x < labels.width; ++x) {
            const int32_t label = labels.at(x, y);
            if (label <= 0) {
                continue;
            }
            Region& region = regions[static_cast<std::size_t>(label - 1)];
            ++region.area;
            if (linha_de_borda || x == 0 || x == labels.width - 1) {
                region.touches_border = true;
            }
        }
    }

    std::vector<Region> out;
    for (const Region& region : regions) {
        if (region.area > 0) {
            out.push_back(region);
        }
    }
    return out;
}

Map<int32_t> label_and_filter(MapView<int32_t> source, const Adjacency& adjacency,
                              const ComponentFilter& filter, std::vector<Region>* all,
                              std::vector<Region>* kept) {
    int count = 0;
    Map<int32_t> labels = connected_components(source, adjacency, &count);
    std::vector<Region> regions = measure_regions(labels.view());
    if (all) {
        *all = regions;
    }

    std::vector<Region> survivors;
    for (const Region& region : regions) {
        if (region.area < filter.min_area) {
            continue;
        }
        if (filter.max_area > 0 && region.area > filter.max_area) {
            continue;
        }
        if (filter.drop_border && region.touches_border) {
            continue;
        }
        if (filter.only_label > 0 && region.label != filter.only_label) {
            continue;
        }
        survivors.push_back(region);
    }

    std::sort(survivors.begin(), survivors.end(),
              [](const Region& a, const Region& b) { return a.area > b.area; });
    if (filter.keep_largest > 0 &&
        survivors.size() > static_cast<std::size_t>(filter.keep_largest)) {
        survivors.resize(static_cast<std::size_t>(filter.keep_largest));
    }
    if (!filter.renumber_by_area) {
        std::sort(survivors.begin(), survivors.end(),
                  [](const Region& a, const Region& b) { return a.label < b.label; });
    }

    std::unordered_map<int32_t, int32_t> renumber;
    for (std::size_t i = 0; i < survivors.size(); ++i) {
        renumber[survivors[i].label] = static_cast<int32_t>(i + 1);
    }

    Map<int32_t> out(labels.width, labels.height);
    for (int y = 0; y < labels.height; ++y) {
        const int32_t* row = labels.view().row(y);
        int32_t* q = out.view().row(y);
        for (int x = 0; x < labels.width; ++x) {
            const auto found = renumber.find(row[x]);
            q[x] = found == renumber.end() ? 0 : found->second;
        }
    }

    if (kept) {
        *kept = survivors;
    }
    return out;
}
