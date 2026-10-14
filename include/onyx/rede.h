// =====================================================================
// onyx/rede.h — o ponto de acesso e os sockets
// ---------------------------------------------------------------------
// Esta é a única parte do firmware que fala com o lwIP, e a única que
// não é portável para o computador. É por isso que
// `docs/testes.md` §Host diz que a camada de host não a cobre: não é
// uma escolha, é uma consequência de o lwIP não existir fora da
// placa.
// =====================================================================
#ifndef ONYX_REDE_H
#define ONYX_REDE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "onyx/limites.h"
#include "onyx/sessao.h"

namespace onyx {

/// O depósito de caixas — a implementação concreta de
/// `Sessao::Deposito`.
///
/// Vive aqui e não em `sessao.h` porque precisa de `MAX_MAILBOXES` e
/// de nada mais; se vivesse no cabeçalho, um teste que instanciasse
/// uma sessão teria de instanciar também um depósito.
class Deposito : public Sessao::Deposito {
public:
    Caixa* procurar(const Mailbox& destino) override;
    Caixa* obter_ou_criar(const Mailbox& destino,
                         std::uint64_t agora_ms) override;
    std::size_t quantas() const override { return entradas_.size(); }

private:
    struct Entrada {
        Mailbox chave{};
        Caixa caixa;
    };

    Caixa* procurar_no_vector(const Mailbox& destino);

    /// Todas as entradas. Procura linear.
    ///
    /// Linear é a escolha certa e não a preguiçosa: `MAX_MAILBOXES` é
    /// maior do que um e menor do que a Memória que uma tabela de
    /// dispersão de chaves de 32 bytes ocuparia — duzentos bytes de
    /// tabela para poupar três comparações de 32 bytes.
    std::vector<Entrada> entradas_;
};

/// O ponto de acesso e as ligações que ele tem.
///
/// ## Porque é que o estado é um array e não um vector
///
/// Um `vector<Sessao>` de seis elementos, com uma sessão de 4 KB cada,
/// é um bloco de 24 KB que o alocador decide onde pôr. Um array é um
/// bloco cujo tamanho é escolhido em tempo de compilação e que o
/// linker decide onde pôr — e o bloco é sempre o mesmo, o que faz o
/// pico de memória do laço ser conhecido à partida em vez de
/// dependente do que o alocador decidiu num dia. Num firmware de 320
/// KB, essa diferença é entre um orçamento que se pode escrever num
/// documento e um que não.
class Rede {
public:
    Rede() = default;

    Rede(const Rede&) = delete;
    Rede& operator=(const Rede&) = delete;

    /// Arranca o ponto de acesso com palavra-passe.
    ///
    /// Nunca arranca **sem** palavra-passe. Um relay aberto numa rede
    /// com clientes não autenticados é um ponto de recolha de
    /// envelopes de toda a gente, e `docs/ameacas.md` §Mitigações
    /// trata a palavra-passe como mitigação e não como opção.
    ///
    /// Devolve `false` se a placa não conseguir levantar o ponto de
    /// acesso. É fatal: um relay sem rede não é um relay, e fingir
    /// que está a funcionar seria pior do que dizer que não está.
    bool arrancar_ap(const char* ssid, const char* palavra_passe);

    /// Liga-se a uma rede existente, em vez de a criar.
    ///
    /// É o modo `estação`, e existe para quem quer o MiniNode na rede
    /// de casa em vez de criar uma rede nova. Um ponto de acesso
    /// próprio é mais simples de configurar e mais difícil de
    /// integrar com o resto da casa, e quem tem router sabe qual
    /// prefere.
    ///
    /// Não espera pela ligação. Esperar bloquearia o arranque por
    /// dezenas de segundos quando o router não estivesse lá, e quem
    /// está a ver o ecrã de arranque pensaria que a placa estava
    /// avariada. `ligado()` diz se já está.
    bool ligar_a_rede(const char* ssid, const char* palavra_passe);

    /// Já tem endereço?
    bool ligado() const noexcept { return endereco_[0] != '\0'; }

    /// O endereço que a placa tem.
    ///
    /// Devolve um ponteiro para uma cadeia interna que a próxima
    /// chamada substitui. Não há alocação, e quem chama precisa de
    /// saber disso: um `String` do Arduino aqui seria uma alocação
    /// por chamada, e esta função é chamada em cada passo do laço.
    const char* endereco() const noexcept { return endereco_; }

    /// Porta em que está a escutar.
    std::uint16_t porta() const noexcept { return porta_; }

    /// Quantas ligações estão abertas.
    std::size_t ligacoes() const noexcept;

    /// Corre um passo do laço: espera, aceita, lê, decide, escreve.
    ///
    /// Devolve `false` quando o laço deve parar. Na prática só
    /// acontece se o socket de escuta desaparecer, e nesse caso
    /// `main.cpp` tenta re-ligá-lo em vez de ficar a interrogar um
    /// socket morto.
    ///
    /// O passo não bloqueia mais do que `TEMPO_PASSO_MS`. Uma função
    /// que bloqueasse até haver trabalho seria mais simples e não
    /// funcionaria: com seis ligações, uma delas a meio de um pacote
    /// impediria as outras cinco de avançarem.
    bool passo(std::uint64_t agora_ms);

    /// Uma sessão, para o diagnóstico. `nullptr` se o índice não
    /// estiver ocupado.
    Sessao* sessao(std::size_t indice) noexcept {
        return ocupado_[indice] ? &sessoes_[indice] : nullptr;
    }

    /// O depósito, para o diagnóstico.
    Deposito& deposito() noexcept { return deposito_; }

private:
    void fechar(std::size_t indice) noexcept;
    bool aceitar();
    bool ler_de(std::size_t indice);
    bool escrever_para(std::size_t indice);

    /// Recebe o que está à espera no socket de escuta.
    void acumular(std::size_t indice, const std::uint8_t* dados,
                  std::size_t tamanho);
    void esvaziar_entrada(std::size_t indice);

    Sessao sessoes_[MAX_SESSOES]{};
    bool ocupado_[MAX_SESSOES]{};
    int sockets_[MAX_SESSOES]{};
    std::vector<std::uint8_t> entrada_[MAX_SESSOES];
    std::size_t posicao_[MAX_SESSOES]{};
    std::uint8_t scratch_[TAM_RECVER]{};

    Deposito deposito_;
    char endereco_[16] = {0};
    std::uint16_t porta_ = PORTA;
    int escuta_ = -1;
    std::uint32_t proximo_id_ = 1;
};

}  // namespace onyx

#endif  // ONYX_REDE_H
