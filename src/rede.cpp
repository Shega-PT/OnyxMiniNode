// =====================================================================
// onyx/rede.cpp — o ponto de acesso, os sockets e o laço
// ---------------------------------------------------------------------
// Esta é a única unidade de tradução do firmware que não compila no
// computador. Os cabeçalhos do Arduino e do lwIP não existem lá, e a
// alternativa — escrever uma camada de abstracção por cima de tudo —
// custaria mais linhas do que as que aqui estão a ser poupadas.
//
// O que é perder: nenhum teste de host passa por este ficheiro. O que
// fica a ganhar: `frame`, `caixa`, `limites_taxa` e `sessao` —
// quatro das cinco unidades — continuam a ser testáveis sem placa, e
// é aí que mora o protocolo. Ver `docs/testes.md` §Host.
// =====================================================================
#include "onyx/rede.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <lwip/sockets.h>
#include <sys/select.h>
#include <sys/socket.h>

namespace onyx {

namespace {

/// Espera máxima em cada passo do laço.
///
/// Não é um tempo de espera, é um tempo máximo: o laço acorda antes
/// se houver trabalho. É curto porque a única coisa que acontece
/// «a intervalos» é o expurgo das caixas, e quem manda fazer esse
/// trabalho é um depósito que está a encher.
constexpr int TEMPO_PASSO_MS = 20;

/// Quantas ligações aceitas à espera, em `listen`.
///
/// Quatro, e não seis. Um `listen` deeper do que as sessões que
/// existem só gasta memória de kernel: o kernel tem de guardar cada
/// ligação aceite antes de a entregar, e essa memória sai do mesmo
/// orçamento de 320 KB.
constexpr int FILA_ACEITE = 4;

}  // namespace

// =====================================================================
// Deposito
// =====================================================================

Caixa* Deposito::procurar_no_vector(const Mailbox& destino) {
    for (Entrada& e : entradas_) {
        if (e.chave == destino) {
            return &e.caixa;
        }
    }
    return nullptr;
}

Caixa* Deposito::procurar(const Mailbox& destino) {
    return procurar_no_vector(destino);
}

Caixa* Deposito::obter_ou_criar(const Mailbox& destino,
                                std::uint64_t agora_ms) {
    if (Caixa* ja_existe = procurar_no_vector(destino)) {
        return ja_existe;
    }

    // O limite vem de `limites.h` §MAX_MAILBOXES, que é derivado dos
    // números de memória e não escrito à mão.
    //
    // A verificação é `>=` e não `>` porque um vector que cresce por
    // `push_back` pode realocar, e um `&entradas_.back()` obtido
    // antes de um realocamento fica a apontar para a memória antiga.
    // Recusar a entrada **antes** de fazer o `push_back` é o que
    // torna impossível ter um ponteiro pendurado.
    if (entradas_.size() >= MAX_MAILBOXES) {
        return nullptr;
    }

    entradas_.push_back(Entrada{});
    Entrada& nova = entradas_.back();
    nova.chave = destino;
    nova.caixa.ligar(destino, agora_ms);
    return &nova.caixa;
}

// =====================================================================
// Rede
// =====================================================================

std::size_t Rede::ligacoes() const noexcept {
    std::size_t total = 0;
    for (std::size_t i = 0; i < MAX_SESSOES; ++i) {
        if (ocupado_[i]) {
            ++total;
        }
    }
    return total;
}

bool Rede::arrancar_ap(const char* ssid, const char* palavra_passe) {
    // A palavra-passe tem de ter pelo menos oito caracteres. O WPA2
    // aceita menos, e o WPA2 com menos de oito é uma coisa que quase
    // todas as ferramentas de análise encontram depressa. Recusar
    // aqui é melhor do que aceitar e fingir que protege.
    if (ssid == nullptr || ssid[0] == '\0' || palavra_passe == nullptr ||
        std::strlen(palavra_passe) < 8) {
        return false;
    }

    // `WPA2_PSK` explícito. O default do `softAP` mudou entre versões
    // do core, e a diferença entre `WPA` e `WPA2` num relay é a
    // diferença entre proteger e não proteger.
    if (!WiFi.softAP(ssid, palavra_passe, 1, /* oculto */ false,
                    WIFI_AUTH_WPA2_PSK)) {
        return false;
    }

    // Endereço fixado, e não derivado de DHCP. Um cliente que se ligue
    // ao ponto de acesso precisa de saber onde está o relay, e não há
    // ninguém para lho dizer: este relay não tem servidor DHCP. É
    // `192.168.4.1` porque é o que o lwIP dá à interface por omissão,
    // o que faz com que quem já programou uma placa ESP32 já saiba o
    // endereço sem o ler.
    const IPAddress ip(192, 168, 4, 1);
    if (!WiFi.softAPConfig(ip, ip, IPAddress(192, 168, 4, 1))) {
        return false;
    }
    std::snprintf(endereco_, sizeof endereco_, "%u.%u.%u.%u", ip[0], ip[1],
                  ip[2], ip[3]);

    escuta_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (escuta_ < 0) {
        return false;
    }

    int reutilizar = 1;
    ::setsockopt(escuta_, SOL_SOCKET, SO_REUSEADDR, &reutilizar,
                 sizeof reutilizar);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
    addr.sin_port = ::htons(porta_);
    if (::bind(escuta_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
        ::listen(escuta_, FILA_ACEITE) < 0) {
        ::close(escuta_);
        escuta_ = -1;
        return false;
    }

    // Não bloqueante. Sem isto, um `accept` sem clientes presentes
    // segura o laço inteiro, e as seis sessões param de ser servidas
    // todas as vezes que alguém se liga.
    ::fcntl(escuta_, F_SETFL, O_NONBLOCK);
    return true;
}

bool Rede::ligar_a_rede(const char* ssid, const char* palavra_passe) {
    if (ssid == nullptr || ssid[0] == '\0' || palavra_passe == nullptr) {
        return false;
    }
    WiFi.mode(WIFI_STA);
    if (WiFi.begin(ssid, palavra_passe) != WL_CONNECTED) {
        return false;
    }
    const IPAddress ip = WiFi.localIP();
    std::snprintf(endereco_, sizeof endereco_, "%u.%u.%u.%u", ip[0], ip[1],
                  ip[2], ip[3]);
    return true;
}

bool Rede::aceitar() {
    if (escuta_ < 0) {
        return false;
    }

    for (;;) {
        sockaddr_in par{};
        socklen_t tamanho = sizeof par;
        const int novo = ::accept(escuta_, reinterpret_cast<sockaddr*>(&par),
                                  &tamanho);
        if (novo < 0) {
            // EAGAIN é o fim normal da fila. Qualquer outra coisa é
            // um erro de socket, e nesse caso a escuta está perdida.
            return errno != EAGAIN && errno != EWOULDBLOCK;
        }

        std::size_t vaga = MAX_SESSOES;
        for (std::size_t i = 0; i < MAX_SESSOES; ++i) {
            if (!ocupado_[i]) {
                vaga = i;
                break;
            }
        }
        if (vaga == MAX_SESSOES) {
            // Cheio. Fecha-se a ligação imediatamente, sem `ERRO`:
            // não há sessão à qual atribuir a resposta, e uma
            // resposta escrita antes de haver para quem a receba
            // seria escrita na socket errada.
            ::close(novo);
            continue;
        }

        ::fcntl(novo, F_SETFL, O_NONBLOCK);
        sockets_[vaga] = novo;
        ocupado_[vaga] = true;
        entrada_[vaga].clear();
        posicao_[vaga] = 0;
        sessoes_[vaga] = Sessao{};
        sessoes_[vaga].fixar_id(proximo_id_++);
        sessoes_[vaga].ligar_deposito(deposito_);
    }
}

void Rede::acumular(std::size_t indice, const std::uint8_t* dados,
                    std::size_t tamanho) {
    entrada_[indice].insert(entrada_[indice].end(), dados, dados + tamanho);

    // O buffer de entrada de uma sessão cresce até ao tamanho de uma
    // mensagem — 4 KB — e nunca mais. A verificação acontece **depois**
    // de acumular e **antes** de tentar ler, e o que se faz quando
    // passa do limite é fechar a ligação. Não é uma poda: uma
    // mensagem pode ter 4 KB e ser válida, e o `select` só dá dados
    // quando há dados.
    if (entrada_[indice].size() > 1 + MAX_COMPRIMENTO) {
        fechar(indice);
    }
}

void Rede::esvaziar_entrada(std::size_t indice) {
    entrada_[indice].clear();
    posicao_[indice] = 0;
}

bool Rede::ler_de(std::size_t indice) {
    const int s = ::recv(sockets_[indice], scratch_, sizeof scratch_, 0);
    if (s == 0) {
        fechar(indice);
        return false;
    }
    if (s < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        fechar(indice);
        return false;
    }

    acumular(indice, scratch_, static_cast<std::size_t>(s));
    if (!ocupado_[indice]) {
        return false;
    }

    // Percorre o que chegou. Pode haver várias mensagens num `recv` só,
    // e ficar a tratar uma e largar o resto seria perder o resto.
    for (;;) {
        Frame recebida;
        const ErroLeitura r = ler(entrada_[indice], posicao_[indice],
                                  recebida);
        if (r == ErroLeitura::Incompleta) {
            // Sobrou resto sem mensagem completa. Copia-se para o
            // início em vez de fazer `erase` a meio: o `erase` move
            // tudo o que está a seguir, e num buffer de 4 KB isso
            // são quatro quilobytes copiados por pacote.
            entrada_[indice].erase(entrada_[indice].begin(),
                                   entrada_[indice].begin() +
                                       static_cast<long>(posicao_[indice]));
            posicao_[indice] = 0;
            return true;
        }
        if (r == ErroLeitura::ComprimentoInvalido) {
            // Um pacote que declara um comprimento impossível é a
            // forma mais barata de tentar gastar a memória de alguém.
            // Fecha-se a ligação sem responder.
            fechar(indice);
            return false;
        }

        const bool continua =
            sessoes_[indice].processar(recebida, ::millis());
        if (!continua) {
            // O cliente pediu para fechar. A resposta ficou na fila de
            // saída e vai ser escrita no fim do passo — fechar o
            // socket agora deitava fora o acuse de recibo.
            if (sessoes_[indice].a_fechar()) {
                esvaziar_entrada(indice);
                return true;  // escreve-se a saída, fecha-se a seguir
            }
            fechar(indice);
            return false;
        }
    }
}

bool Rede::escrever_para(std::size_t indice) {
    const std::vector<std::uint8_t>& por_escrever = sessoes_[indice].saida();
    if (por_escrever.empty()) {
        return true;
    }

    const int n = ::send(sockets_[indice], por_escrever.data(),
                         por_escrever.size(), MSG_NOSIGNAL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // o socket está cheio; tenta-se no próximo passo
        }
        return false;
    }

    // Escrita parcial fica na fila. Escrever e esvaziar na mesma seria
    // perder o que não cabeu, e um socket lento é uma coisa normal,
    // não uma excepção.
    if (static_cast<std::size_t>(n) == por_escrever.size()) {
        sessoes_[indice].consumir_saida();
    }
    return true;
}

void Rede::fechar(std::size_t indice) noexcept {
    if (indice >= MAX_SESSOES || !ocupado_[indice]) {
        return;
    }
    ::close(sockets_[indice]);
    sockets_[indice] = -1;
    ocupado_[indice] = false;
    esvaziar_entrada(indice);
    // A sessão e o socket ficam no sítio e ficam marcados como vagos.
    // Não há `erase`: os índices de `ocupado_` são usados pelo `select`
    // do passo inteiro, e mover o array a meio do laço transformaria
    // um índice válido num índice errado.
}

bool Rede::passo(std::uint64_t agora_ms) {
    if (escuta_ < 0 && !ligado()) {
        return false;
    }

    fd_set leitura;
    fd_set escrita;
    FD_ZERO(&leitura);
    FD_SET(escuta_, &leitura);
    FD_ZERO(&escrita);

    int maximo = escuta_;
    for (std::size_t i = 0; i < MAX_SESSOES; ++i) {
        if (!ocupado_[i]) {
            continue;
        }
        FD_SET(sockets_[i], &leitura);
        if (!sessoes_[i].saida().empty()) {
            FD_SET(sockets_[i], &escrita);
        }
        if (sockets_[i] > maximo) {
            maximo = sockets_[i];
        }
    }

    timeval espera{};
    espera.tv_sec = 0;
    espera.tv_usec = TEMPO_PASSO_MS * 1000;

    const int prontos = ::select(maximo + 1, &leitura, &escrita, nullptr,
                                 &espera);
    if (prontos < 0) {
        if (errno == EINTR) {
            return true;  // um sinal interrupted; o passo não falhou
        }
        return false;
    }

    if (FD_ISSET(escuta_, &leitura)) {
        aceitar();
    }

    // A iteração vai pelo fim para o princípio. `fechar` não move
    // ninguém, e ler pode fechar — o que torna `ocupado_[i]` falso
    // para um índice já visitado. Ir para trás e testar outra vez é
    // o que evita o salto.
    for (std::size_t i = MAX_SESSOES; i-- > 0;) {
        if (!ocupado_[i]) {
            continue;
        }

        if (FD_ISSET(sockets_[i], &escrita)) {
            if (!escrever_para(i)) {
                fechar(i);
                continue;
            }
        }

        if (FD_ISSET(sockets_[i], &leitura)) {
            if (!ler_de(i)) {
                continue;
            }
        }

        // Um `CAIXA` depositado por **outra** sessão só aparece aqui:
        // a sessão que depositou entrega o que está na sua caixa, e
        // a que subscreveu é outra. É o caminho normal de uma mensagem
        // entre dois clientes do mesmo MiniNode.
        if (ocupado_[i]) {
            sessoes_[i].puxar(agora_ms);
        }

        if (ocupado_[i] && sessoes_[i].a_fechar()) {
            if (sessoes_[i].saida().empty()) {
                fechar(i);
            }
        }
    }

    (void)agora_ms;
    return true;
}

}  // namespace onyx
