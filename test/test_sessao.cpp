// =====================================================================
// test_sessao.cpp — a máquina de estados de uma ligação
// ---------------------------------------------------------------------
// Estes testes usam um depósito de mentira. Não é uma coisa que existe
// a mais no firmware: é um `Sessao::Deposito` com três `Caixa` em
// memória, e é o que permite testar a máquina de estados sem socket.
//
// A alternativa seria um socket de loopback, que exigiria lwIP e
// portanto uma placa — e que tornaria os testes mais lentos sem
// testar mais nada. A máquina de estados é pura: recebe uma `Frame`,
// devolve bytes. O socket é o que a alimenta.
// =====================================================================
#include <unity.h>

#include <vector>

#include <onyx/frame.h>
#include <onyx/limites.h>
#include <onyx/sessao.h>

using onyx::Caixa;
using onyx::Frame;
using onyx::Mailbox;
using onyx::MAX_COMPRIMENTO;
using onyx::Sessao;

namespace {

/// Um depósito com um número fixo de caixas, todas em memória.
///
/// `MAX_MAILBOXES` num `Rede` real é limitado pela SRAM; aqui é
/// limitado por uma constante do teste, e o teste que interessa é o
/// que esgota esse limite.
class DepositoFalso : public Sessao::Deposito {
public:
    static constexpr std::size_t MAX = 8;

    Caixa* procurar(const Mailbox& destino) override {
        for (Caixa& c : caixas_) {
            if (c.subscrita() && c.destino() == destino) {
                return &c;
            }
        }
        return nullptr;
    }

    Caixa* obter_ou_criar(const Mailbox& destino,
                         std::uint64_t agora_ms) override {
        for (Caixa& c : caixas_) {
            if (c.subscrita() && c.destino() == destino) {
                return &c;
            }
        }
        if (quantas_ >= MAX) {
            return nullptr;
        }
        caixas_[quantas_].ligar(destino, agora_ms);
        ++quantas_;
        return &caixas_[quantas_ - 1];
    }

    std::size_t quantas() const override { return quantas_; }

private:
    Caixa caixas_[MAX];
    std::size_t quantas_ = 0;
};

Mailbox mailbox_de(std::uint8_t semente) {
    Mailbox m{};
    for (std::size_t i = 0; i < m.size(); ++i) {
        m[i] = static_cast<std::uint8_t>(semente + i);
    }
    return m;
}

std::vector<std::uint8_t> corpo(std::size_t n, std::uint8_t semente) {
    std::vector<std::uint8_t> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = static_cast<std::uint8_t>(semente + i);
    }
    return c;
}

/// Lê todas as mensagens de um buffer de saída.
std::vector<Frame> ler_tudo(const std::vector<std::uint8_t>& buffer) {
    std::vector<Frame> saida;
    std::size_t pos = 0;
    while (pos < buffer.size()) {
        Frame f;
        if (onyx::ler(buffer, pos, f) != onyx::ErroLeitura::Completa) {
            break;
        }
        saida.push_back(std::move(f));
    }
    return saida;
}

/// A primeira mensagem da fila.
Frame primeira(const std::vector<std::uint8_t>& saida) {
    std::vector<Frame> todas = ler_tudo(saida);
    TEST_ASSERT_TRUE_MESSAGE(todas.size() > 0, "a sessão não respondeu");
    return todas.front();
}

/// Monta um `SUBSCREVER` para uma mailbox.
Frame subscrever(const Mailbox& m) {
    Frame f;
    f.tipo = onyx::MSG_SUBSCREVER;
    f.corpo.assign(m.begin(), m.end());
    return f;
}

/// Monta um `ENVIAR` para uma mailbox, com um frame de `n` bytes.
Frame enviar(const Mailbox& m, std::uint8_t frame_tipo, std::size_t n) {
    Frame f;
    f.tipo = onyx::MSG_ENVIAR;
    f.corpo.assign(m.begin(), m.end());
    f.corpo.push_back(frame_tipo);
    const std::vector<std::uint8_t> c = corpo(n, 0x40);
    f.corpo.insert(f.corpo.end(), c.begin(), c.end());
    return f;
}

}  // namespace

