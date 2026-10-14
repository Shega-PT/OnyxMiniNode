# Testes

Três camadas, e a ordem é deliberada: a primeira é a que corre em cada
alteração, a segunda precisa de hardware, e a terceira precisa das duas
coisas ao mesmo tempo.

| Camada | Onde corre | Precisa de placa | O que prova |
| --- | --- | --- | --- |
| Host | `pio test -e native` | não | o protocolo e a lógica de retenção |
| Alvo | `pio test -t devkitv1` | **sim** | que a memória cabe e o tempo chega |
| Interoperabilidade | manual | **sim** | que o OnyxChat real fala com este firmware |

Estado actual: **cinquenta e cinco casos de host, todos a passar**, e
o firmware a compilar para a placa. A camada de interoperação ainda
não foi corrida com uma placa ligada, e `README.md` §Limites diz qual é
o caso que ela fecha.

## Os seis ficheiros de `test/`, e porque é que um deles é o `main`

`test/` é uma suite só, e não seis. O PlatformIO compila todos os
`.cpp` de um directório de `test/` para um executável, e com um `main`
em cada ficheiro o linkador dá «definição múltipla de `main»» — uma
mensagem que não aponta para a causa.

A solução é `test/test_main.cpp`, que tem o `main` e chama a função de
cada ficheiro. E `test_build_src = true` no ambiente de host, que sem
ele dá dezenas de «referência não definida» — outra mensagem que não
aponta para a causa. As duas coisas estão escritas em `platformio.ini`.

## Host

```bash
pio test -e native
```

O núcleo do relay — `frame`, `caixa`, `limites`, `sessao`,
`mailbox` — não depende de ESP-IDF, de FreeRTOS nem de sockets. Compila
para o computador e corre os mesmos casos que correriam na placa.

Isto não é conveniência: é o que torna o protocolo verificável por
quem não tem hardware. Uma placa comprada em qualquer loja de
electrónica é uma barreira baixa, mas não é zero — e a barreira mais
alta nunca é a placa, é a de não saber se o que se alterou funciona.

Os vectores do OnyxChat (`tests/vectors/`) **não são consumidos
ainda**. O que está feito é a derivação da mailbox verificada
byte a byte quanto à estrutura, e o SHA-256 em si verificado no alvo.

Dizer o que está feito é mais útil do que dizer o que se pretende
fazer: um plano de testes que se apresenta como cumprido é a forma mais
barata de um projecto não saber o que sabe.

O que a camada de host **não** apanha, e é preciso dizer:

- **excesso de heap.** O alocador da placa não é o do computador, e o
  limite de 320 KB não existe no host;
- **tempo.** Uma janela de taxa de 32 por segundo é um número; se o
  alvo não consegue despachar 32 `ENVIAR` por segundo, o teste de
  host passa e o utilizador não consegue enviar;
- **comportamento sob Wi-Fi intermitente.** Nada disto aparece sem a
  placa e sem a rede a falhar.

## Alvo

```bash
pio run -t upload
pio test -t devkitv1
```

Os mesmos casos de protocolo, mais os que só têm sentido no alvo:

- **orçamento de memória** — encher uma mailbox com 12 frames de 4 096
  bytes e confirmar que a alocação tem êxito;
- **caixa cheia** — o 13.º `ENVIAR` devolve `0x04`, e os 12 anteriores
  continuam lá;
- **expiração** — um frame que ninguém levanta expira ao fim do prazo
  e a memória é devolvida;
- **reinício** — cortar a alimentação a meio e confirmar que o nó volta
  a responder sem intervenção;
- **janela de taxa** — 33 `ENVIAR` seguidos dão `0x05` no último.

Este último é o mais subjectivo. O alvo tem de conseguir **medir** a
janela por si próprio, e não confiar num temporizador de host: a
diferença de relógio entre o teste e o alvo é suficiente para produzir
um falso-verde.

## Interoperabilidade

É o teste que vale mais do que os outros dois juntos, e é o que
justifica todo o resto.

O que prova: que um `onyxchatd` real, com o cliente real, com as
chaves reais, consegue subscritar uma mailbox, enviar, receber e
terminar — contra este firmware, e não contra `relay_server.py`.

```bash
# 1. o nó, com a placa ligada
pio run -t upload
# a placa imprime o SSID e a palavra-passe no ecrã de arranque

