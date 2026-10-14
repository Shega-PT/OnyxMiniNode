// =====================================================================
// onyx/mailbox.cpp — a única cripto do firmware
// ---------------------------------------------------------------------
// Duas funções com nomes distintos: uma que junta os 45 bytes que se
// vão derivar, e uma que os deriva. A separação é o que permite
// testar a primeira num computador — ver a nota em `mailbox.h`
// §Porque é que esta é a única função que o host não cobre.
// =====================================================================
#include "onyx/mailbox.h"

#ifdef ONYX_HASH_VIA_MBEDTLS
#include <mbedtls/sha256.h>
#endif

namespace onyx {

namespace {

/// Zero explícito, sem `std::memset`.
///
/// A biblioteca padrão oferece `std::fill_n`, que o compilador pode
/// optimizar a um `memset` com a mesma protecção que o resto. O que
/// se quer aqui não é um `memset` melhor, é um que o compilador
/// **não pode** eliminar por ser uma escrita que ninguém lê a
/// seguir. Por isso o destino é `volatile`: a abstracção que diz «isto
/// muda sempre» é a mesma que impede a optimização.
///
/// O parâmetro é um ponteiro e não um contentor por duas razões: um
/// `volatile std::array` não aceita atribuição elemento a elemento,
/// e um `volatile` a um array aplicava-se ao array inteiro e não aos
/// seus bytes. Um ponteiro a `uint8_t` é o único tipo que faz o que
/// esta função diz que faz.
inline void preencher(std::uint8_t* destino, std::uint8_t valor,
                      std::size_t n) noexcept {
    volatile std::uint8_t* p = destino;
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = valor;
    }
}

}  // namespace

void preparar_entrada(std::uint8_t (&entrada)[TAM_ENTRADA],
                      const std::uint8_t (&publica)[TAM_MAILBOX]) {
    preencher(entrada, 0, TAM_ENTRADA);

    // Domínio primeiro, chave depois. A ordem não é uma escolha
    // estética: `H(pub ‖ dom)` e `H(dom ‖ pub)` são dois hashes
    // igualmente válidos e completamente diferentes, e o cliente
    // faz o primeiro. Um erro aqui não dá nenhuma mensagem de erro
    // em lado nenhum — dá um sistema em que ninguém recebe nada.
    for (std::size_t i = 0; i < TAM_DOMINIO; ++i) {
        entrada[i] = DOMINIO_RELAY[i];
    }
    for (std::size_t i = 0; i < TAM_MAILBOX; ++i) {
        entrada[TAM_DOMINIO + i] = publica[i];
    }
}

#ifdef ONYX_HASH_VIA_MBEDTLS

Mailbox derivar_mailbox(const std::uint8_t (&publica)[TAM_MAILBOX]) {
    std::uint8_t entrada[TAM_ENTRADA];
    preparar_entrada(entrada, publica);

    Mailbox saida{};
    preencher(saida.data(), 0, TAM_MAILBOX);

    // `mbedtls_sha256_ret` devolve zero em sucesso e um código
    // negativo em falha. A falha não acontece para 45 bytes num bloco
    // de 64, mas o retorno não é ignorado: um hash que devolvesse
    // erro e enchesse o buffer na mesma, publicaria um endereço que não é
    // derivação de ninguém, e o sintoma — ninguém recebe nada —
    // não apontaria para aqui.
    //
    // Este caminho usa o **acelerador de hardware**. Ver §SHA-256.
    const int rc = mbedtls_sha256_ret(entrada, TAM_ENTRADA, saida.data(),
                                      0 /* é SHA-256, não SHA-224 */);

    preencher(entrada, 0, TAM_ENTRADA);

    if (rc != 0) {
        // Não há para onde voltar com um erro: quem chama é uma
        // máquina de estados que não sabe lidar com uma falha. A
        // decisão é devolver a mailbox nula, que é um endereço a que
        // ninguém se subscreve — um depósito nela é perdido, que é
        // a mesma consequência de um relay em baixo, e muito melhor
        // do que devolver 32 bytes não-derivados que podem colidir
        // com uma mailbox real e mandar a mensagem de alguém a quem
        // não era destino.
        preencher(saida.data(), 0, TAM_MAILBOX);
    }

    return saida;
}

#endif  // ONYX_HASH_VIA_MBEDTLS

void limpar(Mailbox& mailbox) noexcept {
    preencher(mailbox.data(), 0, TAM_MAILBOX);
}

void mailbox_para_log(const Mailbox& mailbox, char* saida,
                      std::size_t tamanho) noexcept {
    static constexpr char HEX[] = "0123456789ABCDEF";
    // Oito caracteres, e não 64. Ver a nota na declaração.
    constexpr std::size_t CARACTERES = 8;

    if (tamanho < CARACTERES * 2 + 1) {
        if (tamanho > 0) {
            saida[0] = '\0';
        }
        return;
    }

    for (std::size_t i = 0; i < CARACTERES; ++i) {
        saida[i * 2] = HEX[mailbox[i] >> 4];
        saida[i * 2 + 1] = HEX[mailbox[i] & 0x0F];
    }
    saida[CARACTERES * 2] = '\0';
}

// ## SHA-256
//
// ## Onde está o acelerador
//
// O ESP-IDF expõe o periférico através de `esp_sha256_start`,
// `esp_sha256_update` e `esp_sha256_finish`, e essa API **não** é o
// caminho que este firmware usa — nem porque a API não esteja
// disponível no core Arduino, nem por ser preciso.
//
// O que se usa é o mbedTLS, e o mbedTLS que este firmware linka
// **vai ao periférico**. A cadeia é esta, e vale a pena escrevê-la
// porque é tudo menos óbvia:
//
//   * o core Arduino-ESP32 embute o ESP-IDF 4.4.7;
//   * o ESP-IDF compila o mbedTLS com `MBEDTLS_SHA256_ALTERNATIVE`
//     ligado, o que faz o `mbedtls_sha256_ret` chamar a implementação
//     do próprio ESP-IDF em vez da original do mbedTLS;
//   * essa implementação é `esp_sha256.c`, que chama `esp_sha_block`,
//     que é o registo do periférico.
//
// Para quem chega aqui com `nm` à mão: o símbolo a procurar é
// `esp_sha_block` dentro de `libmbedcrypto.a`, e o objecto que o
// consome é `esp_sha256.c.obj`.
//
// ## O que isto não compra
//
// O hash de uma mailbox de 45 bytes é, na prática, inferior a um
// microssegundo. O acelerador não está aqui por desempenho: está
// aqui porque usar a primitiva que o fabricante testou é melhor do
// que escrever uma segunda implementação para o mesmo periférico e
// chamá-la de optimização.
//
// E note-se o que **não** há neste ficheiro: AEAD, assinaturas,
// qualquer coisa que exija uma chave secreta. Um relay não tem
// segredos para proteger — `docs/ameacas.md` §A2 — e um ficheiro que
// precisasse de um seria um relay a fazer mais do que o relay faz.

}  // namespace onyx
