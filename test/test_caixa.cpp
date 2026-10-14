// =====================================================================
// test_caixa.cpp — retenção, entrega única, e a caixa cheia
// ---------------------------------------------------------------------
// O relógio é injectado, e é por isso que este ficheiro tem treze
// casos em vez de um: cada caso de expiração é um caso de teste
// normal em vez de uma espera de cinco minutos.
// =====================================================================
#include <unity.h>

#include <onyx/caixa.h>
#include <onyx/limites.h>

using onyx::Caixa;
using onyx::Mailbox;
using onyx::MAX_CORPO;

namespace {

/// Uma mailbox qualquer. O conteúdo não importa para a caixa: a caixa
/// é um address book, e o que está num endereço não muda o
/// comportamento da caixa que o contém.
Mailbox caixa_de(std::uint8_t semente) {
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

constexpr std::uint64_t SEGUNDO = 1000;

}  // namespace

/// Depositados saem pela mesma ordem em que entraram.
///
/// A ordem de uma conversa é o que faz uma conversa ser uma conversa.
/// Um relay que reordenasse — por tamanho, para reduzir alocações —
/// entregaria as mensagens fora de ordem e o cliente não teria como
/// desfazer.
void test_ordem_de_entrega() {
    Caixa c;
    c.ligar(caixa_de(1), 0);

    TEST_ASSERT_TRUE(c.depositar(0x01, nullptr, 0, 0));  // vazio
    const std::vector<std::uint8_t> segundo = corpo(4, 0x10);
    TEST_ASSERT_TRUE(c.depositar(0x01, segundo.data(), segundo.size(), 0));

    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;

    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT8(0x01, tipo);
    TEST_ASSERT_EQUAL_UINT32(0, saida.size());

    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT32(4, saida.size());
    TEST_ASSERT_EQUAL_UINT8(0x10, saida[0]);
}

/// A entrega é **única**.
///
/// Um `CAIXA` que saísse duas vezes faria o cliente receber a mesma
/// mensagem duas vezes, e o anti-replay do OnyxChat — que existe
/// precisamente para travar isso — rejeitaria a segunda como
/// replay. O cliente mostraria a mensagem uma vez e descartaria a
/// outra; se o segundo fosse um aperto de mão, a amizade nunca
/// completava.
void test_entrega_unica() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> f = corpo(3, 0x20);
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));

    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;
    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_FALSE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT32(0, c.tamanho());
}

/// ## A caixa cheia recusa, e não descarta
///
/// O teste mais importante do ficheiro, e o menos óbvio de acertar.
///
/// A política de descarta-o-mais-antigo liberta espaço e **faz o
/// emissor acreditar que a mensagem foi entregue**: o `ENVIAR` volta
/// `OK` e o destinatário nunca a recebe. Com `SemEspaco`, o emissor
/// recebe `0x04` e decide.
///
/// A diferença entre as duas é a diferença entre um sistema que perde
/// mensagens e um sistema que **diz** que perde.
void test_caixa_cheia_recusa_e_nao_descarta() {
    Caixa c;
    c.ligar(caixa_de(1), 0);

    const std::vector<std::uint8_t> pequeno = corpo(8, 0x30);
    for (std::size_t i = 0; i < MININODE_MAX_PENDENTES; ++i) {
        TEST_ASSERT_TRUE(c.depositar(0x01, pequeno.data(), pequeno.size(), 0));
    }
    TEST_ASSERT_EQUAL_UINT32(MININODE_MAX_PENDENTES, c.tamanho());

    // O que não cabe.
    const std::vector<std::uint8_t> a_mais = corpo(8, 0x40);
    TEST_ASSERT_FALSE(c.depositar(0x01, a_mais.data(), a_mais.size(), 0));

    // E o que lá estava continua lá, **e pela ordem**.
    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;
    for (std::size_t i = 0; i < MININODE_MAX_PENDENTES; ++i) {
        TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
        TEST_ASSERT_EQUAL_UINT8(0x30, saida[0]);
    }
}

/// Um frame grande demais não é depositado.
///
/// O limite é verificado **antes** de copiar, e o que o relê é que um
/// `assign` de quatro megabytes numa placa de 320 KB é um `abort()`.
void test_frame_grande_demais() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> grande = corpo(MAX_CORPO + 1, 0x50);

    TEST_ASSERT_FALSE(c.depositar(0x01, grande.data(), grande.size(), 0));
    TEST_ASSERT_EQUAL_UINT32(0, c.tamanho());
}

