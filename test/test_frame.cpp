// =====================================================================
// test_frame.cpp — o enquadramento
// ---------------------------------------------------------------------
// Estes são os testes que valem mais do que os outros, e a razão está
// escrita em `docs/testes.md`: é aqui que entra o que outro escolheu.
//
// O enquadramento tem uma propriedade que nenhuma outra parte do
// firmware tem — **um valor errado não dá resultado errado, dá
// memória alocada a alguém que não a pediu**. Um `MAX_CORPO` que
// permitisse quatro gigabytes passaria todos os testes de
// funcionalidade e rebentaria a placa.
// =====================================================================
#include <unity.h>

#include <string>

#include <onyx/frame.h>
#include <onyx/limites.h>

using onyx::ErroLeitura;
using onyx::Frame;
using onyx::MAX_COMPRIMENTO;
using onyx::MAX_CORPO;

namespace {

/// Lê o comprimento dos primeiros quatro bytes.
std::uint32_t comprimento_de(const std::vector<std::uint8_t>& b) {
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

/// Um corpo de `n` bytes, com um padrão que se reconhece.
std::vector<std::uint8_t> corpo(std::size_t n) {
    std::vector<std::uint8_t> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = static_cast<std::uint8_t>(0xA0 + (i % 0x5F));
    }
    return c;
}

}  // namespace

// =====================================================================
// Escrever
// =====================================================================

/// O comprimento inclui o byte do tipo.
///
/// É o teste que apanha o erro mais fácil de cometer e mais difícil
/// de ver: escrever `len(corpo)` em vez de `1 + len(corpo)` produz
/// pacotes que o próprio nó não consegue ler de volta, porque a
/// validação de leitura veria um corpo um byte mais curto do que o
/// que foi escrito. A falha apareceria como «mensagens truncadas»
/// em vez de «pacotes mal formados», o que aponta para o sítio
/// errado.
void test_escrever_inclui_o_tipo_no_comprimento() {
    std::vector<std::uint8_t> saida;
    const std::vector<std::uint8_t> c = corpo(10);

    TEST_ASSERT_TRUE(
        onyx::escrever(saida, onyx::MSG_SUBSCREVER, c));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, comprimento_de(saida),
                                     "o comprimento não inclui o tipo");
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_SUBSCREVER, saida[4]);
    TEST_ASSERT_EQUAL_UINT8(0xA0, saida[5]);
    TEST_ASSERT_EQUAL_UINT8(0xA1, saida[6]);
}

/// Corpo vazio dá comprimento 1.
///
/// É o `FECHO` e o `OK`, e é o único caso em que `comprimento - 1` dá
/// zero. Se `escrever` tratasse o corpo vazio como um caso especial
/// qualquer, estes dois deixavam de funcionar.
void test_corpo_vazio_da_comprimento_um() {
    std::vector<std::uint8_t> saida;
    TEST_ASSERT_TRUE(onyx::escrever(saida, onyx::MSG_FECHO, nullptr, 0));

    TEST_ASSERT_EQUAL_UINT32(1, comprimento_de(saida));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_FECHO, saida[4]);
    TEST_ASSERT_EQUAL_UINT32(5, saida.size());
}

/// O corpo máximo escreve; um a mais não.
void test_limite_do_corpo() {
    std::vector<std::uint8_t> saida;
    const std::vector<std::uint8_t> exacto = corpo(MAX_CORPO);

    TEST_ASSERT_TRUE(
        onyx::escrever(saida, onyx::MSG_ENVIAR, exacto));
    TEST_ASSERT_EQUAL_UINT32(MAX_COMPRIMENTO, comprimento_de(saida));

    const std::vector<std::uint8_t> grande = corpo(MAX_CORPO + 1);
    const std::size_t antes = saida.size();
    TEST_ASSERT_FALSE(
        onyx::escrever(saida, onyx::MSG_ENVIAR, grande));
    // E a falha **não** deixa o buffer a meio. Uma `escrever` que
    // reservasse e depois falhasse deixaria o vector com um
    // comprimento que não corresponde ao conteúdo, e o socket
    // escreveria lixo — um bug que só apareceria com um cliente que
    // mandasse um frame grande, que é um cliente a falar.
    TEST_ASSERT_EQUAL_UINT32(antes, saida.size());
}

