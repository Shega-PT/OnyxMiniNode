# OnyxMiniNode — documentação

Três documentos, e a divisão entre eles é por pergunta, não por
tamanho. O que cada um responde está no subtítulo, e a pergunta é
sempre a mesma: *o que é que alguém quer saber aqui?*

| Documento | Responde a | Leitor típico |
| --- | --- | --- |
| [`protocolo.md`](protocolo.md) | «o que é que isto põe no fio, exactamente?» | quem vai implementar ou integrar outro nó |
| [`ameacas.md`](ameacas.md) | «isto é seguro para meter na minha rede?» | quem vai instalar |
| [`testes.md`](testes.md) | «isto funciona mesmo, e onde é que não posso confiar?» | quem vai mexer |

## Leitura por perfil

**Vais usar isto.** `README.md` do repositório, e a partir daí
`protocolo.md` §Limites — é a única secção capaz de te
surpreender: diz o tamanho máximo de uma mensagem que este nó
consegue transportar.

**Vais integrar com o OnyxChat.** `README.md` §Como se liga a ele, e
`protocolo.md` §Mensagens. O cliente do OnyxChat e este firmware partilham
o mesmo protocolo, e a parte que importa é a tabela de tipos e corpos.

**Vais contribuir no firmware.** `Estrutura.txt` para saber o que há,
`protocolo.md` §A aritmética dos 320 KB para saber porque é que os
limites são o que são, e `testes.md` §Onde vivem os testes.

**Vais decidir se isto entra na tua rede.** `ameacas.md`, e sobretudo
a secção A2, que é a única ameaça que este firmware **não** resolve
por si e que depende de como o instalas.

## A regra dos rótulos

Três rótulos, e a definição de cada um está no texto que o usa — não
neste índice, porque um índice que repete as definições divergem.

- **não é** — o que este firmware deliberadamente não faz, e porquê.
  Aparece mais vezes no `README.md` do que em qualquer outro sítio, e
  é a lista mais importante de lá.
- **⚠** — algo que é verdade e é fácil de ler mal. Aparece quatro
  vezes em três documentos, e nas quatro é sobre o mesmo assunto: os
  números da memória não são os do datasheet.
- **não é um substituto** — o que este documento faz *não* substitui
  um documento do repositório-mãe. É o caso de `ameacas.md`, que
  acrescenta ao modelo de ameaças do OnyxChat e não o substitui.

## Relação com o repositório-mãe

O OnyxChat é a fonte de verdade sobre o protocolo. Este repositório
implementa-o, e a documentação aqui descreve a implementação — o que
inclui onde ela diverge, e só pela capacidade.

| Pergunta | Onde se responde |
| --- | --- |
| O que é uma mailbox, e porque este esquema | OnyxChat, `docs/relay.md` |
| Como o cliente usa o relay | OnyxChat, `docs/p2p.md` |
| O que este firmware faz no fio | aqui, [`protocolo.md`](protocolo.md) |
| O que muda por ser uma placa | aqui, [`ameacas.md`](ameacas.md) |

Quando os dois divergirem, quem está certo é o repositório-mãe — e o
divergente é um bug, não uma evolução.
