// =====================================================================
// main.cpp — o arranque
// ---------------------------------------------------------------------
// Um ficheiro, e faz três coisas: levanta o ponto de acesso, corre o
// laço da rede, e não deixa que nenhum dos dois bloqueie o outro.
//
// A palavra-passe do ponto de acesso é derivada da identidade de
// hardware da placa. Não é uma escolha de comodidade: uma palavra
// passe de fábrica partilhada por todas as placas é uma palavra-passe
// que toda a gente sabe, e `docs/ameacas.md` §Mitigações trata a
// palavra-passe como a primeira mitigação contra um relay aberto.
// =====================================================================
#include <Arduino.h>
#include <esp_system.h>

#include <cstdio>

#include "onyx/limites.h"
#include "onyx/rede.h"

namespace {

/// O SSID por omissão.
///
/// Inclui o endereço MAC, e não um número de série: o MAC é único
/// por placa e está impresso na etiqueta, o que faz com que duas
/// placas lado a lado não colidam no mesmo SSID.
constexpr char SSID_BASE[] = "onyxmininode";

/// Porta do log, para o `pio device monitor`.
constexpr int BAUDRATE = 115200;

/// A palavra-passe, em hexadecimal, sem `\0`.
///
/// A identidade de hardware é um identificador de 64 bits da Espressif
/// (`esp_efuse_mac_get_default`). Derivar a palavra-passe dele é
/// melhor do que um número de série, que nem todas as placas têm, e
/// melhor do que um valor constantes numa só placa, que é um segredo
/// publicado no repositório.
///
/// Derivar, não copiar: a palavra-passe é o identificador com
/// `#` trocado por `0` e `a` trocado por `1`, de modo que tem sempre
/// exactamente doze caracteres e caracteres que a WPA2 aceita sem
/// ambiguidade. Um identificador com `:` ou `-` seria aceite, mas
/// obrigaria a quem o digita a acertar em dois caracteres que se
/// confundem à primeira vista.
void palavra_passe(char* saida, std::size_t tamanho) {
    std::uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);

    // Não se chama `HEX`. `Print.h`, que vem com o core Arduino,
    // define `HEX` como 16 — para o formato de `print()`. Uma
    // constante local com o mesmo nome é expandida pela
    // pré-processador **antes** de o compilador a ver, e o
    // resultado é `static constexpr char 16[] = "..."`, que é um
    // erro cuja mensagem aponta para o `Print.h` e não para aqui.
    static constexpr char ALGARISMOS[] = "0123456789abcdef";
    std::size_t n = 0;
    for (std::uint8_t byte : mac) {
        if (n + 2 >= tamanho) {
            break;
        }
        saida[n++] = ALGARISMOS[byte >> 4];
        saida[n++] = ALGARISMOS[byte & 0x0F];
    }
    saida[n] = '\0';

    for (std::size_t i = 0; i < n; ++i) {
        if (saida[i] == 'a') {
            saida[i] = '1';
        } else if (saida[i] == ':' || saida[i] == '-') {
            saida[i] = '0';
        }
    }
}

/// Escreve uma linha de arranque.
///
/// Cada linha diz o que o nó **é** e não o que ele faz. Um log de
/// arranque que despeja o estado interno — heap livre, filas, número
/// de sessões — seria útil e seria uma pessoa a fotografar a placa
/// com um ecrã cheio de informação que não precisa de estar ali.
void escrever_arranque(const onyx::Rede& rede, const char* ssid,
                       const char* passe) {
    std::printf("\r\n");
    std::printf("  OnyxMiniNode\r\n");
    std::printf("  um relay do OnyxChat, e nada mais\r\n");
    std::printf("\r\n");
    std::printf("  rede      %s\r\n", ssid);
    std::printf("  passe     %s\r\n", passe);
    std::printf("  endereco  %s:%u\r\n", rede.endereco(),
                static_cast<unsigned>(rede.porta()));
    std::printf("\r\n");
    std::printf("  OnyxChat liga-se a este endereco com\r\n");
    std::printf("  ONYXCHAT_RELAY=\"%s:%u\"\r\n", rede.endereco(),
                static_cast<unsigned>(rede.porta()));
    std::printf("\r\n");
    std::printf("  este no nao ve conteudo nenhum e nao tem chaves\r\n");
    std::printf("\r\n");
}

}  // namespace

/// ## Porque é que o laço está aqui e não em `loop()`
///
/// Porque `loop()` no core Arduino é chamado pelo mesmo task que
/// atende o Wi-Fi, e um laço que passe trinta milissegundos em
/// `select` atrasa o driver. O `select` aqui tem vinte, o que é
/// suficiente para um relay de rendezvous — a latência só importa
/// quando alguém está a falar, e nesse caso há dados a chegar e o
/// `select` acorda antes do tempo.
void loop() {
    static onyx::Rede rede;

    char passe[16] = {0};
    palavra_passe(passe, sizeof passe);

    char ssid[40] = {0};
    std::snprintf(ssid, sizeof ssid, "%s-%02X%02X", SSID_BASE,
                  static_cast<unsigned>(esp_random() & 0xFF),
                  static_cast<unsigned>(esp_random() & 0xFF));

    if (!rede.arrancar_ap(ssid, passe)) {
        // Sem rede não há relay. Dizer que sim e continuar seria pior
        // do que arrancar: quem tentasse ligar-se bateria a bateria
        // sem obter resposta não saberia se o problema era o relay ou
        // a placa.
        Serial.begin(BAUDRATE);
        for (;;) {
            Serial.println("  nao foi possivel levantar o ponto de acesso");
            Serial.println("  a placa precisa de corrente suficiente no pino 3V3");
            delay(2000);
        }
    }

    escrever_arranque(rede, ssid, passe);

    // A assinatura é `uint64_t` e o relógio é o `millis()` do Arduino,
    // que é um contador de 32 bits. O widening é feito uma vez aqui e
    // não em cada chamada, porque `millis()` a dar a volta é o que
    // faria o `expurgar` receber um instante no passado e apagar tudo
    // o que está na caixa.
    std::uint64_t base = ::millis();

    for (;;) {
        const std::uint64_t agora = base + static_cast<std::uint64_t>(
                                                 ::millis());
        if (!rede.passo(agora)) {
            // O laço só para se a escuta desaparecer. Tenta-se
            // re-ligar em vez de ficar a interrogar um socket morto.
            delay(1000);
            if (!rede.arrancar_ap(ssid, passe)) {
                delay(1000);
            }
        }
        // Cede ao watchdog e ao FreeRTOS. Sem isto, um laço de vinte
        // milissegundos com tasks de prioridade zero à volta é um
        // laço que reinicia a placa, e o sintoma seria «o relay
        // cai sempre depois de um minuto», que é a pior coisa
        // possível de diagnosticar.
        yield();
    }
}

void setup() { /* o arranque está todo em `loop` */ }
