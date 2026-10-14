// =====================================================================
// onyx/frame.cpp — escrever e ler o enquadramento
// ---------------------------------------------------------------------
// Este ficheiro tem duas funções e meia. Duas: `escrever` e `ler`. Meia:
// a validação do comprimento, que vive dentro de ambas porque depende
// do contexto de cada uma.
//
// Nada aqui tem estado. É o que permite testar o enquadramento num
// computador sem placa, sem mock e sem lwIP: `test_frame.cpp` chama
// estas funções com `std::vector` e passa.
// =====================================================================
#include "onyx/frame.h"

#include <cstring>

namespace onyx {

namespace {

/// Escreve os quatro bytes do comprimento, little-endian.
///
/// Explicitos, e não um `memcpy` de um `uint32_t`: a representação de
/// inteiros em memória é implementação-defined, e a única coisa que o
/// protocolo garante é a ordem dos bytes. Um `memcpy` num portátil
/// com big-endian escreveria o pacote ao contrário e o cliente
/// receberia um comprimento enorme — o que, pela validação de
/// baixo, viraria logo `ComprimentoInvalido`. Ou seja, não leria
/// nada, mas a causa seria difícil de ver.
inline void escrever_comprimento(std::uint8_t* destino,
                                 std::uint32_t valor) noexcept {
    destino[0] = static_cast<std::uint8_t>(valor & 0xFFu);
    destino[1] = static_cast<std::uint8_t>((valor >> 8) & 0xFFu);
    destino[2] = static_cast<std::uint8_t>((valor >> 16) & 0xFFu);
    destino[3] = static_cast<std::uint8_t>((valor >> 24) & 0xFFu);
}

/// Lê os quatro bytes do comprimento, little-endian.
///
/// Simétrico de `escrever_comprimento`, e pela mesma razão.
inline std::uint32_t ler_comprimento(const std::uint8_t* origem) noexcept {
    return static_cast<std::uint32_t>(origem[0]) |
           (static_cast<std::uint32_t>(origem[1]) << 8) |
           (static_cast<std::uint32_t>(origem[2]) << 16) |
           (static_cast<std::uint32_t>(origem[3]) << 24);
}

/// Copia `n` bytes, tolerando `origem` nula quando `n` é zero.
///
/// O `if` não é decorativo: `std::vector::data()` num vector vazio
/// pode devolver `nullptr`, e passar `nullptr` a `memcpy` com
/// comprimento zero é comportamento indefinido pela norma — e numa
/// placa onde o compilador tem `-fno-exceptions`, o que «não crashing»
/// significa é ninguém dizer nada. `FECHO` e `OK` têm corpo vazio, e
/// portanto passam por aqui em todas as subscrições e em todos os
/// acuses.
void copiar(std::uint8_t* destino, const std::uint8_t* origem,
            std::size_t n) noexcept {
    if (n == 0) {
        return;
    }
    if (origem != nullptr) {
        std::memcpy(destino, origem, n);
    }
}

/// O tipo é um dos seis do protocolo, e o corpo cabe no limite.
///
/// Uma função à parte, e não duas cópias da mesma condição: `escrever`
/// tem de validar **antes** de limpar o buffer do chamador, e
/// `acrescentar` tem de validar antes de mexer no fim dele. Com a
/// validação em dois sítios, uma das duas copias um dia deixaria de
/// validar e ninguém notaria.
bool mensagem_valida(std::uint8_t tipo, std::size_t tamanho_corpo) noexcept {
    if (tamanho_corpo > MAX_CORPO) {
        return false;
    }
    switch (tipo) {
        case MSG_SUBSCREVER:
        case MSG_ENVIAR:
        case MSG_FECHO:
        case MSG_ERRO:
        case MSG_OK:
        case MSG_CAIXA:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool escrever(std::vector<std::uint8_t>& saida, std::uint8_t tipo,
              const std::uint8_t* corpo, std::size_t tamanho_corpo) {
    // Valida **antes** de limpar. Uma falha tem de deixar o buffer do
    // chamador como estava: quem chama é a sessão, e uma sessão que
    // perde o `OK` que já tinha na fila porque o cliente a seguir
    // mandou um frame grande demais fica com o cliente à espera de um
    // acuse que já tinha sido produzido.
    if (!mensagem_valida(tipo, tamanho_corpo)) {
        return false;
    }
    saida.clear();
    return acrescentar(saida, tipo, corpo, tamanho_corpo);
}

bool acrescentar(std::vector<std::uint8_t>& saida, std::uint8_t tipo,
                 const std::uint8_t* corpo, std::size_t tamanho_corpo) {
    if (!mensagem_valida(tipo, tamanho_corpo)) {
        return false;
    }

    const std::uint32_t comprimento =
        static_cast<std::uint32_t>(1 + tamanho_corpo);

    // `resize` **antes** de escrever, e não `reserve` + `push_back`.
    //
    // `reserve` aloca mas não muda o tamanho, e `data()` devolve um
    // ponteiro para memória cujo tamanho é zero.
    // Escrever quatro bytes lá dentro é comportamento indefinido, e o
    // `push_back` seguinte escreve por cima do primeiro. O resultado
    // é um pacote cujo comprimento sai a zero e cujo tipo sai no sítio
    // errado, e o sintoma é um nó que «não recebe nada» em vez de
    // uma mensagem de compilação.
    const std::size_t inicio = saida.size();
    saida.resize(inicio + 4 + comprimento);
    escrever_comprimento(saida.data() + inicio, comprimento);
    saida[inicio + 4] = tipo;
    copiar(saida.data() + inicio + 5, corpo, tamanho_corpo);
    return true;
}

bool escrever_caixa(std::vector<std::uint8_t>& saida,
                    std::uint8_t frame_tipo, const std::uint8_t* frame_corpo,
                    std::size_t tamanho_frame) {
    // O corpo de `CAIXA` é `frame_tipo(1) ‖ frame_corpo(N)`, e por
    // isso o limite aqui é `MAX_CORPO - 1`: o byte do tipo do frame
    // conta para o corpo da mensagem, e somar depois daria um pacote
    // um byte maior do que o que a validação de leitura aceita.
    // O erro apareceria do outro lado, como um `ComprimentoInvalido`
    // num pacote que este nó acabou de escrever — que é a pior
    // forma de um limite: só falha quando há tráfego a sério.
    if (tamanho_frame + 1 > MAX_CORPO) {
        return false;
    }

    saida.clear();
    const std::uint32_t comprimento =
        static_cast<std::uint32_t>(2 + tamanho_frame);
    saida.resize(4 + comprimento);
    escrever_comprimento(saida.data(), comprimento);
    saida[4] = MSG_CAIXA;
    saida[5] = frame_tipo;
    copiar(saida.data() + 6, frame_corpo, tamanho_frame);
    return true;
}

bool escrever_erro(std::vector<std::uint8_t>& saida, std::uint8_t codigo,
                   const char* mensagem) {
    std::size_t tamanho = 0;
    if (mensagem != nullptr) {
        // `strnlen` em vez de `strlen`: a string pode não estar
        // terminada se um chamador passar um buffer exacto, e
        // `strlen` num buffer não terminado lê para fora dos limites
        // até encontrar um zero por sorte. Numa placa, «até encontrar
        // um zero por sorte» é «até rebentar a stack».
        tamanho = strnlen(mensagem, TAM_MENSAGEM_ERRO);
    }

    saida.clear();
    const std::uint32_t comprimento =
        static_cast<std::uint32_t>(2 + tamanho);
    saida.resize(4 + comprimento);
    escrever_comprimento(saida.data(), comprimento);
    saida[4] = MSG_ERRO;
    saida[5] = codigo;
    copiar(saida.data() + 6, reinterpret_cast<const std::uint8_t*>(mensagem),
          tamanho);
    return true;
}

ErroLeitura ler(const std::vector<std::uint8_t>& buffer,
                std::size_t& posicao, Frame& saida) {
    // ## O contrato do cursor
    //
    // **Um `Incompleta` não consome nada.** O cursor volta ao sítio de
    // onde partiu, e quem chamou fica com o buffer intacto para
    // tentar quando chegar mais.
    //
    // A alternativa — avançar para o fim do que está disponível — é
    // mais barata e está errada: um pacote partido a meio deixa o
    // cursor depois dos bytes do corpo que chegaram, e quem esvazia o
    // buffer a partir do cursor deita fora exactamente esses bytes.
    // O sintoma seria uma mensagem que nunca se completa, repetida
    // para sempre, e nenhuma mensagem de erro.
    //
    // O custo de ficar com o buffer intacto é nenhum: o buffer só
    // cresce até ao tamanho de uma mensagem, e `Rede::acumular`
    // fecha a ligação quando passa esse tamanho.
    const std::size_t inicio = posicao;
    const std::size_t disponivel = buffer.size() - posicao;

    if (disponivel < 4) {
        posicao = inicio;
        return ErroLeitura::Incompleta;
    }

    const std::uint32_t comprimento = ler_comprimento(buffer.data() + posicao);

    // O comprimento é validado **antes** de se olhar para o tipo, e
    // antes de reservar seja o que for. Um comprimento de 3 mil
    // milhões com quatro bytes disponíveis é um `ComprimentoInvalido`
    // imediato, e não uma espera por 3 mil milhões de bytes que nunca
    // chegam.
    if (!comprimento_valido(comprimento)) {
        posicao = inicio;
        return ErroLeitura::ComprimentoInvalido;
    }

    if (disponivel < 4 + static_cast<std::size_t>(comprimento)) {
        posicao = inicio;
        return ErroLeitura::Incompleta;
    }

    posicao += 4;
    saida.tipo = buffer[posicao];
    saida.corpo.assign(buffer.begin() + static_cast<long>(posicao) + 1,
                       buffer.begin() + static_cast<long>(posicao) +
                           static_cast<std::size_t>(comprimento));
    posicao += comprimento;
    return ErroLeitura::Completa;
}

}  // namespace onyx
