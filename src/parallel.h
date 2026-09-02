#pragma once

#include <algorithm>
#include <thread>
#include <vector>

// Divide [begin, end) em faixas, uma por núcleo. Sem pool de propósito:
// convolução roda em dezenas de milissegundos e criar thread custa
// microssegundos. Se algum dia isso rodar por tile pequeno, aí vale o pool.
template <typename F>
void parallel_for(int begin, int end, F body) {
    const int count = end - begin;
    if (count <= 0) {
        return;
    }

    unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        hardware = 1;
    }
    const int workers = std::min<int>(static_cast<int>(hardware), std::max(1, count / 32));
    if (workers <= 1) {
        body(begin, end);
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers));
    const int chunk = (count + workers - 1) / workers;
    for (int i = 0; i < workers; ++i) {
        const int lo = begin + i * chunk;
        const int hi = std::min(end, lo + chunk);
        if (lo >= hi) {
            break;
        }
        threads.emplace_back([&body, lo, hi] { body(lo, hi); });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}
