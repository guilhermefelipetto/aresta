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

## Stack

C++20 no núcleo, SDL2 pra janela e input, Dear ImGui pra interface, OpenGL 3.3
pra desenhar, stb_image pra ler arquivo. CMake e Ninja no build.

## Estado

- [x] toolchain de pé
- [x] janela abrindo, com SDL2 e ImGui
- [x] imagem na tela, canvas com pan e zoom
- [x] buffer float32 linear, `Map<T>`, e as operações por pixel
- [x] cadeia de operações tipada, com a saída de cada estágio inspecionável
- [ ] vizinhança configurável, convolução e um editor de kernel
- [ ] histograma, threshold e morfologia
- [x] mapa escalar e de rótulo na tela, com colormap
- [ ] pincel de semente e os algoritmos de grafo
- [ ] modo bench: rodar sobre dataset, cronometrar, medir
- [ ] salvar projeto, exportar

Desenvolvido no Ubuntu 24.04.
