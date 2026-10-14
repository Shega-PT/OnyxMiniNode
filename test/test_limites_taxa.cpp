// =====================================================================
// test_limites_taxa.cpp — a janela deslizante
// ---------------------------------------------------------------------
// Uma função e um relógio injectado. É pouco, e é o suficiente: o
// que se está a testar é uma comparação com o tempo, e uma comparação
// com o tempo só se testa com um relógio que o teste controla.
// =====================================================================
#include <unity.h>

#include <onyx/limites_taxa.h>

using onyx::JanelaTaxa;

namespace {
constexpr std::uint64_t SEGUNDO = 1000;
}  // namespace

/// A janela abre no primeiro envio.
void test_primeiro_envio_passa() {
    JanelaTaxa j;
    TEST_ASSERT_TRUE(j.registar(0));
    TEST_ASSERT_EQUAL_UINT8(1, j.usados());
}

/// Até ao limite, passa.
void test_ate_ao_limite_passa() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(j.registar(0), "recusou dentro do limite");
    }
    TEST_ASSERT_EQUAL_UINT8(MININODE_ENVIOS_POR_S, j.usados());
}

/// ## O 33.º é recusado
///
/// E a recusa é do **33.º**, não do 34.º: `usados_ >= LIMITE` com
/// `usados_` a contar envios já aceites dá exactamente isto. Um
/// `>` em vez de um `>=` daria um envio a mais, e o cliente do
/// OnyxChat — que conta por si próprio — começaria a ver recusas
/// antes de esgotar a cota.
void test_o_limite_mais_um_e_recusado() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(0));
    }
    TEST_ASSERT_FALSE(j.registar(0));
    // E continua recusado.
    TEST_ASSERT_FALSE(j.registar(0));
    TEST_ASSERT_EQUAL_UINT8(MININODE_ENVIOS_POR_S, j.usados());
}

/// Um segundo depois, a janela abre.
///
/// A janela é de um segundo, não de um minuto nem de um instante. É o
/// que a torna uma cota de **taxa** e não uma cota de volume.
void test_um_segundo_depois_a_janela_reabre() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(0));
    }
    TEST_ASSERT_FALSE(j.registar(SEGUNDO - 1));
    TEST_ASSERT_TRUE_MESSAGE(j.registar(SEGUNDO), "a janela não reabriu");
}

/// ## A janela desliza
///
/// O que distingue uma janela deslizante de um contador que reinicia:
/// dois estouros com menos de um segundo de intervalo **contam como
/// um só intervalo**. Num contador que reiniciasse a cada segundo de
/// relógio, 32 envios às 999 ms e mais 32 às 1 000 ms seriam 64
/// num segundo — que é exactamente o abuso que a cota existe para
/// impedir.
void test_dois_estouros_seguidos_partilham_a_janela() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(0));
    }
    // Novecentos e noventa e nove milissegundos depois, ainda não
    // passou um segundo: o primeiro estouro ainda conta.
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_FALSE_MESSAGE(j.registar(SEGUNDO - 1),
                                  "a janela não deslizou");
    }
}

/// Relógio parado é uma janela parada.
///
/// Não é um caso que acontece; é o que acontece quando dois envios
/// chegam no mesmo milissegundo, que é o que acontece com um cliente
/// que faz `lseek` e um socket que não escalona. E o que se tem de
/// garantir é que o limite não se estica por causa disso.
void test_relogio_parado_nao_estica_a_janela() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(500));
    }
    TEST_ASSERT_FALSE(j.registar(500));
}

/// ## Relógio para trás não dá mais cota
///
/// `millis()` no Arduino é um contador de 32 bits que dá a volta de 49,7
/// em 49,7 dias, e um NTP a corrigir pode recuar o relógio. Se um
/// recuo reabriasse a janela, um cliente que recuasse o relógio
/// teria cota infinita.
///
/// A resposta é a conservadora: manter a janela aberta e não contar.
void test_relogio_atras_nao_da_mais_cota() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(10000));
    }
    TEST_ASSERT_FALSE(j.registar(9000));
    TEST_ASSERT_FALSE(j.registar(0));
}

/// `repor` limpa a janela.
void test_repor() {
    JanelaTaxa j;
    for (std::size_t i = 0; i < MININODE_ENVIOS_POR_S; ++i) {
        TEST_ASSERT_TRUE(j.registar(0));
    }
    j.repor();
    TEST_ASSERT_EQUAL_UINT8(0, j.usados());
    TEST_ASSERT_TRUE(j.registar(0));
}

/// A entrada de `test_main.cpp`.
void testes_limites_taxa() {
    RUN_TEST(test_primeiro_envio_passa);
    RUN_TEST(test_ate_ao_limite_passa);
    RUN_TEST(test_o_limite_mais_um_e_recusado);
    RUN_TEST(test_um_segundo_depois_a_janela_reabre);
    RUN_TEST(test_dois_estouros_seguidos_partilham_a_janela);
    RUN_TEST(test_relogio_parado_nao_estica_a_janela);
    RUN_TEST(test_relogio_atras_nao_da_mais_cota);
    RUN_TEST(test_repor);
}
