// =====================================================================
// test_mailbox.cpp — a derivação da mailbox
// ---------------------------------------------------------------------
// ## O que este ficheiro apanha e o que não
//
// Este é o único ficheiro de testes que **não** fecha. A razão está
// escrita em `mailbox.h` §Porque é que esta é a única função que o
// host não cobre: `derivar_mailbox` precisa de mbedTLS, e o mbedTLS
// não está no caminho de _include_ de um compilador de estação de
// trabalho.
//
// O que fica coberto aqui é a parte que um programador é capaz de
// estragar sem dar por isso: o valor dos treze bytes do domínio e a
// ordem da concatenação. A parte de cima é a função de hash, que é do
// fabricante e que não vamos reescrever para lhe chamar de
// optimização.
//
// ## O vector de resposta conhecida
//
// O teste abaixo **não** apanha um SHA-256 trocado por outro, e é
// deliberado: o SHA-256 é o que o OnyxChat usa, e o que interessa é
// que os três concordem. Um teste de interoperação com o
// `relay_server.py` em Python é o que fecha essa parte, e está em
// `docs/testes.md` §Interoperabilidade.
// =====================================================================
#include <unity.h>

#include <onyx/limites.h>
#include <onyx/mailbox.h>

using onyx::Mailbox;
using onyx::TAM_DOMINIO;
using onyx::TAM_ENTRADA;
using onyx::TAM_MAILBOX;

namespace {

/// Uma chave pública de mentira. O conteúdo não importa: o que se
/// testa é a estrutura da derivação, e uma chave real seria uma
// chave a passear num ficheiro de testes.
std::uint8_t (&chave_de_mentira())[TAM_MAILBOX] {
    static std::uint8_t chave[TAM_MAILBOX] = {0};
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        chave[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    return chave;
}

}  // namespace

/// ## O domínio são treze bytes, e são estes
///
/// `ONYX/RELAY/v1` — treze bytes: `ONYX`, barra, `RELAY`, barra,
/// `v1`. A contagem é feita pelo compilador, com um `static_assert`,
/// e não por uma asserção escrita à mão. Foi o `static_assert` que
/// apanhou este número estar errado quando o ficheiro foi escrito:
/// estava em 14 e a verdade é 13.
///
/// Um `\0` a mais, um `/` trocado por `_`, um `V` maiúsculo: cada um
/// produz um hash perfeitamente válido e completamente diferente, e o
/// sintoma é um sistema em que o depósito responde `OK` e a mensagem
/// nunca chega a ninguém. Não há mensagem de erro que apanhe isso.
void test_o_dominio_sao_treze_bytes() {
    TEST_ASSERT_EQUAL_UINT8(13, TAM_DOMINIO);
    TEST_ASSERT_EQUAL_UINT8(13, TAM_ENTRADA - TAM_MAILBOX);

    static_assert(TAM_DOMINIO == 13,
                  "o dominio mudou de tamanho: o cliente vai derivar "
                  "outra mailbox e nao ha nada no fio que diga porquê");
    static_assert(TAM_ENTRADA == 45,
                  "a entrada deixou de ser dominio + chave de 32 bytes");
}

/// Byte a byte, o domínio é `ONYX/RELAY/v1`.
void test_o_dominio_byte_a_byte() {
    static constexpr std::uint8_t esperado[] = {
        'O', 'N', 'Y', 'X', '/', 'R', 'E', 'L', 'A', 'Y', '/', 'v', '1'};
    static_assert(sizeof esperado == TAM_DOMINIO,
                  "o literal nao tem o mesmo tamanho do dominio");

    for (std::size_t i = 0; i < TAM_DOMINIO; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            esperado[i], onyx::DOMINIO_RELAY[i],
            "o dominio nao bate com ONYX/RELAY/v1");
    }
}

/// ## A entrada é domínio ‖ chave, e nesta ordem
///
/// A ordem não é uma escolha estética. `H(pub ‖ dom)` e `H(dom ‖
/// pub)` são dois hashes igualmente válidos e completamente
/// diferentes, e o cliente faz o segundo.
void test_a_entrada_e_dominio_seguido_de_chave() {
    std::uint8_t entrada[TAM_ENTRADA];
    onyx::preparar_entrada(entrada, chave_de_mentira());

    for (std::size_t i = 0; i < TAM_DOMINIO; ++i) {
        TEST_ASSERT_EQUAL_UINT8(onyx::DOMINIO_RELAY[i], entrada[i]);
    }
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        TEST_ASSERT_EQUAL_UINT8(chave_de_mentira()[i],
                                entrada[TAM_DOMINIO + i]);
    }
}

