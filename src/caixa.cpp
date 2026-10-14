// =====================================================================
// onyx/caixa.cpp — as caixas, e o que nelas fica
// ---------------------------------------------------------------------
// Três funções. `ligar`, `depositar` e `retirar`. As três recebem o
// instante actual em milissegundos, e nenhuma chama a uma relógio:
// isso é o que torna cada uma delas um caso de teste que acaba.
// =====================================================================
#include "onyx/caixa.h"

namespace onyx {

void Caixa::ligar(const Mailbox& destino, std::uint64_t agora_ms) {
    destino_ = destino;
    ligada_ = true;
    // O expurgo acontece **aqui** e não em `depositar`. É o mesmo
    // efeito com outra ordem: se uma caixa cheia de frames expirados
    // receber um depósito, `depositar` tem de devolver `SemEspaco` a
    // um emissor cuja caixa está, na verdade, livre.
    expurgar(agora_ms);
}

void Caixa::expurgar(std::uint64_t agora_ms) noexcept {
    std::size_t quantos = 0;
    while (quantos < itens_.size() &&
           itens_[quantos].expira_ms <= agora_ms) {
        ++quantos;
    }
    if (quantos == 0) {
        return;
    }

    // `erase` com begin+quantos é O(n) no que fica, e o que fica é
    // o que ainda vai ser entregue. Numa caixa de doze frames, isso é
    // doze operações de mover bytes, e acontece uma vez por subscrição
    // — não uma vez por depósito.
    itens_.erase(itens_.begin(),
                 itens_.begin() + static_cast<long>(quantos));

    // Os corpos apagados saem de cena com o vector. A memória que
    // eles ocupavam vai para a arena do alocador, não é devolvida ao
    // sistema, e é por isso que `docs/protocolo.md` §A aritmética dos
    // 320 KB fala em **pico** e não em média: uma caixa que enche e
    // esvazia dez vezes tem o pico de uma caixa cheia, sempre.
}

bool Caixa::depositar(std::uint8_t frame_tipo, const std::uint8_t* corpo,
                      std::size_t tamanho, std::uint64_t agora_ms) {
    if (tamanho > MAX_CORPO) {
        return false;
    }

    expurgar(agora_ms);

    if (itens_.size() >= MININODE_MAX_PENDENTES) {
        return false;
    }

    Pendente p;
    p.frame_tipo = frame_tipo;
    p.expira_ms = expira(agora_ms);
    p.corpo.assign(corpo, corpo + tamanho);

    // `push_back` pode lançar `bad_alloc`, e este firmware compila
    // sem excepções. A consequência seria um `abort()`. É aceitável
    // porque é o mesmo que aconteceria ao encher a SRAM: uma caixa
    // que precisa de 49 KB e não os tem é uma placa sem memória, e
    // não um caso de teste. O que **não** é aceitável é crescer a
    // lista sem limite para «não falhar» — o `MAX_PENDENTES` acima
    // é o limite que impede que isso aconteça.
    itens_.push_back(std::move(p));
    return true;
}

bool Caixa::retirar(std::vector<std::uint8_t>& saida, std::uint8_t& tipo,
                    std::uint64_t agora_ms) {
    expurgar(agora_ms);
    if (itens_.empty()) {
        return false;
    }
    tipo = itens_.front().frame_tipo;
    saida = std::move(itens_.front().corpo);
    itens_.erase(itens_.begin());
    return true;
}

std::size_t Caixa::bytes_pendentes() const noexcept {
    std::size_t total = 0;
    for (const Pendente& p : itens_) {
        total += p.corpo.size();
    }
    return total;
}

}  // namespace onyx
