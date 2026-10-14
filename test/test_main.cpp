// =====================================================================
// test_main.cpp — o ponto de entrada único dos testes de host
// ---------------------------------------------------------------------
// Este ficheiro existe por uma razão do PlatformIO, e vale a pena
// escrevê-la porque a razão não é óbvia.
//
// ## Porque é que o `main` está num ficheiro só
//
// O PlatformIO trata um **directório** de `test/` como uma suite, e
// compila todos os `.cpp` desse directório para um executável. Com os
// ficheiros todos em `test/` a sério, cada um com o seu `main`, o
// linkador dá «definição múltipla de `main`» — e a mensagem não diz
// nada sobre a causa.
//
// A alternativa, uma subpasta por suite, resolvia. Mas aí cada pasta
// repete o `#include <unity.h>`, o `setUp` e o `tearDown`, e quatro
// cópias de três linhas são quatro sítios a divergir. Este ficheiro é
// a alternativa: um `main`, e cada ficheiro de testes expõe uma
// função que corre os seus.
//
// ## Porque é que `setUp` e `tearDown` estão vazios
///
/// Nenhum teste precisa de estado partilhado, e um `setUp` vazio é a
/// coisa mais honesta que se pode escrever quando não há nada a
// preparar. Escrever `static Caixa c;` e um `c = Caixa{}` aqui seria
// estado global que os testes de uma caixa tocariam de todos os
// outros, e o primeiro teste a correr seria o que definisse se os
// outros passam.
//
// Cada teste constrói a sua caixa e sabe o estado em que a deixa.
// =====================================================================
#include <unity.h>

void testes_frame();
void testes_caixa();
void testes_sessao();
void testes_limites_taxa();
void testes_mailbox();

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();

    testes_frame();
    testes_caixa();
    testes_sessao();
    testes_limites_taxa();
    testes_mailbox();

    return UNITY_END();
}