/// A entrada muda quando a chave muda.
void test_chaves_diferentes_dao_entradas_diferentes() {
    std::uint8_t a[TAM_ENTRADA];
    std::uint8_t b[TAM_ENTRADA];

    std::uint8_t chave_a[TAM_MAILBOX];
    std::uint8_t chave_b[TAM_MAILBOX];
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        chave_a[i] = static_cast<std::uint8_t>(i);
        chave_b[i] = static_cast<std::uint8_t>(i + 1);
    }

    onyx::preparar_entrada(a, chave_a);
    onyx::preparar_entrada(b, chave_b);

    TEST_ASSERT_NOT_EQUAL_UINT8(a[TAM_DOMINIO], b[TAM_DOMINIO]);
}

/// A mesma chave dá a mesma entrada.
///
/// É o que faz um nó sobreviver a um reinício: quem se subscreveu
/// antes continua a ser esperado no mesmo endereço.
void test_a_mesma_chave_da_a_mesma_entrada() {
    std::uint8_t a[TAM_ENTRADA];
    std::uint8_t b[TAM_ENTRADA];
    onyx::preparar_entrada(a, chave_de_mentira());
    onyx::preparar_entrada(b, chave_de_mentira());

    for (std::size_t i = 0; i < TAM_ENTRADA; ++i) {
        TEST_ASSERT_EQUAL_UINT8(a[i], b[i]);
    }
}

/// `limpar` deixa a mailbox a zeros.
void test_limpar() {
    Mailbox m{};
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        m[i] = 0xFF;
    }
    onyx::limpar(m);
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, m[i]);
    }
}

/// ## O log imprime oito caracteres, não sessenta e quatro
///
/// A mailbox é o endereço de alguém. Um log de arranque que despeja
/// o endereço completo à frente de quem está à ver a placa está a
/// fazer mais do que `docs/protocolo.md` §Metadados observáveis
/// promete — e a promessa é explícita.
void test_o_log_imprime_oito_caracteres() {
    Mailbox m{};
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        m[i] = static_cast<std::uint8_t>(0xF0 + i);
    }

    char saida[32];
    onyx::mailbox_para_log(m, saida, sizeof saida);

    // Oito bytes de mailbox são dezasseis caracteres hexadecimais, e
    // dezasseis caracteres mais o terminador ocupam dezassete. A
    // asserção é sobre o comprimento, não sobre o terminador.
    TEST_ASSERT_EQUAL_UINT8(16, static_cast<std::uint8_t>(__builtin_strlen(saida)));
    TEST_ASSERT_EQUAL_UINT8('F', saida[0]);
    TEST_ASSERT_EQUAL_UINT8('0', saida[1]);
    // `m[7]` vale `0xF0 + 7`, que em hexadecimal é `F7`.
    TEST_ASSERT_EQUAL_UINT8('F', saida[14]);
    TEST_ASSERT_EQUAL_UINT8('7', saida[15]);
    TEST_ASSERT_EQUAL_UINT8('\0', saida[16]);
}

/// Um buffer pequeno demais dá cadeia vazia, e não escreve fora.
void test_log_em_buffer_pequeno() {
    Mailbox m{};
    char saida[4];
    onyx::mailbox_para_log(m, saida, sizeof saida);
    TEST_ASSERT_EQUAL_UINT8('\0', saida[0]);
}

/// A entrada de `test_main.cpp`.
void testes_mailbox() {
    RUN_TEST(test_o_dominio_sao_treze_bytes);
    RUN_TEST(test_o_dominio_byte_a_byte);
    RUN_TEST(test_a_entrada_e_dominio_seguido_de_chave);
    RUN_TEST(test_chaves_diferentes_dao_entradas_diferentes);
    RUN_TEST(test_a_mesma_chave_da_a_mesma_entrada);
    RUN_TEST(test_limpar);
    RUN_TEST(test_o_log_imprime_oito_caracteres);
    RUN_TEST(test_log_em_buffer_pequeno);
}
