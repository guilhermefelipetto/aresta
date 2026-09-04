#pragma once

#include "image.h"
#include "map.h"

// O que o estágio mostra na tela. O número resume, o mapa diz onde.
enum class MetricMap { AbsError, SquaredError, Ssim };

const char* metric_map_name(MetricMap kind);

// Quanto uma imagem se afastou de outra. Tudo de uma vez, porque cada número
// esconde uma coisa que os outros mostram: o RMSE não diz se o erro está
// concentrado, o PSNR não diz nada sobre estrutura, e o SSIM não diz o tamanho
// do erro.
struct Metrics {
    double mae = 0.0;
    double mse = 0.0;
    double rmse = 0.0;
    double snr_db = 0.0;
    double psnr_db = 0.0;
    double ssim = 0.0;

    // O pico usado no PSNR e nas constantes do SSIM. Os dois só querem dizer
    // alguma coisa junto com ele, então ele anda com o resultado.
    float peak = 1.0f;
};

// `peak` é a amplitude que o dado poderia ter, não a que ele tem. Passar zero
// ou negativo faz cair no 1.0.
Metrics compare(MapView<float> reference, MapView<float> measured, float peak);

// Em cor, a conta roda canal a canal e sai a média. `on_srgb` mede sobre o
// valor com gama, que é onde a literatura de PSNR e SSIM vive; sobre o linear
// os números não batem com paper nenhum.
Metrics compare(ImageView reference, ImageView measured, float peak, bool on_srgb);

// Mapa do SSIM local, que é de onde sai a média. Vale mais que o número: ele
// mostra onde a imagem quebrou.
Map<float> ssim_map(MapView<float> reference, MapView<float> measured, float peak);
Map<float> ssim_map(ImageView reference, ImageView measured, float peak, bool on_srgb);

// Faixa que o dado ocupa, pra quem quiser o pico tirado da própria referência
// em vez do 1.0.
float peak_of(MapView<float> map);
float peak_of(ImageView image, bool on_srgb);

Map<float> metric_map(MapView<float> reference, MapView<float> measured, MetricMap kind,
                      float peak);
Map<float> metric_map(ImageView reference, ImageView measured, MetricMap kind, float peak,
                      bool on_srgb);