/// `CAIXA` conta o byte do tipo do frame dentro do limite.
///
/// O `CAIXA` tem um byte a mais que um `ENVIAR` de corpo igual, e é
/// por isso que o limite é `MAX_CORPO - 1`. Um `CAIXA` de corpo
/// máximo escreveria um pacote um byte maior do que a leitura aceita,
/// e a falha apareceria do outro lado — num nó a escrever um pacote
/// que ele mesmo não leria.
void test_caixa_conta_o_byte_do_tipo() {
    std::vector<std::uint8_t> saida;
    const std::vector<std::uint8_t> c = corpo(MAX_CORPO - 1);

    TEST_ASSERT_TRUE(onyx::escrever_caixa(saida, onyx::FRAME_CHAT, c.data(),
                                          c.size()));
    TEST_ASSERT_EQUAL_UINT32(MAX_COMPRIMENTO, comprimento_de(saida));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_CAIXA, saida[4]);
    TEST_ASSERT_EQUAL_UINT8(onyx::FRAME_CHAT, saida[5]);

    const std::vector<std::uint8_t> grande = corpo(MAX_CORPO);
    TEST_ASSERT_FALSE(onyx::escrever_caixa(saida, onyx::FRAME_CHAT,
                                           grande.data(), grande.size()));
}

/// `ERRO` leva o código e a mensagem.
void test_erro() {
    std::vector<std::uint8_t> saida;
    TEST_ASSERT_TRUE(onyx::escrever_erro(saida, onyx::ERRO_SEM_ESPACO,
                                         "caixa cheia"));

    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_ERRO, saida[4]);
    TEST_ASSERT_EQUAL_UINT8(onyx::ERRO_SEM_ESPACO, saida[5]);
    TEST_ASSERT_EQUAL_UINT8('c', saida[6]);
    TEST_ASSERT_EQUAL_UINT32(2 + 11, comprimento_de(saida));
}

/// A mensagem de `ERRO` é cortada, e não transbordada.
///
/// O limite de 96 bytes é o que impede que o conteúdo do erro — que
/// é decidido por quem o provocou — possa ser usado para escrever
/// fora da reserva.
void test_mensagem_de_erro_e_cortada() {
    std::vector<std::uint8_t> saida;
    const std::string enorme(500, 'x');

    TEST_ASSERT_TRUE(
        onyx::escrever_erro(saida, onyx::ERRO_PAYLOAD_MALFORMADO,
                            enorme.c_str()));
    TEST_ASSERT_EQUAL_UINT32(2 + onyx::TAM_MENSAGEM_ERRO,
                             comprimento_de(saida));
}

// =====================================================================
// Ler
// =====================================================================

/// Ler o que se escreveu dá o que se escreveu.
void test_ida_e_volta() {
    std::vector<std::uint8_t> saida;
    const std::vector<std::uint8_t> c = corpo(33);
    TEST_ASSERT_TRUE(onyx::escrever(saida, onyx::MSG_SUBSCREVER, c));

    std::size_t pos = 0;
    Frame lida;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Completa),
                          static_cast<int>(onyx::ler(saida, pos, lida)));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_SUBSCREVER, lida.tipo);
    TEST_ASSERT_EQUAL_UINT32(c.size(), lida.corpo.size());
    TEST_ASSERT_EQUAL_UINT8(c[0], lida.corpo[0]);
    TEST_ASSERT_EQUAL_UINT32(saida.size(), pos);
}

/// Duas mensagens num buffer, lidas as duas.
void test_duas_mensagens_no_mesmo_buffer() {
    std::vector<std::uint8_t> saida;
    TEST_ASSERT_TRUE(onyx::escrever(saida, onyx::MSG_FECHO, nullptr, 0));
    const std::vector<std::uint8_t> c = corpo(8);
    TEST_ASSERT_TRUE(onyx::acrescentar(saida, onyx::MSG_SUBSCREVER, c.data(),
                                       c.size()));

    std::size_t pos = 0;
    Frame lida;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Completa),
                          static_cast<int>(onyx::ler(saida, pos, lida)));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_FECHO, lida.tipo);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Completa),
                          static_cast<int>(onyx::ler(saida, pos, lida)));
    TEST_ASSERT_EQUAL_UINT8(onyx::MSG_SUBSCREVER, lida.tipo);
    TEST_ASSERT_EQUAL_UINT32(saida.size(), pos);
}

