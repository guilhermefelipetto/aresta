# aresta

Uma bancada de processamento de imagem escrita do zero em C++, pra montar
algoritmo e ver o que ele faz na hora.

O nome é o trocadilho óbvio: aresta de grafo e aresta de imagem são a mesma
palavra.

Não é um GIMP. O GIMP já existe, é livre e é bom, e refazer aquilo seria
retrabalho sem motivo. A pergunta aqui é outra: montar um kernel na mão e ver
o efeito, trocar a vizinhança de 4 pra 8 e comparar, empilhar operações e
inspecionar a saída de cada estágio, cronometrar contra uma referência. Tem
função de edição porque sem elas não dá pra chegar até o experimento, mas o
produto é o experimento.

## Compilando

Precisa de um compilador com C++20, CMake, Ninja e SDL2:

```
sudo apt install build-essential cmake ninja-build libsdl2-dev libgl-dev
```

Daí:

```
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

`Map<T>` é o plano escalar de mesma geometria — um valor por pixel, sem canal.
É onde moram gradiente, distância, rótulo, binário, e mais tarde os mapas de
custo e predecessor da IFT. Metade das operações interessantes produz um desses
e não uma imagem colorida, então ele é cidadão de primeira classe, não um caso
particular de `Image`.

## Stack

C++20 no núcleo, SDL2 pra janela e input, Dear ImGui pra interface, OpenGL 3.3
pra desenhar, stb_image pra ler arquivo. CMake e Ninja no build.

## Estado

- [x] toolchain de pé
- [x] janela abrindo, com SDL2 e ImGui
- [x] imagem na tela, canvas com pan e zoom
- [x] buffer float32 linear, `Map<T>`, e as operações por pixel
- [ ] cadeia de operações tipada, com a saída de cada estágio inspecionável
- [ ] vizinhança configurável, convolução e um editor de kernel
- [ ] histograma, threshold e morfologia
- [ ] mapa escalar e de rótulo na tela, com colormap
- [ ] pincel de semente e os algoritmos de grafo
- [ ] modo bench: rodar sobre dataset, cronometrar, medir
- [ ] salvar projeto, exportar

Vai demorar pra chegar na parte que interessa, mas a ordem é essa mesmo.

Desenvolvido no Ubuntu 24.04.
