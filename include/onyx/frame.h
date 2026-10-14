// =====================================================================
// onyx/frame.h — o enquadramento do fio
// ---------------------------------------------------------------------
// Cada mensagem no fio é:
//
//     [comprimento: u32 little-endian][tipo: 1][corpo: comprimento-1]
//
// É o mesmo formato que o IPC local do OnyxChat e o mesmo que o relay
// em Python fala. Não há versão, não há negociação, não há
// compressão: um relay de 320 KB não tem espaço para renegociar nada,
// e um formato que se negoceia é um formato em que as duas pontas
// podem discordar.
//
// Este cabeçalho **não** é uma função de framing. É o tipo e as
// validações, e são as duas coisas mais importantes do ficheiro: um
// relay mal implementado não é um relay que não funciona, é um relay
// que deixa outro escrever na sua memória.
// =====================================================================
#ifndef ONYX_FRAME_H
#define ONYX_FRAME_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "onyx/limites.h"

namespace onyx {

/// ## Porque é que um comprimento de zero é um erro e não uma mensagem
///
/// `docs/protocolo.md` §Transporte diz que um comprimento `0` fecha a
/// ligação. A razão é que um pacote de comprimento zero **não
/// codifica nada**: nem tipo, nem corpo. Aceitá-lo seria aceitar um
/// pacote que não diz nada, e a primeira coisa que um cliente faz com
/// um pacote que não diz nada é ficar à espera do seguinte. Um
/// cliente à espera é um recurso consumido por outro.
inline constexpr bool comprimento_valido(std::uint32_t comprimento) {
    return comprimento >= 1 && comprimento <= MAX_COMPRIMENTO;
}

/// Uma mensagem lida, ou a preparar para escrever.
///
/// O corpo é um `vector` e não um ponteiro para um buffer de
/// recepção porque as duas coisas têm vidas diferentes: a mensagem
/// lida vive enquanto a sessão viver, e o buffer do lwIP pertence à
/// stack. Copiar é o que torna a frame auto-contida, e o custo são
/// alguns microssegundos por pacote.
struct Frame {
    std::uint8_t tipo = 0;
    std::vector<std::uint8_t> corpo;
};

/// O que `ler` pode ter a dizer quando não preencheu nada.
enum class ErroLeitura {
    /// A mensagem está completa e correcta.
    Completa,
    /// Ainda não chegou tudo. O cursor ficou onde tinha de ficar.
    Incompleta,
    /// Comprimento `0`, ou acima do limite. Fecha a ligação.
    ComprimentoInvalido,
};

/// ## Porque é que o limite é verificado *antes* de qualquer alocação
///
/// É a ordem que importa, não o teste. `MAX_COMPRIMENTO` é 4 097 e
/// um `uint32_t` chega a quatro milhares de milhões. Uma leitura que
/// obedecesse ao valor do outro lado alocaria até quatro gigabytes
/// numa placa que tem 320 KB — e a falha não é um erro, é o
/// watchdog a reiniciar o nó em ciclo, que é a forma mais cara de
/// alguém mandar numa placa que nem é sua.
///
/// Por isso a verificação acontece na função que **lê o cabeçalho**,
/// e o corpo só é copiado depois de o comprimento ter passado. O que
/// está entre as duas coisas não pode ser nada.
///
/// ## Porque é que `ler` não toca em sockets
///
/// Porque é a única parte do firmware que os testes de host exercitam
/// a sério, e a única que vale a pena exercitar a sério: é a
/// fronteira onde entra o que outro escolheu. Tudo o resto é lógica
/// que se lê.
///
/// A rede fica de fora de propósito. Um teste que também precisasse
/// de lwIP deixaria de correr sem placa, e essa é a propriedade que
/// torna os testes úteis em cada alteração.
bool escrever(std::vector<std::uint8_t>& saida, std::uint8_t tipo,
              const std::uint8_t* corpo, std::size_t tamanho_corpo);

/// Acrescenta uma mensagem **sem** limpar o que já lá está.
///
/// `escrever` limpa o vector do chamador antes de escrever, e isso é o
/// que faz a saída de uma sessão poder ser sobrescrita sem rubbish
/// acumulado. Mas um buffer com duas mensagens seguidas — que é o que
/// uma ligação que enviou um `FECHO` logo a seguir a um `CAIXA`
/// produz — tem de ser montado a partir de duas chamadas.
///
/// O nome é `acrescentar` e não `escrever_mais` porque é a palavra
/// que a operação faz, e não a posição que ocupa.
bool acrescentar(std::vector<std::uint8_t>& saida, std::uint8_t tipo,
                 const std::uint8_t* corpo, std::size_t tamanho_corpo);

/// Serializa `[tipo][corpo]` com o cabeçalho de comprimento.
///
/// O comprimento escrito é `1 + tamanho_corpo`, e inclui o byte do
/// tipo. Ver a nota em `limites.h` §MAX_CORPO sobre o que acontece se
/// alguém decidir que devia excluir.
inline bool escrever(std::vector<std::uint8_t>& saida, std::uint8_t tipo,
                     const std::vector<std::uint8_t>& corpo) {
    return escrever(saida, tipo, corpo.data(), corpo.size());
}

/// Constrói `CAIXA`: `frame_tipo(1) ‖ frame_corpo(N)`.
///
/// É `escrever` com um byte à frente, e tem uma função própria porque
/// o byte extra é o que separa um relay cego de um relay que entendeu
/// o que está a transportar. Ver `caixa.h` §O byte que ninguém lê.
bool escrever_caixa(std::vector<std::uint8_t>& saida, std::uint8_t frame_tipo,
                    const std::uint8_t* frame_corpo,
                    std::size_t tamanho_frame);

/// ## Porque é que a mensagem de erro é limitada a 96 bytes
///
/// O limite é um número escolhido a olho, e é curto de propósito. Um
/// relay que escrevesse um rastreio de pilha num `ERRO` transformaria
/// a banda de depuração num caminho que um atacante controla — o
/// conteúdo do `ERRO` é decidido por quem provocou o erro.
///
/// 96 bytes chegam para «caixa cheia» e para «demasiadas mensagens»,
/// que são os dois erros que um utilizador precisa de ler.
inline constexpr std::size_t TAM_MENSAGEM_ERRO = 96;

/// Constrói `ERRO`: `codigo(1) ‖ mensagem UTF-8`.
///
/// A mensagem é opcional; `nullptr` produz só o código.
bool escrever_erro(std::vector<std::uint8_t>& saida, std::uint8_t codigo,
                   const char* mensagem);

/// Constrói `OK`, que tem corpo vazio.
inline bool escrever_ok(std::vector<std::uint8_t>& saida) {
    return escrever(saida, MSG_OK, nullptr, 0);
}

/// Lê uma mensagem de um cursor sobre um buffer acumulado.
///
/// Devolve `Completa` e preencheu `saida` quando a mensagem inteira
/// chegou, e `Incompleta` quando ainda não chegou tudo — que é
/// diferente de erro. Um erro fecha a ligação; um cursor que ainda
/// espera é só um pacote a caminho.
///
/// ## O contrato do cursor
///
/// **Um `Incompleta` não consome nada.** O cursor volta ao sítio de
/// onde partiu. Quem chamou fica com o buffer intacto e tenta quando
/// chegar mais, e o buffer só cresce até ao tamanho de uma mensagem.
///
/// A alternativa — avançar para o fim do disponível — deita fora
/// exatamente os bytes que já chegaram, e o sintoma é uma mensagem
/// que nunca se completa, repetida para sempre, sem mensagem de erro
/// nenhuma. O teste `test_frame.cpp` §consumo parcial existe para
/// isso.
ErroLeitura ler(const std::vector<std::uint8_t>& buffer,
                 std::size_t& posicao, Frame& saida);

}  // namespace onyx

#endif  // ONYX_FRAME_H