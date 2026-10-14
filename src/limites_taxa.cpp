// =====================================================================
// onyx/limites_taxa.cpp — a janela deslizante
// ---------------------------------------------------------------------
// Uma função. `registar`. Tudo o que há a saber sobre o assunto está
// escrito no cabeçalho, e repeti-lo aqui seria a forma mais barata de
// criar duas fontes de verdade.
// =====================================================================
#include "onyx/limites_taxa.h"

namespace onyx {

bool JanelaTaxa::registar(std::uint64_t agora_ms) {
    // A janela abre no primeiro envio e só se fecha ao fim de um
    // segundo. Abrir no primeiro e não no início do segundo é o que
    // torna a janela deslizante: o cliente que envia às 999 ms leva
    // consigo um segundo inteiro de histórico, e não uma fracção do
    // segundo de relógio.
    if (!aberta_) {
        aberta_ = true;
        instante_inicio_ = agora_ms;
        usados_ = 0;
    }

    // Relógio a andar para trás é um caso que existe — `millis()` no
    // Arduino é um contador de 32 bits que dá a volta todos os 49,7
    // dias, e um NTP a corrigir pode recuar. Tratar um recuo como
    // «nova janela» reinicia a quota, e um cliente que fica a recuar
    // o relógio tem quota infinita. O que se faz é manter a janela
    // aberta e não contar: é a resposta conservadora, e a que nunca
    // dá mais a alguém do que a que tinha antes.
    if (agora_ms < instante_inicio_) {
        return usados_ < LIMITE;
    }

    if (agora_ms - instante_inicio_ >= JANELA_MS) {
        instante_inicio_ = agora_ms;
        usados_ = 0;
    }

    if (usados_ >= LIMITE) {
        return false;
    }
    ++usados_;
    return true;
}

}  // namespace onyx
