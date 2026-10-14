// =====================================================================
// onyx/sessao.h — a máquina de estados de uma ligação
// ---------------------------------------------------------------------
// Uma sessão é uma ligação TCP e tudo o que se sabe sobre ela. Não há
// mais estado no firmware: as sessões são o estado, e o resto — quem
// está ligado a que caixa, que mailbox tem pendentes — vive noutro
// sítio.
//
// A máquina é pequena de propósito e cabe numa tela. O protocolo tem
// seis mensagens e um relay não tem grafos: quem desenhar um diagrama
// com mais do que um círculo neste ficheiro está a desenhar mais
// do que o protocolo tem.
// =====================================================================
#ifndef ONYX_SESSAO_H
#define ONYX_SESSAO_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "onyx/caixa.h"
#include "onyx/frame.h"
#include "onyx/limites_taxa.h"
#include "onyx/mailbox.h"

namespace onyx {

/// O que uma sessão quer que o laço de rede faça a seguir.
///
/// O relay não escreve para o socket de dentro de uma função que
/// «decide». Decide, e devolve. A distinção é a que permite testar a
/// máquina de estados sem um socket — que é o que `test_sessao.cpp`
/// faz, e é a razão de esse ficheiro não tocar em lwIP.
class Sessao {
public:
    Sessao() = default;

    /// Processa uma mensagem recebida.
    ///
    /// Devolve `false` quando a ligação deve fechar. Isso acontece em
    /// três casos, e só em três: o cliente pediu para fechar, mandou
    /// um comprimento inválido, ou tentou subscrever mais do que as
    /// ligações activas permitem. Tudo o resto — uma `SUBSCREVER` sem
    /// assinatura, um `ENVIAR` malformado, uma taxa estourada —
    /// responde com `ERRO` e a ligação continua, porque um cliente que
    /// mandou uma coisa errada tem de poder mandar a seguinte.
    bool processar(const Frame& recebida, std::uint64_t agora_ms);

    /// Bytes prontos a escrever para o socket, e quantos são.
    ///
    /// Devolve um ponteiro para a fila interna, não uma cópia. A
    /// cópia seria alocar para cada `CAIXA` entregue, e a fila
    /// interna é uma coisa que já existe.
    const std::vector<std::uint8_t>& saida() const noexcept {
        return saida_;
    }

    /// Esvazia a fila de saída, depois de ter sido escrita no socket.
    void consumir_saida() noexcept { saida_.clear(); }

    /// Quantos bytes estão por escrever.
    std::size_t bytes_por_escrever() const noexcept { return saida_.size(); }

    /// Houve subscrição?
    bool subscrita() const noexcept {
        return caixa_ != nullptr && caixa_->subscrita();
    }

    /// Já passou por `SUBSCREVER`?
    ///
    /// Separado de `caixa_.subscrita()` por uma razão que só aparece
    /// nos testes: `Caixa` sabe se está ligada a alguém, e a sessão
    /// sabe se o cliente alguma vez disse quem. São coisas diferentes
    /// porque uma `Caixa` que se subscreveu a si mesma é um bug, e
    /// um teste que não os distinga passa à frente dele.
    bool alguma_vez_subscrita() const noexcept { return subscrita_; }

    /// A caixa a que esta sessão está ligada, ou `nullptr`.
    ///
    /// É uma **referência**, não uma cópia, e a razão é o que está
    /// escrito em `sessao.cpp` §subscrever: uma cópia esgota-se no
    /// momento da subscrição e nunca mais recebe nada.
    Caixa* caixa() noexcept { return caixa_; }
    const Caixa* caixa() const noexcept { return caixa_; }

    /// A janela de taxa.
    const JanelaTaxa& taxa() const noexcept { return taxa_; }

    /// Mete a sessão em modo de fecho, se ainda não estiver.
    void pedir_fecho() noexcept { fechar_ = true; }

    /// O cliente pediu para fechar, ou a sessão deve fechar?
    bool a_fechar() const noexcept { return fechar_; }