// =====================================================================
// SUBSCREVER
// =====================================================================

/// Uma subscrição bem formada dá `OK` e liga a caixa.
void test_subscrever_da_ok() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    TEST_ASSERT_TRUE(s.processar(subscrever(mailbox_de(1)), 0));
    TEST_ASSERT_TRUE(s.subscrita());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_OK, primeira(s.saida()).tipo);
}

/// ## A mailbox tem de ter exactamente 32 bytes
///
/// Nem mais nem menos. Uma mailbox de 33 bytes é uma caixa diferente,
/// e um cliente que mandasse uma a pensar que mandou a outra receberia
/// um `OK` e nunca uma mensagem — que é a falha mais silenciosa que
/// existe num protocolo de correio.
///
/// E a ligação **fica aberta**: o cliente pode corrigir-se.
void test_mailbox_do_tamanho_errado_nao_fecha_a_ligacao() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    Frame f;
    f.tipo = onyx::MSG_SUBSCREVER;
    f.corpo = corpo(33, 0x01);

    TEST_ASSERT_TRUE(s.processar(f, 0));
    TEST_ASSERT_FALSE(s.subscrita());
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_MAILBOX_INVALIDA, r.corpo[0]);
}

/// Re-subscrever move a sessão para a outra caixa.
void test_resubscrever_move_a_sessao() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    const Mailbox a = mailbox_de(1);
    const Mailbox b = mailbox_de(9);
    TEST_ASSERT_TRUE(s.processar(subscrever(a), 0));
    TEST_ASSERT_TRUE(s.caixa() != nullptr && s.caixa()->ligada_a(a));

    s.consumir_saida();
    TEST_ASSERT_TRUE(s.processar(subscrever(b), 0));
    TEST_ASSERT_TRUE(s.caixa() != nullptr && s.caixa()->ligada_a(b));
}

// =====================================================================
// ENVIAR
// =====================================================================

/// ## Um depósito para a própria caixa dá `CAIXA` e depois `OK`
///
/// A ordem é esta e não outra: a entrega vem **antes** do acuse. Um
/// cliente que deposita na própria mailbox recebe o `CAIXA` — que é o
/// que o `CAIXA` é: a entrega do frame que acabou de meter na caixa
/// — e só depois o `OK` que confirma o depósito.
///
/// Um `OK` primeiro seria um acuse de algo que ainda não foi entregue,
/// e um cliente que tratasse o `OK` como «está entregue» ficaria com a
/// mensagem na fila de entrada à espera de um `CAIXA` que já passou.
void test_enviar_para_a_propria_caixa() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    const Mailbox a = mailbox_de(1);
    TEST_ASSERT_TRUE(s.processar(subscrever(a), 0));
    s.consumir_saida();

    TEST_ASSERT_TRUE(s.processar(enviar(a, onyx::FRAME_CHAT, 5), 0));

    const std::vector<Frame> r = ler_tudo(s.saida());
    TEST_ASSERT_EQUAL_UINT32(2, r.size());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_CAIXA, r[0].tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::FRAME_CHAT, r[0].corpo[0]);
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_OK, r[1].tipo);
}

/// ## Um `CAIXA` depositado por outra sessão chega
///
/// Este é o caminho normal de um relay — dois clientes, um a falar e
/// outro a ouvir — e é o que `Sessao::puxar` resolve. Sem ele, o
/// segundo cliente não recebia nada enquanto não mandasse uma
/// mensagem sua.
void test_caixa_de_outra_sessao_e_entregue() {
    DepositoFalso d;

    Sessao emissor;
    emissor.ligar_deposito(d);
    Sessao ouvinte;
    ouvinte.ligar_deposito(d);

    const Mailbox a = mailbox_de(1);
    TEST_ASSERT_TRUE(ouvinte.processar(subscrever(a), 0));
    ouvinte.consumir_saida();

    TEST_ASSERT_TRUE(emissor.processar(enviar(a, onyx::FRAME_CHAT, 7), 0));
    TEST_ASSERT_EQUAL_UINT32(0, ouvinte.bytes_por_escrever());

    ouvinte.puxar(1);
    TEST_ASSERT_TRUE(ouvinte.bytes_por_escrever() > 0);

    const Frame r = primeira(ouvinte.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_CAIXA, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::FRAME_CHAT, r.corpo[0]);
    TEST_ASSERT_EQUAL_UINT32(7, r.corpo.size() - 1);
}