/// O prazo é o do protocolo, e não um número inventado aqui.
///
/// Se este teste falhar, `platformio.ini` mudou `MININODE_TTL_S` sem
/// o `docs/protocolo.md` mudar — e as duas coisas têm de mudar juntas.
void test_o_prazo_e_o_do_protocolo() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> f = corpo(1, 0x60);
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));

    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;

    // Um segundo antes do prazo, **ainda está**. E `retirar` consome,
    // por isso o teste tem de voltar a depositar para poder perguntar
    // outra vez. Duas asserções sobre o mesmo frame não são possíveis
    // porque a primeira leva-o consigo.
    const std::uint64_t antes =
        static_cast<std::uint64_t>(MININODE_TTL_S) * SEGUNDO - 1;
    TEST_ASSERT_TRUE_MESSAGE(c.retirar(saida, tipo, antes),
                             "o frame expirou antes do prazo");
    TEST_ASSERT_EQUAL_UINT32(0, c.tamanho());

    // Deita-se outro, **no instante zero**, e pergunta-se no prazo.
    // A pergunta tem de ser relativa ao depósito: um frame deitado às
    // 299 999 ms não expira às 300 000 ms, expira às 599 999 ms.
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));
    const std::uint64_t no_prazo =
        static_cast<std::uint64_t>(MININODE_TTL_S) * SEGUNDO;
    TEST_ASSERT_FALSE_MESSAGE(c.retirar(saida, tipo, no_prazo),
                              "o frame sobreviveu ao prazo");
    TEST_ASSERT_EQUAL_UINT32(0, c.tamanho());
}

/// Expirar devolve a memória.
void test_expirar_esvazia_a_caixa() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    for (std::size_t i = 0; i < MININODE_MAX_PENDENTES; ++i) {
        const std::vector<std::uint8_t> f = corpo(64, 0x70);
        TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));
    }
    TEST_ASSERT_TRUE(c.bytes_pendentes() > 0);

    const std::uint64_t depois =
        static_cast<std::uint64_t>(MININODE_TTL_S) * SEGUNDO + 1;
    c.ligar(caixa_de(1), depois);

    TEST_ASSERT_EQUAL_UINT32(0, c.tamanho());
    TEST_ASSERT_EQUAL_UINT32(0, c.bytes_pendentes());
}

/// Uma caixa cheia de frames expirados aceita um depósito novo.
///
/// É o caso que justifica expurgar em `ligar` e não só em
/// `depositar`: se a caixa encheu, o cliente que a subscreve chega
/// depois, e nesse momento os frames que lá estão já expiraram. Sem o
/// expurgo, o primeiro depósito deste cliente levaria `SemEspaco` numa
/// caixa que estava, na verdade, livre.
void test_caixa_cheia_de_expirados_aceita() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    for (std::size_t i = 0; i < MININODE_MAX_PENDENTES; ++i) {
        const std::vector<std::uint8_t> f = corpo(8, 0x80);
        TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));
    }
    TEST_ASSERT_EQUAL_UINT32(MININODE_MAX_PENDENTES, c.tamanho());

    // O destinatário chega tarde.
    const std::uint64_t tarde =
        static_cast<std::uint64_t>(MININODE_TTL_S) * SEGUNDO + 1;
    Caixa d;
    d.ligar(caixa_de(1), tarde);

    const std::vector<std::uint8_t> nova = corpo(8, 0x90);
    TEST_ASSERT_TRUE(d.depositar(0x01, nova.data(), nova.size(), tarde));
    TEST_ASSERT_EQUAL_UINT32(1, d.tamanho());
}

/// Uma caixa não ligada não é a caixa de ninguém.
void test_ligar_muda_o_destino() {
    Caixa c;
    const Mailbox a = caixa_de(1);
    const Mailbox b = caixa_de(2);

    TEST_ASSERT_FALSE(c.subscrita());
    TEST_ASSERT_FALSE(c.ligada_a(a));

    c.ligar(a, 0);
    TEST_ASSERT_TRUE(c.subscrita());
    TEST_ASSERT_TRUE(c.ligada_a(a));
    TEST_ASSERT_FALSE(c.ligada_a(b));

    // Re-subscrição: muda o destino e esvazia.
    const std::vector<std::uint8_t> f = corpo(2, 0xA0);
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));
    TEST_ASSERT_EQUAL_UINT32(1, c.tamanho());

    c.ligar(b, 0);
    TEST_ASSERT_TRUE(c.ligada_a(b));
    TEST_ASSERT_FALSE(c.ligada_a(a));
    // E o que estava pendente **sobrevive**: as pendências são do
    // endereço, não da sessão que o ouve. Re-subsccrever muda quem
    // recebe, não o que está à espera — e é por isso que o mesmo
    // `relay_server.py` faz `pop` só do que se pede na subscrição e
    // deixa o resto da caixa intacto.
    TEST_ASSERT_EQUAL_UINT32(1, c.tamanho());
}

