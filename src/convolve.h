#pragma once

#include "image.h"
#include "kernel.h"
#include "map.h"

enum class Border { Zero, Clamp, Mirror, Wrap };

const char* border_name(Border border);

// Correlação, que é o que o filter2D do OpenCV chama de filtro. Com flip ligado
// o kernel é espelhado nos dois eixos e vira convolução no sentido estrito.
Image convolve(ImageView src, const Kernel& kernel, Border border, bool flip);
Map<float> convolve(MapView<float> src, const Kernel& kernel, Border border, bool flip);
