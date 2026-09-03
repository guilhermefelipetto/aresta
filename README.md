# aresta

Uma bancada para processamento de imagem escrita em C++.

## Compilando

Precisa de um compilador com C++20, CMake, Ninja e SDL2:

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl-dev
```

Daí:

```bash
./build.sh
./aresta imagem.png
```

Pra build otimizado, `./build.sh Release`. O script deixa um link simbólico
`aresta` na raiz apontando pro binário dentro de `build/bin`, que é só pra não
ter que digitar o caminho inteiro.

Dá pra abrir arquivo por argumento, arrastando na janela, ou pelo menu.

## Núcleo

Cor é `float32` em espaço **linear**, RGBA intercalado, com stride pra recorte
sair sem cópia. A conversão de sRGB acontece só nas bordas: na leitura e na ida
pra tela.

`Map<T>` é o plano escalar de mesma geometria, um valor por pixel, sem canal.
É onde ficam gradiente, distância, rótulo, binário, e mais tarde os mapas de
custo e predecessor da IFT.

## Kernel

`Ferramentas > Kernel` abre a grade. Dá pra digitar coeficiente a coeficiente,
carregar um dos prontos, gerar por parâmetro (gaussiana, LoG, diferença de
gaussianas, gabor, disco, borrado de movimento) ou preencher por fórmula, onde
`x` e `y` contam do centro, `r` e `t` são as mesmas coordenadas em polar e
`a`, `b`, `c` ficam amarrados em sliders. `gauss(r, a)` reconstrói a gaussiana,
`r <= a` dá um disco.

A janela mostra a soma dos coeficientes, se a matriz é separável (a diferença
entre `w*h` e `w+h` multiplicações por pixel) e a **resposta em frequência** do
kernel, ao vivo. Gaussiana acende o centro e apaga a borda; laplaciano faz o
contrário. Aplicar não mexe na
imagem: acrescenta um estágio de convolução na cadeia.

O caminho de volta também existe: o botão `editar` na linha do estágio liga a
janela naquele estágio, e daí em diante mexer num coeficiente é mexer na
cadeia. Clicar em `editar` noutro estágio só troca o que a janela está
dirigindo. A janela da curva funciona igual.

## Curva

`Ferramentas > Curva` é a família de transformação de intensidade: negativo,
log, gama, alongamento linear, fatiamento, solarização. Todas saem da mesma
fórmula em `v`, com o gráfico da transformação desenhado por cima do histograma
da entrada, que é a figura clássica do Gonzalez virada ferramenta.

Isso não cabe num kernel: convolução é linear, então ela multiplica e soma mas
não eleva a potência nem corta faixa. Kernel 1x1 com valor 2 multiplica por 2,
e é só até onde ela vai.

## Histograma

`Ferramentas > Histograma` mede o estágio que estiver na tela, não a imagem
original. Em cor mostra R, G, B e luminância, com as faixas contadas em sRGB ou
no valor linear guardado. Escala log por padrão, porque um fundo liso vira um
pico que achata todo o resto contra o eixo.

Junto vêm média, mediana, desvio, entropia e quanto pesa a faixa mais cheia,
o nível que o Otsu escolheu marcado na curva, e botões que acrescentam estágio
na cadeia: equalizar pela acumulada, equalizar local (CLAHE), alongar contraste
entre dois percentis, e binarizar no Otsu.

A equalização global é monotônica, então não separa o que entrou junto. Numa
foto com fundo liso e comprimido isso é fatal: dá pra ter 80% dos pixels num
único valor de luminância, e nenhuma função de um argumento vai espalhar
aquilo. A local escapa disso dando mapeamentos diferentes em lugares
diferentes.

## Exportando

`Arquivo > Exportar como` (Ctrl+E) abre uma janela que pergunta qual estágio
salvar, não só o que está na tela, e se você quer **como aparece** (RGBA de 8
bits, com colormap, pra figura) ou os **valores crus** (o número que o estágio
carrega, sem colormap e sem cortar).

PNG, JPEG, BMP e TGA guardam o que aparece. Pros valores crus tem PFM (float32
exato), Netpbm (PGM de 16 bits), CSV com nove dígitos, e NPY, que o
`numpy.load` abre direto. A pasta padrão sai do XDG, então cai em Downloads
mesmo com o sistema em outro idioma.

## Stack

C++20 no núcleo, SDL2 pra janela e input, Dear ImGui pra interface, OpenGL 3.3
pra desenhar, stb_image pra ler arquivo. CMake e Ninja no build.

## Estado

- [x] toolchain de pé
- [x] janela abrindo, com SDL2 e ImGui
- [x] imagem na tela, canvas com pan e zoom
- [x] buffer float32 linear, `Map<T>`, e as operações por pixel
- [x] cadeia de operações tipada, com a saída de cada estágio inspecionável
- [x] convolução, com editor de kernel, geradores e fórmula
- [x] filtros de ordem, casamento de histograma, planos de bit
- [x] FFT 2D, espectro centralizado e filtros no domínio da frequência
- [x] transformação de intensidade por fórmula, com o gráfico da curva
- [x] aritmética entre estágios, fios da cadeia desenhados, e exibição fixável
- [x] vizinhança por raio, morfologia e componentes conexas, com filtro por área
- [x] histograma, com equalização global e local, alongamento e Otsu
- [x] mapa escalar e de rótulo na tela, com colormap
- [ ] pincel de semente e os algoritmos de grafo
- [ ] modo bench: rodar sobre dataset, cronometrar, medir
- [x] exportar estágio: PNG, JPEG, BMP, TGA, Netpbm, PFM, CSV, NPY
- [ ] salvar e carregar o projeto

Desenvolvido no Ubuntu 24.04.