/// ## `ENVIAR` para uma caixa sem subscritor dá erro, e a ligação fica
///
/// Criar a caixa à receção de um depósito seria um mapa que cresce sem
/// limite, alimentado por quem quer que seja, para endereços que nunca
/// são subscritos. Não se cria.
void test_enviar_sem_subscritor_da_erro() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    TEST_ASSERT_TRUE(s.processar(enviar(mailbox_de(3), onyx::FRAME_CHAT, 4),
                                 0));
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_MAILBOX_INVALIDA, r.corpo[0]);
    TEST_ASSERT_FALSE(s.a_fechar());
}

/// `ENVIAR` com o corpo curto demais dá `PayloadMalformado`.
void test_enviar_com_corpo_curto() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    Frame f;
    f.tipo = onyx::MSG_ENVIAR;
    f.corpo = corpo(32, 0x01);  // mailbox, mas sem tipo nem frame

    TEST_ASSERT_TRUE(s.processar(f, 0));
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_PAYLOAD_MALFORMADO, r.corpo[0]);
}

/// ## A taxa é por ligação
///
/// Uma sessão que esgota a janela não impede outra de enviar. Se a
/// cota fosse global, uma pessoa a falar muito num grupo de discussão
/// consumia a quota de toda a gente.
void test_a_taxa_e_por_ligacao() {
    DepositoFalso d;

    Sessao a;
    a.ligar_deposito(d);
    Sessao b;
    b.ligar_deposito(d);

    const Mailbox m = mailbox_de(1);
    a.processar(subscrever(m), 0);
    b.processar(subscrever(m), 0);

    // A primeira fica sem quota; a segunda ainda a tem.
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        a.processar(enviar(m, onyx::FRAME_CHAT, 2), 0);
    }
    a.consumir_saida();
    a.processar(enviar(m, onyx::FRAME_CHAT, 2), 0);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_RATE_LIMIT,
                            primeira(a.saida()).corpo[0]);

    // A segunda sessão deposita na mesma mailbox e não esgota a cota:
    // dá `CAIXA` e `OK`, como qualquer depósito para uma caixa a que
    // está ligada.
    b.consumir_saida();
    b.processar(enviar(m, onyx::FRAME_CHAT, 2), 0);
    const std::vector<Frame> r = ler_tudo(b.saida());
    TEST_ASSERT_TRUE(r.size() >= 1);
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_CAIXA, r.front().tipo);
}

/// ## O depósito vem antes da entrega a si próprio
///
/// Se a entrega viesse primeiro, o cliente receberia um `CAIXA` de um
/// depósito que a caixa não aceitou — uma mensagem que ele recebeu e
/// que o destinatário nunca vai receber. É a pior falha possível num
/// sistema de mensagens, e nenhum teste de integridade a apanha
/// porque os dois lados foram correctos.
void test_caixa_cheia_nao_entrega_um_frame_que_nao_entrou() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    const Mailbox m = mailbox_de(1);
    s.processar(subscrever(m), 0);
    s.consumir_saida();

    // Enche a caixa pela outra sessão.
    Sessao outro;
    outro.ligar_deposito(d);
    for (std::size_t i = 0; i < MININODE_MAX_PENDENTES; ++i) {
        outro.processar(enviar(m, onyx::FRAME_CHAT, 2), 0);
    }
    outro.consumir_saida();

    // Esta agora deposita: não cabe.
    s.processar(enviar(m, onyx::FRAME_CHAT, 2), 0);
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_SEM_ESPACO, r.corpo[0]);
}

