// =====================================================================
// onyx/sessao.cpp — a máquina de estados de uma ligação
// ---------------------------------------------------------------------
// A parte do firmware onde um erro é mais caro do que parece: a maior
// parte das decisões que este ficheiro toma são sobre o que fazer a
// seguir depois de uma coisa errada, e quase todas as respostas são
// «responde com um erro e continua».
// =====================================================================
#include "onyx/sessao.h"

#include <cstring>

namespace onyx {

namespace {

/// ## Onde é que o frame começa
///
/// O corpo de `ENVIAR` é `mailbox(32) ‖ frame_tipo(1) ‖ frame(N)`, e
/// o `frame_tipo` está portanto no índice **32** — não no índice 1.
///
/// A confusão é o erro mais provável deste ficheiro e o mais difícil
/// de ver, porque o índice 1 também está dentro do vector e devolve um
/// byte. O sintoma não é um crash: é o relay a entregar à
/// destinatário o `frame_tipo` errado, com o tamanho certo, e o
/// cliente a descartá-lo por ser um tipo que não reconhece. O teste
/// `test_sessao.cpp` §o `CAIXA` chega com o tipo certo é o que fecha
/// isto, e ele falha se o índice mudar.
///
/// `TAM_ENVIAR_CABECALHO` é a constante, e não `32`: um número escrito
/// à mão ao lado do nome da coisa que mede é um número que diverge.
inline std::uint8_t tipo_do_frame(const std::vector<std::uint8_t>& corpo) {
    return corpo[TAM_MAILBOX];
}

/// O frame propriamente dito, sem a mailbox e sem o byte do tipo.
///
/// `nullptr` quando o corpo é demasiado curto. Quem chama tem de tratar
/// isso **antes** de usar, e trata.
inline const std::uint8_t* frame_corpo(
    const std::vector<std::uint8_t>& corpo) noexcept {
    return corpo.size() > TAM_ENVIAR_CABECALHO
               ? corpo.data() + TAM_ENVIAR_CABECALHO
               : nullptr;
}

}  // namespace

void Sessao::responder_erro(std::uint8_t codigo, const char* mensagem) {
    std::vector<std::uint8_t> quadro;
    if (escrever_erro(quadro, codigo, mensagem)) {
        saida_.insert(saida_.end(), quadro.begin(), quadro.end());
    }
}

void Sessao::responder_ok() {
    std::vector<std::uint8_t> quadro;
    if (escrever_ok(quadro)) {
        saida_.insert(saida_.end(), quadro.begin(), quadro.end());
    }
}

bool Sessao::subscrever(const Frame& recebida, std::uint64_t agora_ms) {
    // O corpo tem de ter **exactamente** 32 bytes. Nem mais nem menos:
    // uma mailbox de 33 bytes é uma caixa diferente, e um cliente que
    // mandasse uma a pensar que mandou a outra receberia um `OK` e
    // nunca uma mensagem. Por isso a comparação é de igualdade e não
    // um `>= TAM_MAILBOX`.
    if (recebida.corpo.size() != TAM_MAILBOX) {
        responder_erro(ERRO_MAILBOX_INVALIDA, "a mailbox tem de ter 32 bytes");
        return true;
    }
    if (deposito_ == nullptr) {
        responder_erro(ERRO_COMANDO_DESCONHECIDO, "sem deposito");
        return true;
    }

    Mailbox destino{};
    std::memcpy(destino.data(), recebida.corpo.data(), TAM_MAILBOX);

    Caixa* alvo = deposito_->obter_ou_criar(destino, agora_ms);
    if (alvo == nullptr) {
        // Não há espaço para mais uma caixa. É o caso em que fechar a
        // ligação é o que está certo — ao contrário de todo o resto
        // deste ficheiro — porque não é o cliente que fez algo errado:
        // é o nó que chegou ao limite. E fechar é honesto: o cliente
        // recebe uma falha de rede e sabe que pode tentar outro relay.
        responder_erro(ERRO_SEM_ESPACO, "sem espaco para mais caixas");
        return false;
    }

    // Uma sessão subscreve **uma** mailbox. Subscrever outra move-a,
    // e a anterior deixa de lhe entregar. É o que `docs/protocolo.md`
    // §Mensagens chama re-subscrição, e é o que permite a um cliente
    // mudar de destinatário sem reabrir a ligação.
    alvo->ligar(destino, agora_ms);

    // A sessão guarda uma **referência** à caixa, não uma cópia.
    //
    // Uma cópia seria um bug silencioso de primeira classe: esgota-se
    // no instante da subscrição e nunca mais recebe nada, porque o
    // depósito escreve sempre na caixa do depósito. O sintoma seria
    // um cliente ligado à sua própria mailbox a receber `OK` em cada
    // `ENVIAR` e nunca um `CAIXA` — e como o cliente do OnyxChat
    // subscreve uma mailbox por ligação, um teste feito só com ele
    // nunca encontraria o caminho.
    caixa_ = alvo;

    subscrita_ = true;
    responder_ok();
    entregar(agora_ms);
    return true;
}

bool Sessao::enviar(const Frame& recebida, std::uint64_t agora_ms) {
    // `mailbox(32) ‖ frame_tipo(1) ‖ frame_corpo(N)`, com N ≥ 1.
    if (recebida.corpo.size() <= TAM_ENVIAR_CABECALHO) {
        responder_erro(ERRO_PAYLOAD_MALFORMADO, "corpo curto para ENVIAR");
        return true;
    }

    // A taxa é cobrada **antes** do depósito, e não depois. A ordem
    // é a que impede que um cliente que já esgotou a janela consiga
    // esvaziar uma mailbox alheia enquanto recebe `RateLimit` — o que
    // seria usar o relay como trabalho gratuito, e o relay é de quem
    // o instalou.
    if (!taxa_.registar(agora_ms)) {
        responder_erro(ERRO_RATE_LIMIT, "demasiadas mensagens");
        return true;
    }

    if (deposito_ == nullptr) {
        responder_erro(ERRO_COMANDO_DESCONHECIDO, "sem deposito");
        return true;
    }

    Mailbox destino{};
    std::memcpy(destino.data(), recebida.corpo.data(), TAM_MAILBOX);

    Caixa* alvo = deposito_->procurar(destino);
    if (alvo == nullptr) {
        // Não há caixa para este destino, o que quer dizer que
        // ninguém se subscreveu. Criar uma agora seria um `map` que
        // cresce sem limite, alimentado por quem quer que seja, para
        // endereços que nunca são subscritos. Não se cria: responde-se
        // `MailboxInvalida` e a ligação continua.
        //
        // O nome do código é o que o protocolo tem. Não existe um
        // código «ninguém está à espera», e acrescentar um seria uma
        // extensão que o cliente não conhece — e um cliente que
        // recebe um código que não conhece trata-o como erro
        // genérico e fecha a ligação.
        responder_erro(ERRO_MAILBOX_INVALIDA,
                       "ninguem subscrito a essa caixa");
        return true;
    }

    const std::uint8_t* corpo = frame_corpo(recebida.corpo);
    const std::size_t tamanho = recebida.corpo.size() - TAM_ENVIAR_CABECALHO;

    if (!alvo->depositar(tipo_do_frame(recebida.corpo), corpo, tamanho,
                         agora_ms)) {
        // Cheia, ou frame grande demais. Os dois casos devolvem `false`
        // e a sessão tem de saber qual — mas ambos resultam em
        // «não foi depositado», e o emissor tem de poder distinguir
        // «a minha mensagem era grande demais» de «não há espaço».
        responder_erro(tamanho > MAX_CORPO ? ERRO_PAYLOAD_GRANDE_DEMAIS
                                           : ERRO_SEM_ESPACO,
                       tamanho > MAX_CORPO ? "frame grande demais" : "caixa cheia");
        return true;
    }

    // O depósito foi aceite. Agora é que a sessão de origem recebe
    // uma cópia, se for a mesma pessoa.
    if (caixa_ != nullptr && destino == caixa_->destino()) {
        // Depósito para si próprio: o `CAIXA` sai já, sem passar pela
        // caixa. É o caminho que um cliente em modo relay usa quando
        // está a falar com ele próprio, e é a razão de a taxa estar
        // antes disto — caso contrário seria uma forma de entregar sem
        // limite.
        entregar(agora_ms);
    }

    responder_ok();
    return true;
}

void Sessao::entregar(std::uint64_t agora_ms) {
    if (caixa_ == nullptr) {
        return;
    }
    std::vector<std::uint8_t> corpo;
    std::uint8_t tipo = 0;
    while (caixa_->retirar(corpo, tipo, agora_ms)) {
        std::vector<std::uint8_t> quadro;
        if (!escrever_caixa(quadro, tipo, corpo.data(), corpo.size())) {
            // O `CAIXA` não coube no limite de escrita. O que já foi
            // retirado da caixa está aqui e vai sair; o que ficou
            // voltou a entrar? Não — `retirar` remove. Perder um
            // frame porque o relay não o consegue escrever é a
            // consequência de ter uma capacidade, e é melhor perdê-lo
            // aqui, onde o emissor sabe que houve um `OK` mas o
            // destinatário não o recebeu, do que descartá-lo em
            // silêncio mais tarde.
            break;
        }
        saida_.insert(saida_.end(), quadro.begin(), quadro.end());
        corpo.clear();
    }
}

void Sessao::puxar(std::uint64_t agora_ms) {
    if (caixa_ == nullptr || !caixa_->subscrita()) {
        return;
    }
    entregar(agora_ms);
}

bool Sessao::processar(const Frame& recebida, std::uint64_t agora_ms) {
    switch (recebida.tipo) {
        case MSG_SUBSCREVER:
            return subscrever(recebida, agora_ms);

        case MSG_ENVIAR:
            return enviar(recebida, agora_ms);

        case MSG_FECHO:
            // O cliente disse que acabou. Responde-se `OK` e fecha-se
            // depois de a resposta sair, e não antes: fechar sem
            // responder dá ao cliente um corte de rede em vez de um
            // acuse de recibo, e a diferença é a que faz o cliente
            // tentar de novo.
            responder_ok();
            fechar_ = true;
            return false;

        default:
            // Um tipo desconhecido é `ComandoDesconhecido`, e a
            // ligação **fica aberta**. Fechar por aqui seria punish
            // um cliente de uma versão futura por tentar carregar
            // numa mensagem nova.
            responder_erro(ERRO_COMANDO_DESCONHECIDO, "tipo desconhecido");
            return true;
    }
}

}  // namespace onyx
