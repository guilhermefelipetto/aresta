# aresta

Um editor de imagem escrito do zero em C++, que na verdade é uma desculpa pra
ter onde implementar algoritmo de segmentação e ver o resultado na hora.

O nome é o trocadilho óbvio: aresta de grafo e aresta de imagem são a mesma
palavra. A parte de editor existe porque segmentação interativa precisa de
alguém rabiscando semente na imagem, e rabiscar semente precisa de UI.

Está bem no começo — por enquanto isso aqui compila e imprime uma versão.

## Compilando

Precisa de um compilador com C++20, CMake e Ninja:

```
sudo apt install build-essential cmake ninja-build
```

Daí:

```
./build.sh
./aresta
```

Pra build otimizado, `./build.sh Release`. O script deixa um link simbólico
`aresta` na raiz apontando pro binário dentro de `build/bin`, que é só pra não
ter que digitar o caminho inteiro.

## Stack

C++20 no núcleo, SDL2 pra janela e input, Dear ImGui pra interface, OpenGL 3.3
pra desenhar, stb_image pra ler arquivo. CMake e Ninja no build.

## Estado

- [x] toolchain de pé
- [x] janela abrindo, com SDL2 e ImGui
- [x] PNG na tela, canvas com pan e zoom
- [x] o buffer de imagem e as operações por pixel
- [ ] pilha de operações não-destrutiva, com undo
- [ ] filtros de vizinhança
- [ ] pincel de semente e overlay de máscara
- [ ] segmentação
- [ ] salvar projeto, exportar

Vai demorar pra chegar na parte que interessa, mas a ordem é essa mesmo.

Desenvolvido no Ubuntu 24.04.