// =====================================================================
// FECHO e desconhecidos
// =====================================================================

/// `FECHO` dá `OK` e marca para fechar.
///
/// A resposta vem **antes** do fecho. Fechar sem responder dá ao
/// cliente um corte de rede em vez de um acuse de recibo, e a
/// diferença é a que faz o cliente tentar de novo.
void test_fecho_da_ok_e_marca() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    Frame f;
    f.tipo = onyx::MSG_FECHO;
    TEST_ASSERT_FALSE(s.processar(f, 0));
    TEST_ASSERT_TRUE(s.a_fechar());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_OK, primeira(s.saida()).tipo);
}

/// ## Um tipo desconhecido não fecha a ligação
///
/// Fechar por aqui seria punir um cliente de uma versão futura por ter
/// carregado numa mensagem nova. Responde-se com
/// `ComandoDesconhecido` e a ligação continua.
void test_tipo_desconhecido_nao_fecha() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    Frame f;
    f.tipo = 0x7F;
    f.corpo = corpo(4, 0x01);

    TEST_ASSERT_TRUE(s.processar(f, 0));
    TEST_ASSERT_FALSE(s.a_fechar());
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_COMANDO_DESCONHECIDO, r.corpo[0]);
}

/// Sem depósito, tudo dá erro e nada rebenta.
///
/// Uma sessão sem depósito é um bug de construção, mas rebentar por
/// isso seria pior do que responder com um erro: o sintoma de um
/// `nullptr` desreferenciado é um reinício por watchdog, e o sintoma
/// de um `ERRO` é um cliente que sabe que algo correu mal.
void test_sem_deposito_nao_rebenta() {
    Sessao s;
    TEST_ASSERT_TRUE(s.processar(subscrever(mailbox_de(1)), 0));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, primeira(s.saida()).tipo);
    TEST_ASSERT_FALSE(s.a_fechar());
}

/// A sessão responde a `puxar` mesmo sem ter subscrito, e não rebenta.
void test_puxar_sem_subscricao_nao_rebenta() {
    Sessao s;
    s.puxar(0);
    s.puxar(1000);
    TEST_ASSERT_EQUAL_UINT32(0, s.bytes_por_escrever());
}

/// Entrada de `ENVIAR` maior do que `MAX_COMPRIMENTO` dá
/// `PayloadGrandeDemais`.
void test_frame_acima_do_limite() {
    DepositoFalso d;
    Sessao s;
    s.ligar_deposito(d);

    const Mailbox m = mailbox_de(1);
    s.processar(subscrever(m), 0);
    s.consumir_saida();

    s.processar(enviar(m, onyx::FRAME_CHAT, MAX_COMPRIMENTO), 0);
    const Frame r = primeira(s.saida());
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, r.tipo);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_PAYLOAD_GRANDE_DEMAIS, r.corpo[0]);
}

/// A entrada de `test_main.cpp`.
void testes_sessao() {
    RUN_TEST(test_subscrever_da_ok);
    RUN_TEST(test_mailbox_do_tamanho_errado_nao_fecha_a_ligacao);
    RUN_TEST(test_resubscrever_move_a_sessao);
    RUN_TEST(test_enviar_para_a_propria_caixa);
    RUN_TEST(test_caixa_de_outra_sessao_e_entregue);
    RUN_TEST(test_enviar_sem_subscritor_da_erro);
    RUN_TEST(test_enviar_com_corpo_curto);
    RUN_TEST(test_a_taxa_e_por_ligacao);
    RUN_TEST(test_caixa_cheia_nao_entrega_um_frame_que_nao_entrou);
    RUN_TEST(test_fecho_da_ok_e_marca);
    RUN_TEST(test_tipo_desconhecido_nao_fecha);
    RUN_TEST(test_sem_deposito_nao_rebenta);
    RUN_TEST(test_puxar_sem_subscricao_nao_rebenta);
    RUN_TEST(test_frame_acima_do_limite);
}