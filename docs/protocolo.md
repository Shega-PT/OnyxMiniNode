# O protocolo, como este firmware o implementa

Este documento especifica o que o OnyxMiniNode faz no fio. Ele não
inventa nada: implementa o mesmo protocolo que a implementação de
referência do OnyxChat, em `server/relay_server.py`, e as diferenças
entre os dois são de **capacidade**, não de formato. Onde os bytes
diferem, a tabela abaixo diz qual dos dois está certo — e é sempre o
mesmo.

Índice:

- [Identidade e mailbox](#identidade-e-mailbox)
- [Transporte e enquadramento](#transporte-e-enquadramento)
- [Mensagens](#mensagens)
- [Códigos de erro](#códigos-de-erro)
- [Regras de retenção](#regras-de-retenção)
- [A aritmética dos 320 KB](#a-aritmética-dos-320-kb)
- [O que este firmware não implementa](#o-que-este-firmware-não-implementa)
- [Metadados observáveis](#metadados-observáveis)

---

## Identidade e mailbox

A mailbox de um destinatário é o resultado de:

```
mailbox = SHA-256("ONYX/RELAY/v1" ‖ publica)
```

onde `publica` são os 32 bytes da chave pública Ed25519 do
destinatário, e o prefixo tem exactamente 13 bytes. A concatenação é
crua: não há separador, nem comprimento, nem codificação. O mesmo
hash que o Python calcula com `hashlib.sha256` e que o Rust calcula
com `sha2`, aqui calcula-se com o acelerador de hardware do ESP32.

Isto tem duas propriedades que o resto do desenho depende:

- **é estável** — a mesma chave dá sempre a mesma caixa, o que
  permite a um nó recomeçar sem perder a capacidade de ser
  alcançado;
- **não é reversível** — o nó guarda o hash, nunca a chave. Quem
  intercepte o tráfego do relay vê 32 bytes sem maneira de saber a quem
  pertencem.

⚠ A segunda propriedade é o que torna o relay aceitável mesmo sem
confiança no seu operador. Um relay malicioso **não** consegue ler as
caixas de quem não conhece a chave pública — e essa chave é pública,
o que significa que está ao alcance de quem inspeccionar o tráfego.
O que ele consegue fazer é correlacionar *quando* uma caixa é
depositada e *quando* é esvaziada. Ver
[`ameacas.md`](ameacas.md).

## Transporte e enquadramento

TCP. Cada mensagem é:

```
[comprimento: u32 little-endian][tipo: 1 byte][corpo: comprimento-1 bytes]
```

Três regras, e cada uma delas tem uma razão:

1. **`comprimento` inclui o byte de tipo.** Um pacote de `SUBSCREVER`
   tem 33 bytes de corpo e comprimento `34`. Um pacote vazio tem
   comprimento `1`. Isto é o que distingue «mensagem vazia» de
   «erro de leitura», e um enquadramento em que o comprimento não
   incluísse o tipo não teria forma de codificar o primeiro.
2. **Um comprimento de `0` é erro**, não mensagem. Não existe uma
   mensagem de comprimento zero, e aceitar uma seria aceitar um
   pacote que não diz nada — que é a forma mais barata de gastar o
   heap de alguém.
3. **Um comprimento acima do limite fecha a ligação.** Não devolve
   erro e continua: alocar um buffer do tamanho que o outro lado
   pediu é exactamente o que transforma um bug remoto numa
   recusa de serviço. A ligação fecha-se antes de a alocação
   acontecer.

## Mensagens

### Cliente para o relay

| Tipo | Nome | Corpo |
| --- | --- | --- |
| `0x01` | `SUBSCREVER` | `mailbox(32)` |
| `0x02` | `ENVIAR` | `mailbox(32) ‖ frame_tipo(1) ‖ frame_corpo(N)` |
| `0x03` | `FECHO` | vazio |

### Relay para o cliente

| Tipo | Nome | Corpo |
| --- | --- | --- |
| `0x81` | `ERRO` | `codigo(1) ‖ mensagem UTF-8 (opcional)` |
| `0x83` | `OK` | vazio |
| `0x84` | `CAIXA` | `frame_tipo(1) ‖ frame_corpo(N)` |

O corpo de `ENVIAR` e de `CAIXA` leva o **frame P2P completo**, e não
só o envelope: o byte `frame_tipo` é um tipo do protocolo P2P do
OnyxChat — `0x01` para mensagens, `0x10`, `0x11` ou `0x12` para os
frames do handshake de amizade.

Isto é o que torna o relay **agnóstico**: ele não sabe a diferença
entre uma mensagem e um aperto de mão, porque nunca descola os dois.
Um MiniNode não sabe o que é uma assinatura Ed25519, e não precisa de
saber — a verificação acontece no destinatário, no outro extremo.

`SUBSCREVER` liga a ligação a uma mailbox. Uma ligação só pode estar
numa caixa de cada vez; subscrever outra move-a, e a anterior deixa
de lhe ser entregue.

`ENVIAR` é **fire-and-forget**: se não houver subscritor, o frame
fica pendente até ao prazo de validade. Se houver, é entregue
imediatamente e sai da caixa.

## Códigos de erro

| Código | Nome | Quando |
| --- | --- | --- |
| `0x01` | `PayloadMalformado` | corpo curto para o tipo pedido |
| `0x02` | `MailboxInvalida` | `mailbox` não tem 32 bytes |
| `0x03` | `PayloadGrandeDemais` | o frame excede o limite do nó |
| `0x04` | `SemEspaco` | a caixa tem o máximo de frames pendentes |
| `0x05` | `RateLimit` | demasiados `ENVIAR` na janela |
| `0x06` | `ComandoDesconhecido` | tipo fora de `{0x01, 0x02, 0x03}` |

`SemEspaco` **não** descarta o frame mais antigo para fazer lugar.
Descartar em silêncio parece simpático e é a pior política possível:
quem emitiu não sabe que a mensagem se perdeu, e quem a receberia
nunca vai saber que existiu. Com a caixa cheia, o emissor recebe o
erro e decide o que fazer.

## Regras de retenção

| | |
| --- | --- |
| Prazo de validade de um frame pendente | 300 s |
| Máximo de frames pendentes por mailbox | 12 |
| Máximo `ENVIAR` por ligação | 32 por segundo |
| Máximo de bytes por frame | 4 096 |

A entrega é **única**: quando um `CAIXA` é escrito, o frame sai da
caixa. Não há histórico, não há cópias, e um subscritor que se
desligue não volta a recebê-lo.

## A aritmética dos 320 KB

Esta secção existe porque os números da tabela anterior não são
arbitrários, e porque a diferença para o relay em Python é grande o
bastante para ser notada.

O ESP32-WROOM-32 tem 520 KB de SRAM no datasheet. **Não são 520 KB
disponíveis.** Antes de o firmware arrancar, o driver Wi-Fi e a stack
lwIP já levaram a sua parte. O número que o PlatformIO publica para
esta placa é `maximum_ram_size: 327680` — 320 KB — e é esse o
orçamento com que se planeia.

O consumo deste firmware é:

```
12 frames pendentes  ×  4 096 B  =  49 152 B  (48 KB)
```

E as restantes mailbox activas, no pior caso razoável — digamos oito
destinatários distintos em simultâneo:

```
8 mailboxes  ×  (12 frames × 4 096 B)  =  393 216 B  (384 KB)
```

O segundo número é maior do que a SRAM toda, e é por isso que a
capacidade por mailbox é um limite superior e não uma reserva. Na
prática há menos de oito mailboxes activas ao mesmo tempo, e o que
falta é devolvido ao heap pelo próprio allocator. O que **não** é
aceitável é um desenho em que o pior caso estoura a SRAM: um nó que
se recupera de um pico de memória não é um nó em que se pode confiar
para rendezvous, porque a falha aparece no momento em que a rede está
mais ocupada.

O relay em Python não tem esta restrição — 64 frames de 65 692 B dão
4,2 MB — e por isso é o melhor dos dois para uso Continuo. O MiniNode
é melhor por ser outro motivo: não precisa de um servidor.

Para aumentar a capacidade, `MAX_PENDENTES` sobe em `platformio.ini`.
Para aumentar o tamanho dos frames, `MAX_CORPO` sobe e `MAX_PENDENTES`
tem de descer, para o produto dos dois caber.

## O que este firmware não implementa

Escrito para não haver dúvida:

- **relay em `.onion`** — exigiria resolver o endereço por dentro do
  Tor, e a implementação de referência do OnyxChat também não o faz.
- **encaminhamento entre pares** — não existe. Um frame entra por uma
  ligação e sai por outra, mas não decide para onde vai: vai para a
  mailbox que o emissor nomeou.
- **descoberta** — o MiniNode não sabe que clientes existem, e não
  anuncia nada sobre si. Quem o encontra é configuração.
- **persistência** — o que está numa caixa morre com a corrente se a
  placa perder a alimentação. É uma escolha, não uma limitação
  esquecida: um relay que guarda mensagens em memória não volátil é
  um relay que guarda mensagens.

## Metadados observáveis

O que um operador do relay vê, e o que um cliente vê:

| Observável | Quem vê |
| --- | --- |
| Endereços IP e portas das ligações | o operador do nó |
| Horários de ligação, depósito e Levantamento | o operador do nó |
| A mailbox de 32 bytes de cada depósito | o operador do nó |
| O tamanho de cada frame | o operador do nó |
| O `frame_tipo` (0x01, 0x10, 0x11, 0x12) | o operador do nó |
| Conteúdo do frame | **ninguém** — está cifrado |
| A identidade por trás da mailbox | **ninguém** — o hash não se inverte |
| As chaves K1, K5, K9 | **ninguém** — nunca entram no nó |

O `frame_tipo` é o metadado mais revelador dos oito, e vale a pena
dizer porquê: distingue uma mensagem de um aperto de mão, e os tipos
de handshake são poucos e com forma previsível. Quem analisa o tráfego
descobre quando duas pessoas se tornam amigas, mesmo que não descubra
com quem nem o que dizem.

Isto é uma consequência do desenho do OnyxChat, não uma escolha deste
firmware, e está declarado no modelo de privacidade do repositório-mãe
com a mesma precisão. Dizer que um relay não vê o conteúdo é verdade;
dizer que um relay não revela nada seria mentira.