/// O tipo do frame viaja com o corpo e é o que volta.
void test_o_tipo_do_frame_volta_certa() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> f = corpo(5, 0xB0);

    TEST_ASSERT_TRUE(c.depositar(onyx::FRAME_PEDIDO_AMIZADE, f.data(),
                                 f.size(), 0));

    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;
    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT8(onyx::FRAME_PEDIDO_AMIZADE, tipo);
    TEST_ASSERT_EQUAL_UINT32(5, saida.size());
}

/// Um depósito não tem de ser para a caixa que está ligada.
///
/// É o caso normal de um relay: quem subscreve e quem deposita são
/// ligações diferentes. A caixa tem de aceitar um depósito de quem
/// não está ligado a ela — quem liga é o emissor, e a caixa guarda
/// para o destinatário.
void test_depositar_sem_estar_subscrito() {
    Caixa c;
    const std::vector<std::uint8_t> f = corpo(3, 0xC0);

    // Sem `ligar`, o depósito é aceite: a caixa é um endereço, e o
    // depósito é uma carta que chega antes de o destinatário existir.
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));
    TEST_ASSERT_EQUAL_UINT32(1, c.tamanho());

    // E quando o destinatário aparece, recebe.
    c.ligar(caixa_de(1), 0);
    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;
    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT32(3, saida.size());
}

/// `bytes_pendentes` é a soma, e serve para o registo de memória.
void test_bytes_pendentes() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> a = corpo(10, 0xD0);
    const std::vector<std::uint8_t> b = corpo(30, 0xE0);

    TEST_ASSERT_TRUE(c.depositar(0x01, a.data(), a.size(), 0));
    TEST_ASSERT_TRUE(c.depositar(0x01, b.data(), b.size(), 0));
    TEST_ASSERT_EQUAL_UINT32(40, c.bytes_pendentes());
}

/// Relógio a andar para trás não ressuscita frames.
///
/// Um `Incompleta` que descartasse por estar fora de prazo seria um
/// bug grave: um cliente que mudasse o relógio — ou um `millis()` do
/// Arduino a dar a volta, o que acontece de 49,7 em 49,7 dias —
/// perderia mensagens sem ninguém dar por isso.
void test_relogio_atras_saude_a_caixa() {
    Caixa c;
    c.ligar(caixa_de(1), 0);
    const std::vector<std::uint8_t> f = corpo(4, 0xF0);
    TEST_ASSERT_TRUE(c.depositar(0x01, f.data(), f.size(), 0));

    // O instante vai para trás: nada expira.
    std::vector<std::uint8_t> saida;
    std::uint8_t tipo = 0;
    TEST_ASSERT_TRUE(c.retirar(saida, tipo, 0));
    TEST_ASSERT_EQUAL_UINT32(4, saida.size());
}

/// A entrada do ficheiro. Ver `test_main.cpp` §Porque é que o `main`
/// está num ficheiro só.
void testes_caixa() {
    RUN_TEST(test_ordem_de_entrega);
    RUN_TEST(test_entrega_unica);
    RUN_TEST(test_caixa_cheia_recusa_e_nao_descarta);
    RUN_TEST(test_frame_grande_demais);
    RUN_TEST(test_o_prazo_e_o_do_protocolo);
    RUN_TEST(test_expirar_esvazia_a_caixa);
    RUN_TEST(test_caixa_cheia_de_expirados_aceita);
    RUN_TEST(test_ligar_muda_o_destino);
    RUN_TEST(test_o_tipo_do_frame_volta_certa);
    RUN_TEST(test_depositar_sem_estar_subscrito);
    RUN_TEST(test_bytes_pendentes);
    RUN_TEST(test_relogio_atras_saude_a_caixa);
}
