# OnyxMiniNode

Um nó de rede para OnyxChat, do tamanho de uma placa de 5 euros.

Um ESP32 DEVKIT V1, com firmware próprio, que fala o protocolo de relay
do OnyxChat e serve de ponto de rendezvous para duas instâncias que
conseguem ligar-se directamente. Só isso: **não é um proxy, não
encaminha nada, não vê o conteúdo de nada, e não tem chaves.**

O objectivo é duplo. O primeiro é prático: quando a ligação directa
entre dois hidden services falha, o OnyxChat precisa de um ponto de
encontro, e esse ponto hoje tem de ser um servidor algures. O segundo
é estrutural: quanto mais pontos de encontro existirem, mais
probabilidade há de dois pares se encontrarem directamente — e a
ligação directa é a que não expõe o IP de ninguém.

---

## Índice

- [O que faz](#o-que-faz)
- [O que não faz](#o-que-não-faz)
- [Porque é que um relay não precisa de ser seguro](#porque-é-que-um-relay-não-precisa-de-seguro)
- [Porque é que este repositório tem uma licença diferente](#porque-é-que-este-repositório-tem-uma-licença-diferente)
- [Hardware](#hardware)
- [Construir e gravar](#construir-e-gravar)
- [Configuração](#configuração)
- [Como se liga a ele](#como-se-liga-a-ele)
- [Limites](#limites)
- [Testes](#testes)

---

## O que faz

Implementa o protocolo descrito em
[`docs/protocolo.md`](docs/protocolo.md), que é o mesmo que a
implementação de referência do OnyxChat em `server/relay_server.py`
fala. Byte a byte:

| | |
| --- | --- |
| Enquadramento | `[comprimento: u32 LE][tipo: 1][corpo]` |
| `0x01` | `SUBSCREVER` — ligar esta ligação a uma mailbox |
| `0x02` | `ENVIAR` — depositar um frame numa mailbox |
| `0x03` | `FECHO` — desligar |
| `0x81` | `ERRO` — código de 1 byte, mensagem opcional |
| `0x83` | `OK` |
| `0x84` | `CAIXA` — entrega de um frame ao subscritor |

A mailbox de um destinatário é `SHA-256("ONYX/RELAY/v1" ‖ chave
pública Ed25519)`. O nó guarda o hash; quem não conhece a chave
pública não consegue depositar na caixa de ninguém.

Modos de rede: **ponto de acesso** por omissão (ligas-te à rede do nó e
tens OnyxChat, sem configurar nada) e **estação** como alternativa
(ligas-te a uma rede existente e o nó liga-se a um router).

## O que não faz

Esta lista é a mais importante do ficheiro, e é escrita antes da
funcionalidade de propósito.

- **Não é relay em `.onion`.** Um relay do OnyxChat abre um TCP
  directo, e um endereço `.onion` não é resolvido por esse caminho. Um
  MiniNode está em `192.168.1.42` ou `nodominio.exemplo`, nunca atrás
  de um hidden service.
- **Não é um nó de encaminhamento.** Não encaminha tráfego entre
  dois pares, não participa em rotas de rede, não descobre quem
  está ligado. É uma caixa com um prazo de validade.
- **Não substitui o Tor.** Continua a expor o IP de quem se liga a
  ele, exactamente como o relay em Python. Ver
  [`docs/ameacas.md`](docs/ameacas.md).
- **Não guarda mensagens.** O que está numa caixa pendente
  desaparece ao fim de 300 segundos, sem excepção e sem destino de
  cópia de segurança.
- **Não fala o handshake de amizade.** Não tem ideia do que uma
  assinatura Ed25519 é, e isso é propositado.

## Porque é que um relay não precisa de ser seguro

Porque **não tem nada que proteger**.

O pipeline criptográfico do OnyxChat está nos *endpoints*. Um relay
nunca vê texto em claro, nunca participa no handshake, nunca toca numa
chave K1, K5 ou K9, e nunca vê uma chave privada. É cego por
desenho, e a consequência é que **um relay comprometido não é um
compromisso** — é, no máximo, uma indisponibilidade. Um atacante que
controle o relay pode descartar e atrasar mensagens e correlacion
horários. Não pode ler uma sequer.

Isto não é uma opinião: é o que o modelo de ameaças do OnyxChat já
diz, na secção que descreve o «relay malicioso».

## Porque é que este repositório tem uma licença diferente

O OnyxChat é **PolyForm Noncommercial**. Este repositório é **MIT**. A
diferença é deliberada, e a razão cabe numa frase:

> **O que tem segredos é PolyForm. O que é transporte cego é livre.**

O PolyForm existe para proteger o que não pode ser desfeito se alguém
fechar o código: a implementação do pipeline criptográfico e o
protocolo. Um firmware de relay não tem nenhum dos dois. Não existe
raiz de confiança dentro dele para proteger.

A consequência prática é de adopção, e é essa a intenção: qualquer
pessoa pode comprar a placa, programme-a, pôr o relay a correr e
contribuir para a rede. Ninguém precisa de pedir licença a ninguém.
Para o OnyxChat, um MiniNode com MIT é **mais** útil do que um com
PolyForm, porque o número de pontos de encontro é o que determina a
probabilidade de dois pares se encontrarem directamente — e é a
ligação directa que protege o endereço IP de cada um.

Quem quiser fechar este firmware, vendê-lo, ou fazer dele um
produto comercial, tem a MIT para o fazer. Não há
`COMMERCIAL-LICENSE.md` aqui, e essa é a diferença.

---

## Hardware

| | |
| --- | --- |
| Placa | ESP32 DEVKIT V1 (`esp32doit-devkit-v1`) |
| Módulo | ESP32-WROOM-32 / ESP32-D0WD |
| Flash | 4 MB |
| SRAM | 520 KB no datasheet; **320 KB utilizáveis** depois do Wi-Fi, do lwIP e do FreeRTOS |
| PSRAM | não |
| Alimentação | 5 V pela porta USB, ou 3,0–3,6 V pelo pino `3V3` |

O número que importa é o dos **320 KB**. É o que a definição da board
do PlatformIO declara, e é o orçamento com que se planeia o heap: os
520 KB do datasheet já includeem o que o Wi-Fi e o lwIP levam antes
do primeiro `setup()`.

⚠ O DEVKIT V1 é vendido em variantes de 30 e de 36 pinos. As duas
funcionam; o que muda é que alguns pinos não estão ligados em
uma delas. Nenhum dos pinos usados por este firmware faz falta
em qualquer das duas.

## Construir e gravar

Requer [PlatformIO](https://platformio.org/) 6.2 ou superior.

```bash
pio run                    # compilar
pio run -t upload          # gravar pela porta USB
pio device monitor         # ver o log
```

A gravação pela porta USB precisa de um cabo que suporte dados.
Um cabo só de carga é a causa mais comum de «a placa não
aparece».

A plataforma e a placa de destino estão fixadas por versão em
`platformio.ini`, e o motivo está escrito no próprio ficheiro: um
firmware que muda de toolchain sem que o número de versão mude é um
firmware que ninguém consegue reproduzir depois de um erro.

## Configuração

Os parâmetros vivem em `platformio.ini`, em `build_flags`, e não em
constantes espalhadas pelo código. Ficarem juntos tem uma razão
prática: são os **mesmos números** que a implementação de referência
do OnyxChat usa, e mudá-los aqui sem os mudar lá produz um nó que
aceita mensagens que o outro lado não espera.

```ini
build_flags =
    -D MININODE_TTL_S=300            ; segundos que um frame espera na caixa
    -D MININODE_MAX_PENDENTES=12     ; frames pendentes por mailbox
    -D MININODE_MAX_CORPO=4096       ; bytes por frame
    -D MININODE_ENVIOS_POR_S=32      ; `ENVIAR` por segundo e por ligação
```

Para experimenting sem tocar no ficheiro versionado:

```bash
cp platformio.ini platformio-local.ini
```

O `platformio-local.ini` fica por cima e **não** é versionado —
ver o `.gitignore`.

## Como se liga a ele

Do lado do OnyxChat, o nó é um endpoint de relay. Em `config.json`, ou
por variável de ambiente:

```bash
export ONYXCHAT_RELAY="192.168.1.42:8788"
```

Ou, se o nó for escutar em todas as interfaces (que é o que uma
placa isolada faz, e que expõe o relay a quem mais estiver na rede):

```bash
onyxchat relay --host 0.0.0.0 --porta 8788
```

Do lado da rede, o nó é um ponto de acesso Wi-Fi. O SSID é
`onyxmininode` e a palavra-passe é **derivada da identidade de
hardware da placa** e impressa no ecrã de arranque — o que significa
que não é uma palavra-passe de fábrica partilhada por todas as
placas, mas que também não é visível a quem não arrancou o aparelho.

## Limites

O nó tem mais limites que o relay em Python, porque tem 320 KB em vez
de um servidor. A aritmética inteira está em
[`docs/protocolo.md`](docs/protocolo.md) §A aritmética dos 320 KB.

| | OnyxChat (Python) | OnyxMiniNode (ESP32) |
| --- | --- | --- |
| Frames pendentes por mailbox | 64 | **12** |
| Tamanho máximo de um frame | 65 692 B | **4 096 B** |
| Pior caso em memória | 4,2 MB | **48 KB** |

Estes não são bugs, e não são um protocolo diferente: são a
capacidade declarada de um nó com pouca memória. O cliente OnyxChat
sabe o que fazer com eles — recebe `0x03 PayloadGrandeDemais` ou
`0x04 SemEspaco` e trata-os como o que são.

A consequência prática é uma só, e vale a pena dizê-la em voz alta:
**uma mensagem com mais de 4 KB não passa por um MiniNode.** Para
texto isso nunca chega a acontecer. Para um anexo, sim.

## Testes

```bash
pio test -e native          # núcleo compilado para host, sem placa
pio test -t devkitv1        # testes no alvo, com a placa ligada
```

O primeiro é o que corre em cada alteração: compila o núcleo do
relay para o computador, sem ESP-IDF e sem placa, e corre-o contra os
mesmos vectores que o OnyxChat usa. É o que torna o protocolo testável
por quem não tem hardware.

O segundo precisa da placa. E há um terceiro que vale mais do que os
dois: o **teste de interoperabilidade**, documentado em
[`docs/testes.md`](docs/testes.md) §Interoperabilidade, em que um
`onyxchatd` real fala com um MiniNode real, nos dois sentidos.

---

## Estrutura

[`Estrutura.txt`](Estrutura.txt) é o mapa do repositório, e
`scripts/verificar_estrutura.py` confirma que o mapa corresponde à
árvore em cada alteração. Um mapa desatualizado é pior do que não ter
mapa, porque permite responder com confiança a uma pergunta cuja
resposta já não é verdadeira.