/// ## Consumo parcial
///
/// Este é o teste mais importante do ficheiro. `ler` tem de distinguir
/// «a mensagem não chegou toda» de «a mensagem está errada» — e a
/// distinção é o que permite a um cliente mandar um pacote em duas
/// partes sem que o relay feche a ligação.
///
/// Um `Incompleta` **não consome nada**: o cursor volta ao sítio de
/// onde partiu. Um cursor que devolvesse a posição sem a mover
/// seria uma espera infinita; um cursor que a movesse para o fim do
/// buffer deitaria fora os bytes que já chegaram, e a mensagem nunca
/// se completaria.
void test_consumo_parcial() {
    std::vector<std::uint8_t> saida;
    const std::vector<std::uint8_t> c = corpo(200);
    TEST_ASSERT_TRUE(onyx::escrever(saida, onyx::MSG_SUBSCREVER, c));

    std::vector<std::uint8_t> recebido(saida.begin(), saida.begin() + 10);
    std::size_t pos = 0;
    Frame lida;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Incompleta),
                          static_cast<int>(onyx::ler(recebido, pos, lida)));
    // Não consumiu nada, e é por isso que os dez bytes que já
    // chegaram continuam no buffer.
    TEST_ASSERT_EQUAL_UINT32(0, pos);
    TEST_ASSERT_EQUAL_UINT32(10, recebido.size());

    // Chega o resto.
    recebido.insert(recebido.end(), saida.begin() + 10, saida.end());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Completa),
                          static_cast<int>(onyx::ler(recebido, pos, lida)));
    TEST_ASSERT_EQUAL_UINT32(c.size(), lida.corpo.size());
    TEST_ASSERT_EQUAL_UINT32(recebido.size(), pos);
}

/// Um comprimento de zero é erro, não mensagem.
///
/// E o cursor **não** avança: a ligação fecha e não interessa onde
/// estava.
void test_comprimento_zero_e_erro() {
    std::vector<std::uint8_t> pacote = {0x00, 0x00, 0x00, 0x00, 0x01};
    std::size_t pos = 0;
    Frame lida;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::ComprimentoInvalido),
                          static_cast<int>(onyx::ler(pacote, pos, lida)));
    TEST_ASSERT_EQUAL_UINT32(0, pos);
}

/// ## Um comprimento de quatro mil milhões
///
/// É o teste que justifica a ordem das verificações. Um pacote com
/// quatro bytes que dizem `0xFFFFFFFF` e nada mais é a forma mais
/// barata de tentar esgotar a memória de uma placa. A resposta
/// correcta é um erro imediato, sem esperar pelo resto — que nunca
/// chega, e cuja chegada custaria gigabytes.
void test_comprimento_absurdo_e_recusado_sem_esperar() {
    std::vector<std::uint8_t> pacote = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    std::size_t pos = 0;
    Frame lida;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::ComprimentoInvalido),
                          static_cast<int>(onyx::ler(pacote, pos, lida)));
    TEST_ASSERT_EQUAL_UINT32(0, pos);
}

/// Um buffer vazio é «incompleto», não «erro».
void test_buffer_vazio_e_incompleto() {
    const std::vector<std::uint8_t> nada;
    std::size_t pos = 0;
    Frame lida;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Incompleta),
                          static_cast<int>(onyx::ler(nada, pos, lida)));
    TEST_ASSERT_EQUAL_UINT32(0, pos);
}

/// Quatro bytes a menos do que é preciso é «incompleto».
void test_cabecalho_incompleto() {
    std::vector<std::uint8_t> tres = {0x05, 0x00, 0x00};
    std::size_t pos = 0;
    Frame lida;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ErroLeitura::Incompleta),
                          static_cast<int>(onyx::ler(tres, pos, lida)));
}

/// A entrada do ficheiro. Ver `test_main.cpp` §Porque é que o `main`
/// está num ficheiro só.
void testes_frame() {
    RUN_TEST(test_escrever_inclui_o_tipo_no_comprimento);
    RUN_TEST(test_corpo_vazio_da_comprimento_um);
    RUN_TEST(test_limite_do_corpo);
    RUN_TEST(test_caixa_conta_o_byte_do_tipo);
    RUN_TEST(test_erro);
    RUN_TEST(test_mensagem_de_erro_e_cortada);
    RUN_TEST(test_ida_e_volta);
    RUN_TEST(test_duas_mensagens_no_mesmo_buffer);
    RUN_TEST(test_consumo_parcial);
    RUN_TEST(test_comprimento_zero_e_erro);
    RUN_TEST(test_comprimento_absurdo_e_recusado_sem_esperar);
    RUN_TEST(test_buffer_vazio_e_incompleto);
    RUN_TEST(test_cabecalho_incompleto);
}
