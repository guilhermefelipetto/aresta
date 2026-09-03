#pragma once

#include "image.h"
#include "kernel.h"
#include "map.h"

enum class Border { Zero, Clamp, Mirror, Wrap };

const char* border_name(Border border);

// Kernel grande custa w*h multiplicações por pixel no caminho direto e não
// custa nada a mais pela frequência, que só depende do tamanho da imagem.
enum class ConvPath { Auto, Spatial, Frequency };

const char* conv_path_name(ConvPath path);

// Correlação, que é o que o filter2D do OpenCV chama de filtro. Com flip ligado
// o kernel é espelhado nos dois eixos e vira convolução no sentido estrito.
//
// `used_fft`, quando dado, diz por qual caminho passou de fato.
Image convolve(ImageView src, const Kernel& kernel, Border border, bool flip,
               ConvPath path = ConvPath::Auto, bool* used_fft = nullptr);
Map<float> convolve(MapView<float> src, const Kernel& kernel, Border border, bool flip,
                    ConvPath path = ConvPath::Auto, bool* used_fft = nullptr);
