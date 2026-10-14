// =====================================================================
// onyx/limites.h — os números do protocolo, num sítio só
// ---------------------------------------------------------------------
// Tudo o que é constante vem de `platformio.ini`, em `build_flags`. O
// motivo não é convenção: os mesmos quatro números estão na
// implementação de referência do OnyxChat, em `server/relay_server.py`,
// e estão escritos em `docs/protocolo.md` §Regras de retenção. Três
// sítios a dizer a mesma coisa é um sítio a mais.
//
// Aqui só vivem os **derivados** — o que sai dos números. E cada
// derivado tem um `static_assert` a dizer que a suposição que ele
// carrega continua a ser verdadeira, porque um destes cálculos está
// errado em silêncio: compila, produz um número plausível, e o nó
// aceita mais do que a memória permite.
// =====================================================================
#ifndef ONYX_LIMITES_H
#define ONYX_LIMITES_H

#include <cstddef>
#include <cstdint>

namespace onyx {

/// ## Os quatro números que vêm do exterior
///
/// Vêm de `platformio.ini` e são os mesmos que o OnyxChat usa. Se
/// alguém compilar este ficheiro fora do PlatformIO, os valores de
/// baixo são os de referência e servem para os testes de host — o que
/// é deliberado: `pio test -e native` não tem `build_flags`.

#ifndef MININODE_TTL_S
#define MININODE_TTL_S 300
#endif
#ifndef MININODE_MAX_PENDENTES
#define MININODE_MAX_PENDENTES 12
#endif
#ifndef MININODE_MAX_CORPO
#define MININODE_MAX_CORPO 4096
#endif
#ifndef MININODE_ENVIOS_POR_S
#define MININODE_ENVIOS_POR_S 32
#endif

// --- Dimensionais -----------------------------------------------------

/// Bytes de um hash SHA-256, e portanto de uma mailbox.
inline constexpr std::size_t TAM_MAILBOX = 32;

/// Bytes do corpo de `SUBSCREVER`: só a mailbox.
inline constexpr std::size_t TAM_SUBSCREVER = TAM_MAILBOX;

/// Bytes do corpo de `ENVIAR`: mailbox, tipo do frame, e o frame.
inline constexpr std::size_t TAM_ENVIAR_CABECALHO = TAM_MAILBOX + 1;

/// Bytes do corpo de `CAIXA`: tipo do frame, e o frame.
inline constexpr std::size_t TAM_CAIXA_CABECALHO = 1;

/// O maior corpo que este nó aceita, sem contar o byte do tipo.
///
/// O enquadramento escreve o comprimento **incluindo** o tipo, e por
/// isso o corpo máximo é um a menos do que o comprimento máximo. A
// subtileza vale um comentário porque a alternativa — declarar o
// comprimento e subtractar no sítio certo — dá um `PayloadGrandeDemais`
// a um frame de exactamente o tamanho máximo, que é a pior forma de
// um limite: funciona até alguém acertar no tamanho exacto.
inline constexpr std::size_t MAX_CORPO = MININODE_MAX_CORPO;

/// O maior `comprimento` que pode aparecer no cabeçalho: tipo mais o
/// corpo máximo.
inline constexpr std::size_t MAX_COMPRIMENTO = 1 + MAX_CORPO;

/// Heap reservado para os frames pendentes de **uma** mailbox.
///
/// `12 × 4096 = 49 152` bytes. A conta inteira, com o que resta dos
/// 320 KB da placa, está em `docs/protocolo.md` §A aritmética dos
/// 320 KB.
inline constexpr std::size_t HEAP_POR_MAILBOX =
    MININODE_MAX_PENDENTES * MININODE_MAX_CORPO;

/// Quantas mailboxes podem estar activas ao mesmo tempo.
///
/// Este é o número que decide se o pior caso cabe na SRAM, e é
/// derivado dos outros dois de propósito: quem quiser aumentar a
/// capacidade por mailbox não deve poder fazê-lo sem que este valor
/// baixe sozinho e avise. Um `MAX_MAILBOXES` escrito à mão ao lado
/// dos outros seria um número que ninguém actualiza.
inline constexpr std::size_t MAX_MAILBOXES =
    (HEAP_POR_MAILBOX == 0) ? 1 : (160u * 1024u) / HEAP_POR_MAILBOX;

/// Tipos de frame P2P do OnyxChat.
///
/// O relay **não** interpreta nenhum deles. Estão aqui porque `CAIXA`
/// e `ENVIAR` transportam o byte, e porque um nó que valida o byte
/// contra esta lista apanha um cliente que fala uma versão diferente
/// em vez de o retransmitir até alguém se aperceber.
inline constexpr std::uint8_t FRAME_CHAT = 0x01;
inline constexpr std::uint8_t FRAME_PEDIDO_AMIZADE = 0x10;
inline constexpr std::uint8_t FRAME_ACEITE_AMIZADE = 0x11;
inline constexpr std::uint8_t FRAME_RECUSA_AMIZADE = 0x12;

inline constexpr bool frame_p2p_conhecido(std::uint8_t tipo) {
    return tipo == FRAME_CHAT || (tipo >= FRAME_PEDIDO_AMIZADE &&
                                   tipo <= FRAME_RECUSA_AMIZADE);
}

// --- As seis mensagens do protocolo -----------------------------------

inline constexpr std::uint8_t MSG_SUBSCREVER = 0x01;
inline constexpr std::uint8_t MSG_ENVIAR = 0x02;
inline constexpr std::uint8_t MSG_FECHO = 0x03;
inline constexpr std::uint8_t MSG_ERRO = 0x81;
inline constexpr std::uint8_t MSG_OK = 0x83;
inline constexpr std::uint8_t MSG_CAIXA = 0x84;

// --- Os seis códigos de erro ------------------------------------------

inline constexpr std::uint8_t ERRO_PAYLOAD_MALFORMADO = 0x01;
inline constexpr std::uint8_t ERRO_MAILBOX_INVALIDA = 0x02;
inline constexpr std::uint8_t ERRO_PAYLOAD_GRANDE_DEMAIS = 0x03;
inline constexpr std::uint8_t ERRO_SEM_ESPACO = 0x04;
inline constexpr std::uint8_t ERRO_RATE_LIMIT = 0x05;
inline constexpr std::uint8_t ERRO_COMANDO_DESCONHECIDO = 0x06;

/// Porto TCP de escuta por omissão.
///
/// Não é o 8788 que o `relay_server.py` usa por omissão, e a
/// diferença é deliberada: os dois não estão em cena ao mesmo tempo
/// na maior parte dos casos, e um MiniNode numa rede de casa não
/// tem porquê de estar na mesma porta que um servidor de
/// desenvolvimento.
inline constexpr std::uint16_t PORTA = 8788;

/// Segmentos de socket recebidos de cada vez.
///
/// lwIP tem um número limitado de buffers de recepção, e partilhá-los
/// entre todas as ligações é o que faz um nó cair quando aparecem
/// muitos clientes ao mesmo tempo. Este valor é deliberadamente
/// pequeno — o ponto de um relay é ser um ponto de encontro, não um
/// servidor de streaming.
inline constexpr int TAM_RECVER = 2048;

/// Conexões simultâneas.
///
/// Ver `docs/protocolo.md` §A aritmética dos 320 KB: cada ligação
/// custa uma estrutura de sessão e uma janela de taxa, e o número
/// que importa é o que cabe depois das mailboxes.
inline constexpr std::size_t MAX_SESSOES = 6;

// --- As suposições que têm de continuar verdadeiras -------------------

static_assert(MAX_COMPRIMENTO > 1,
              "um pacote de comprimento 1 é o `FECHO`; abaixo disso não há "
              "protocolo");
static_assert(MAX_CORPO >= 16,
              "um corpo de menos de 16 bytes nunca é um envelope do "
              "OnyxChat — o mínimo é 117 — e acceptar menos faria de um "
              "erro do cliente um erro da máquina");
static_assert(MININODE_MAX_PENDENTES >= 2,
              "com uma única pendência a caixa não distinguiria «cheio» "
              "de «quase cheio», que é o que evita descartar em silêncio");
static_assert(MAX_MAILBOXES >= 2,
              "com uma única mailbox possível o nó não é um ponto de "
              "encontro: é uma caixa de uma pessoa");
static_assert(HEAP_POR_MAILBOX <= 64u * 1024u,
              "uma mailbox acima de 64 KB deixa de caber várias vezes nos "
              "320 KB da placa. Se esta asserção rebentou, a conta de "
              "`docs/protocolo.md` §A aritmética dos 320 KB está errada e "
              "precisa de ser refeita antes de o firmware voar");

}  // namespace onyx

#endif  // ONYX_LIMITES_H