    /// Entrega o que apareceu na caixa desta sessão.
    ///
    /// Chama `rede.cpp` em cada passo, para todas as sessões
    /// subscritas. Sem isto, um `CAIXA` depositado por **outra**
    /// sessão ficava na caixa à espera de alguém — e ninguém ia
    /// buscá-lo, porque a sessão que o depositou só entrega o que
    /// está na **sua** caixa.
    ///
    /// É o caso mais comum de todos num relay: dois clientes ligados
    /// ao mesmo MiniNode, um a falar e outro a ouvir. Se esta função
    /// não existisse, o segundo não receberia nada enquanto não
    /// mandasse uma mensagem sua.
    void puxar(std::uint64_t agora_ms);

    /// Identificador para o log. Só o número da ligação.
    std::uint32_t id() const noexcept { return id_; }
    void fixar_id(std::uint32_t novo) noexcept { id_ = novo; }

    /// Liga a sessão a um depósito de caixas.
    ///
    /// O depósito é externo à sessão para que a janela de taxa e a
    /// memória das caixas sejam de uma coisa só: cada sessão tem a
    /// **sua** caixa (a que subscreveu) e o depósito tem as de todos os
    /// destinatários. Um relay com uma caixa só por sessão não é um
    /// relay: é uma caixa de correio com uma pessoa.
    class Deposito {
    public:
        virtual ~Deposito() = default;

        /// Procura a caixa deste destino. Devolve `nullptr` se não
        /// houver.
        virtual Caixa* procurar(const Mailbox& destino) = 0;

        /// Procura, e cria se não houver.
        virtual Caixa* obter_ou_criar(const Mailbox& destino,
                                     std::uint64_t agora_ms) = 0;

        /// Quantas caixas existem.
        virtual std::size_t quantas() const = 0;
    };

    /// Liga a sessão a um depósito.
    void ligar_deposito(Deposito& deposito) noexcept {
        deposito_ = &deposito;
    }

    /// Já tem depósito?
    bool tem_deposito() const noexcept { return deposito_ != nullptr; }

private:
    /// Processa `SUBSCREVER`.
    ///
    /// O corpo tem de ter exactamente 32 bytes. Nem mais nem menos:
    /// uma mailbox de 33 bytes é uma caixa diferente, e um cliente que
    /// mandasse uma a pensar que mandou a outra receberia um `OK` e
    /// nunca uma mensagem. Por isso o corpo tem de ser **exacto**.
    bool subscrever(const Frame& recebida, std::uint64_t agora_ms);

    /// Processa `ENVIAR`.
    ///
    /// É a única mensagem que toca em duas caixas: a do destinatário,
    /// onde o frame vai parar, e a da sessão, que é de onde ele sai.
    /// A ordem é depósito-primeiro: se o depósito falhar com
    /// `SemEspaco`, o emissor recebe o erro e o frame **não** é
    /// entregue a ninguém. Ao contrário, retirar primeiro e depositar
    /// depois daria ao cliente uma mensagem que ele recebeu e que o
    /// destinatário nunca vai receber — que é a pior falha possível
    /// num sistema de mensagens, e uma que nenhum teste de
    /// integridade apanha porque ambos os lados foram correctos.
    bool enviar(const Frame& recebida, std::uint64_t agora_ms);

    /// Devolve as pendentes desta caixa, uma por uma.
    void entregar(std::uint64_t agora_ms);

    /// Escreve um `ERRO` na fila de saída.
    void responder_erro(std::uint8_t codigo, const char* mensagem);

    /// Escreve um `OK` na fila de saída.
    void responder_ok();

    Caixa* caixa_ = nullptr;
    JanelaTaxa taxa_;
    std::vector<std::uint8_t> saida_;
    Deposito* deposito_ = nullptr;
    std::uint32_t id_ = 0;
    bool subscrita_ = false;
    bool fechar_ = false;
};

}  // namespace onyx

#endif  // ONYX_SESSAO_H