# 2. o daemon, apontando ao nó
ONYXCHAT_RELAY="192.168.1.42:8788" onyxchatd --tor nenhum

# 3. duas instâncias em modo relay, uma contra a outra através do nó
```

Os mesmos casos que o OnyxChat já corre contra o relay em Python, e
que têm de correr aqui:

| | |
| --- | --- |
| `SUBSCREVER` seguido de `ENVIAR` para a caixa de alguém | o frame chega |
| `ENVIAR` para uma caixa sem subscritor | fica pendente, entrega-se quando alguém se liga |
| `ENVIAR` depois de `SUBSCREVER` na mesma ligação | entrega directa, sem pendência |
| Re-subscrever noutra mailbox | a anterior deixa de entregar |
| `FECHO` | a ligação fecha e a mailbox é libertada |
| 33 `ENVIAR` seguidos | `0x05 RateLimit` no último |
| 13 frames numa mailbox cheia | `0x04 SemEspaco`, e nada se perde em silêncio |
| Um frame de 5 000 bytes | `0x03 PayloadGrandeDemais` |

⚠ Os vectores do OnyxChat usam mensagens grandes. Com
`MININODE_MAX_CORPO` a 4 096, alguns deles **vão falhar com
`0x03`** — e isso é o comportamento correcto, não uma falha. O que
não pode acontecer é o daemon tratar esse `0x03` como uma falha de
ligação e desistir; é esse o teste que vale a pena correr.

## Onde vivem os testes

- `test/` — ficheiros de teste, um por unidade. O nome tem de
  começar por `test`, que é o que o PlatformIO reconhece e o que o
  `Estrutura.txt` conta.
- `include/onyx/*.h` — as declarações que os testes usam. Os
  cabeçalhos são a API; os testes compilam contra a mesma API que o
  firmware, e não contra uma cópia.
- `src/*.cpp` — o firmware. Não tem nenhum `#ifdef` que exista apenas
  para os testes, e não há caminho de código que só o teste use. Um
  código que só existe nos testes não está a ser testado — está a ser
  escrito duas vezes.

## O que a camada de host **não** apanha

Escrito antes de `docs/ameacas.md` §O que este documento não cobre,
porque aqui a lista é mais curta e a razão é mais específica.

**`derivar_mailbox` não é testada em host.** Precisa de mbedTLS, e o
mbedTLS não está no caminho de _include_ de um compilador de estação
de trabalho. O guard `ONYX_HASH_VIA_MBEDTLS` em `mailbox.h` é o que a
exclui, e o que fica coberto é a parte que um programador estraga sem
dar por isso: os treze bytes do domínio e a ordem da concatenação.
`test_mailbox.cpp` testa os dois.

O `static_assert` desse ficheiro encontrou o número do domínio
errado — estava em 14 e `ONYX/RELAY/v1` tem treze bytes. É o tipo de
erro que um teste escrito à mão com o mesmo número errado não apanha.

**`rede.cpp` e `main.cpp` não são compiladas em host.** Falam com o
Arduino e com o lwIP. É uma consequência, não uma escolha.

**Excesso de heap e tempo de resposta** — ver §Host.

## O que não é testado, e porque está aqui

Escrito para que a lista não seja uma surpresa quando alguém a ler:

- **o driver de Wi-Fi** não é nosso, e a sua fiabilidade não é uma
  propriedade deste firmware;
- **a WPA2** é do ESP-IDF;
- **o entreferimento em banda de 2,4 GHz** é do meio, e nenhum teste
  unitário o apanha;
- **o comportamento depois de uma descarga eléctrica**, **de um
  sobreaquecimento**, e **de uma queda de tensão** são, todos eles,
 conjecturas razoáveis em vez de medições.

Um documento de testes que só lista o que passa é um folheto. Este
lista os dois, e a segunda lista é a que interessa.