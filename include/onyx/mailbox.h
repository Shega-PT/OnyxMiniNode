// =====================================================================
// onyx/mailbox.h — derivar a caixa a partir da chave pública
// ---------------------------------------------------------------------
// O relay nunca vê uma chave. Vê um hash de 32 bytes, derivado de uma
// chave pública que nunca atravessa o fio. Este ficheiro é a
// implementação dessa derivação, e é a única cripto do firmware
// inteiro.
//
// A fórmula é uma linha e não muda:
//
//     mailbox = SHA-256("ONYX/RELAY/v1" ‖ publica)
//
// onde `publica` são 32 bytes e o prefixo tem 13. Concatenação crua:
// sem separador, sem comprimento, sem codificação. É a mesma fórmula
// que o Python calcula com `hashlib.sha256` e que o Rust calcula com
// `sha2`, e o teste `test_mailbox.cpp` compara os três — porque um
// relay cujo hash não bate é um relay a que ninguém chega.
// =====================================================================
#ifndef ONYX_MAILBOX_H
#define ONYX_MAILBOX_H

#include <array>
#include <cstdint>

#include "onyx/limites.h"

namespace onyx {

/// O domínio de derivação, byte a byte.
///
/// O valor é **literal** e nunca é construído em execução. Construí-lo
/// a partir de uma constante `std::string` seria o mesmo valor, e
/// valeria menos: um literais não pode ser alterado por um
/// `sed` distraído sem o compilador dar queixa, e esta é
/// exactamente a linha que tem de bater com o Python e com o Rust.
inline constexpr std::uint8_t DOMINIO_RELAY[] = {
    'O', 'N', 'Y', 'X', '/', 'R', 'E', 'L', 'A', 'Y', '/', 'v', '1'};

inline constexpr std::size_t TAM_DOMINIO = sizeof(DOMINIO_RELAY);

/// Bytes que a derivação consome: o domínio e a chave pública.
inline constexpr std::size_t TAM_ENTRADA =
    TAM_DOMINIO + TAM_MAILBOX;

/// Uma mailbox: 32 bytes, e nada mais.
using Mailbox = std::array<std::uint8_t, TAM_MAILBOX>;

/// Escreve, em `entrada`, os 45 bytes que a derivação consome.
///
/// É a parte que está separada de propósito. A ordem — domínio
/// primeiro, chave depois — e o valor exacto dos 13 bytes do domínio
/// são as duas coisas que um relay mal feito estraga, e nenhuma das
/// duas precisa de cripto para ser testada. Por isso esta função não
/// sabe o que é SHA-256 e por isso compila e corre em `pio test -e
/// native`, num computador, sem placa e sem mbedTLS.
///
/// O teste `test_mailbox.cpp` §a entrada bate byte a byte é o que
/// apanha o erro que os testes de integralidade não apanham: um
/// domínio com um `\0` a mais, ou com o `/` trocado, produz um hash
/// perfeitamente válido e completamente diferente, e o sintoma —
///
///     depósito aceite, ninguém subscreve, nada acontece
///
/// — não aponta para aqui.
void preparar_entrada(std::uint8_t (&entrada)[TAM_ENTRADA],
                      const std::uint8_t (&publica)[TAM_MAILBOX]);

/// Deriva a mailbox de uma chave pública Ed25519 de 32 bytes.
///
/// ## Porquê que isto tem de bater exactamente
///
/// A mailbox é o endereço de correio do sistema. Se o MiniNode
/// derivar `H(pub)` e o cliente derivar `H'(pub)`, o `ENVIAR` vai
/// parar a uma caixa que ninguém subscreve e o `CAIXA` nunca sai —
/// e nada no fio indica isso: o depósito responde `OK`, e um depósito
/// bem-sucedido numa caixa que ninguém lê é indistinguível de um
/// sistema que funciona.
///
/// ## Porquê SHA-256 e não outra coisa
///
/// Não é escolha: é a fórmula do OnyxChat, e mudar a função de
/// derivação tornaria este nó incompatível com o cliente. O que é
/// escolha é **não** usar uma KDF cara. O argumento é o mesmo que o
/// da licença: a derivação não tem segredo. A chave pública é
/// pública, o prefixo é público, e um atacante que saiba a fórmula
/// calcula a mailbox — que é precisamente o que se pretende, porque
/// o `ENVIAR` tem de ser possível a quem tem a chave do destinatário
/// e a mais ninguém. Um PBKDF2 aqui custaria CPU a toda a gente para
/// proteger algo que não é segredo.
///
/// ## Porque é que esta é a única função que o host não cobre
///
/// Porque precisa de mbedTLS, e o mbedTLS não está no caminho de
/// _include_ de um compilador de estação de trabalho. O guard
/// `ONYX_HASH_VIA_MBEDTLS` — ligado por omissão e desligado no
/// ambiente de host — é o único ponto do firmware em que a resposta
/// depende de uma biblioteca de terceiros em vez do código deste
/// repositório, e por isso a função fica para o alvo. Ver
/// `docs/testes.md` §O que a camada de host não apanha.

#ifdef ONYX_HASH_VIA_MBEDTLS
Mailbox derivar_mailbox(const std::uint8_t (&publica)[TAM_MAILBOX]);
#endif

/// Apaga a mailbox depois de usada.
///
/// A mailbox não é um segredo — ver acima — mas é o endereço de
/// alguém, e apaga-lo é mais barato do que explicar porque é que a
/// memória foi deixada como estava. A função é `noexcept` e não
/// aloca: é o que a permite chamar de um destructor.
void limpar(Mailbox& mailbox) noexcept;

/// Escreve a mailbox em hexadecimal maiúsculo, para o log de arranque.
///
/// Só para diagnóstico, e só quando o nível de log está ligado. O
/// `docs/protocolo.md` §Metadados observáveis é explícito sobre o que
/// um operador vê, e um log de arranque que despeja a mailbox para o
/// ecrã de quem está à frente da placa é mais do que a tabela
///-promete. Por isso o que se imprime é um **prefixo** de oito
/// caracteres: suficiente para distinguir duas placas numa rede, sem
/// ser o endereço completo de ninguém.
void mailbox_para_log(const Mailbox& mailbox, char* saida,
                      std::size_t tamanho) noexcept;

}  // namespace onyx

#endif  // ONYX_MAILBOX_H
