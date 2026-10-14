// =====================================================================
// onyx/caixa.h — as caixas, e o que nelas fica
// ---------------------------------------------------------------------
// Este é o estado do relay, e é o único estado que o firmware tem.
//
// A regra de retenção numa frase: um buffer temporário de transporte,
// não arquivo de mensagens. O que está numa caixa desaparece ao fim
// de `MININODE_TTL_S` segundos, e não há caminho no código que faça
// outra coisa.
// =====================================================================
#ifndef ONYX_CAIXA_H
#define ONYX_CAIXA_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "onyx/limites.h"
#include "onyx/mailbox.h"

namespace onyx {

/// ## O byte que ninguém lê
///
/// O corpo de `ENVIAR` e de `CAIXA` começa por um byte que é o tipo
/// do frame P2P — `0x01` para uma mensagem, `0x10` a `0x12` para o
/// handshake de amizade.
///
/// O relay **não** o interpreta. Não valida, não descola, não sabe o
/// que é uma assinatura. E mesmo assim ele entra na caixa, porque
/// quem tem de o receber é o cliente.
///
/// É a razão de o `CAIXA` levar o frame inteiro e não só o envelope:
/// um relay que conhecesse o envelope teria de conhecer o envelope.
struct Pendente {
    std::uint8_t frame_tipo = 0;
    std::vector<std::uint8_t> corpo;

    /// Instante de expiração, em milissegundos, de um relógio
    /// injectado.
    ///
    /// O relógio é injectado porque um teste que dependesse de
    /// `millis()` só se provava a correr durante 300 segundos. Com um
    /// relógio que o teste controla, «o frame expirou» é um caso de
    /// teste normal.
    std::uint64_t expira_ms = 0;
};

/// Uma caixa: os frames à espera de alguém os ir buscar.
///
/// Não é um mapa de mailboxes para pendentes. É uma caixa por
/// destinatário, e cada sessão subscreve **uma**. A diferença é de
/// quem resolve: com um mapa, entregar é procurar; com uma caixa,
/// entregar é percorrer uma lista de doze. Doze é o mesmo número nos
/// dois casos, e um relay que crescesse precisaria do primeiro — mas
/// um relay de 320 KB não cresce.
class Caixa {
public:
    Caixa() = default;

    /// Liga a caixa a um destino e expurga o que já expirou.
    ///
    /// Devolve os frames **na ordem em que foram depositados**, que é
    /// a ordem em que a caixa estava. Um relay que reordenasse
    /// mensagens para reduzir alocações transformaria uma conversa
    /// em conversa fora de ordem, e o cliente não tem como desfazer.
    void ligar(const Mailbox& destino, std::uint64_t agora_ms);

    /// Já está ligada a este endereço?
    bool ligada_a(const Mailbox& destino) const noexcept {
        return ligada_ && destino_ == destino;
    }

    /// Houve subscrição?
    bool subscrita() const noexcept { return ligada_; }

    /// Para que endereço está ligada?
    const Mailbox& destino() const noexcept { return destino_; }

    /// Deposita um frame. Devolve `false` se a caixa está cheia.
    ///
    /// Devolve `false` sem descartar nada quando a caixa está cheia, e
    /// a política é o inverso do que parece correcto: descartar o
    /// mais antigo liberta espaço e **faz o emissor acreditar que a
    /// mensagem foi entregue**. Numa caixa cheia, o emissor recebe
    /// `0x04` e decide. Perder em silêncio é a única forma de o
    /// cliente não conseguir saber que há um problema.
    bool depositar(std::uint8_t frame_tipo, const std::uint8_t* corpo,
                   std::size_t tamanho, std::uint64_t agora_ms);

    /// Quantos frames estão pendentes.
    std::size_t tamanho() const noexcept { return itens_.size(); }

    /// Quantos bytes estão pendentes, para o registo de memória.
    std::size_t bytes_pendentes() const noexcept;

    /// Tira e devolve o mais antigo que não expirou.
    ///
    /// Devolve `false` quando não há nada — o que inclui o caso em
    /// que havia mas expirou. A diferença é intencional: para quem
    /// chama, um frame que expirou e um frame que nunca existiu são o
    /// mesmo, e tratar o primeiro como erro seria anunciar uma falha
    /// de rede que não houve.
    bool retirar(std::vector<std::uint8_t>& saida, std::uint8_t& tipo,
                 std::uint64_t agora_ms);

private:
    /// Apaga tudo o que já expirou.
    ///
    /// Percorre da frente para trás e não é um `remove_if`: os frames
    /// expiram pela ordem em que entraram, porque foi assim que
    /// receberam o prazo. A lista fica ordenada por prazo e o que
    /// expira primeiro é sempre o primeiro, o que faz isto ser O(1)
    /// numa caixa cheia e O(n) numa vazia — que é o caso comum.
    void expurgar(std::uint64_t agora_ms) noexcept;

    Mailbox destino_{};
    std::vector<Pendente> itens_;
    bool ligada_ = false;
};

/// O prazo de validade, em milissegundos.
inline constexpr std::uint64_t TTL_MS =
    static_cast<std::uint64_t>(MININODE_TTL_S) * 1000ull;

/// O prazo que cada depósito recebe, calculado a partir de `agora`.
inline constexpr std::uint64_t expira(std::uint64_t agora_ms) noexcept {
    return agora_ms + TTL_MS;
}

}  // namespace onyx

#endif  // ONYX_CAIXA_H
