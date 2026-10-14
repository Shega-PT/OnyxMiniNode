// =====================================================================
// onyx/limites_taxa.h — quantos ENVIAR por segundo
// ---------------------------------------------------------------------
// O relay em Python limita a 32 `ENVIAR` por segundo e por ligação.
// Aqui é igual, e a razão de ser igual é que a diferença faria um nó
// e o outro discordarem sobre o que é abuso.
// =====================================================================
#ifndef ONYX_LIMITES_TAXA_H
#define ONYX_LIMITES_TAXA_H

#include <cstdint>

#include "onyx/limites.h"

namespace onyx {

/// ## Porque é que o relógio é injectado
///
/// Por causa da janela. Uma janela deslizante é, por definição, uma
/// comparação com o tempo, e um teste que dependesse de `millis()`
/// mediria em segundos reais o que o código quer medir em milissegundos
/// de simulação. Com o relógio injectado, «33 `ENVIAR` numa janela dá
/// `RateLimit` no 33.º» é um caso de teste que acaba em microssegundos.
///
/// ## Porque é que é por ligação e não global
///
/// Um relay global tem uma consequência que ninguém quer num sistema
/// que se diz justo: uma pessoa a enviar mensagens sem parar acabaria
/// por gastar a quota de todas as outras.
///
/// ## Porque é que uma ligação que esgota pára em vez de fechar
///
/// Fechar a ligação é indistinguível de o emissor ter sido cortado, e
/// o cliente do OnyxChat trata um corte como «tenta mais tarde» — que
/// é a resposta certa para uma rede com Tor por baixo. Uma ligação que
/// pára devolve `RateLimit`, e o emissor sabe porquê.
///
/// ## Porque é que a janela conta para trás
///
/// A janela é de um segundo e deslizante: não é «32 no minuto», é «32
/// no último segundo». Deslizante significa que um cliente que
/// envia 32 depressa e 32 depressa-passado um segundo está dentro da
/// janela dos dois estouros — e está, porque o que interessa é a
/// taxa, não o minuto. Um cliente que envio 64 e depois fica quieto
/// um segundo volta a estar dentro, e é isso que uma janela
/// deslizante faz e um contador fixo não.
class JanelaTaxa {
public:
    JanelaTaxa() = default;

    /// Regista um envio. Devolve `false` quando a janela está cheia.
    bool registar(std::uint64_t agora_ms);

    /// Quantos envios a janela corrente tem usado.
    std::uint8_t usados() const noexcept { return usados_; }

    /// Esquece a janela.
    ///
    /// Chamar-se entre ligações, nunca durante uma. Uma sessão que
    /// recomeçasse a contar a meio mandaria o cliente receber
    /// `RateLimit` por envios que fez há meio segundo, e o cliente não
    /// tem como saber que a culpa é do nó.
    void repor() noexcept {
        usados_ = 0;
        instante_inicio_ = 0;
        aberta_ = false;
    }

private:
    static constexpr std::uint8_t LIMITE = MININODE_ENVIOS_POR_S;
    static constexpr std::uint64_t JANELA_MS = 1000ull;

    std::uint8_t usados_ = 0;
    std::uint64_t instante_inicio_ = 0;
    bool aberta_ = false;
};

}  // namespace onyx

#endif  // ONYX_LIMITES_TAXA_H