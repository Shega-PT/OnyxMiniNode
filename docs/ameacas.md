# O que um MiniNode muda no modelo de ameaças

Um relay do OnyxChat é um programa que corre num servidor. Um MiniNode
é um programa que corre numa placa que **qualquer pessoa pode
comprar, levar para casa e pôr na rede de café**. A diferença não é
de grau, é de categoria, e este documento é sobre ela.

O que **não** muda está no repositório-mãe e não é repetido aqui: um
relay é cego por desenho, não vê texto em claro, não tem chaves e não
participa no handshake de amizade. Um MiniNode é exactamente um relay
nesse aspecto, e é por isso que a MIT é defensável — ver
[`../README.md`](../README.md) §Porque é que este repositório tem uma
licença diferente.

O que muda são quatro coisas.

---

## A1 — Propriedade física

Uma placa que está no escritório de alguém é, no fundo, um
computador sem segredos. Não é pior do que qualquer outro computador
— é o que é.

O que um MiniNode **não** guarda torna isto quase irrelevante:

- não guarda chaves privadas;
- não guarda mensagens (a memória é volátil);
- não guarda histórico: um frame entregue sai da caixa, um frame por
  expirar é descartado.

O pior caso de um MiniNode confiscated é que alguém leva uma caixa
com cifras de Ethernet e o SSID e a palavra-passe. Isso serve para
ligar-se à rede de acesso do aparelho e tentar a mesma coisa de novo —
e é por isso que o acesso sem palavra-passe é um erro de configuração
que este firmware não permite. Ver §Mitigações.

## A2 — Um MiniNode partilhado por muitas pessoas

Esta é a ameaça **nova**, e é a que a licença MIT torna provável. Um
relay em Python é tipicamente operated por quem constrói o OnyxChat, ou
por quem se inspiresse nele. Um MiniNode com licença que não pede
permissão a ninguém vai acabar, se funcionar, na mão de muita gente —
e o caso menarik é **vários utilizadores a usar o mesmo MiniNode
público**.

O que isso piora, com precisão:

| | MiniNode privado | MiniNode público partilhado |
| --- | --- | --- |
| Endereço IP exposto | o de quem se liga | o de quem se liga |
| Ponto de correlação | um por utilizador | **o mesmo para todos** |
| Quem vê os horários | quem tem a placa | quem tem a placa, para todos |
| resistente a quebras | uma por instância | **uma para todas** |

O ponto do meio é o que dói: com um MiniNode partilhado por vinte
pessoas, o operador do nó vê vinte pares de endereços e horários que
sabem-se casados, e não sabe de antemão que são amigos. Isso é
correlação a um nível que o desenho nunca teve como objectivo.

**Isto não é um defeito do firmware.** É uma consequência de
implantar uma REDE de relays, e o OnyxChat já declara a mesma
limitação para o relay em Python: ligar-se a um relay alcançável é
ligar-se em claro, e o IP fica visível. O MiniNode não piora o
problema, **escala-o**.

A mitigação é de implantação, não de código: um MiniNode é para a rede
de casa, do escritório, ou de quem vive junto. Não é para a internet
pública. Dizer isto num `README.md` é mais útil do que tentar
impedir em software, porque a utilização correcta é uma decisão de
quem instala.

## A3 — Intercepção sem fios

O tráfego entre o cliente e o MiniNode passa por Wi-Fi. Dentro de uma
rede com palavra-passe, o WPA2 protege-o. Numa rede aberta — cafés,
hotéis, airports — não há nada que impeça alguém na mesma sala de ver
os frames a passar.

O que o atacante apanha nessa situação:

- **nada em claro** — os frames já vão cifrados pelo pipeline K1→K9
  do OnyxChat, antes de chegarem ao relay;
- os endereços IP e as mailbox de 32 bytes;
- os horários e os tamanhos.

Ou seja, a intercepção sem fios a um relay **não** enfraquece o
OnyxChat: não havia conteúdo em claro para roubar. O que se obtém é o
mesmo que o operador do relay obtém. É um gleaning de metadados que
já era declarado.

Isto é o mesmo que acontece a qualquer utilizador de Tor num café, e
a resposta não é técnica — é não usar o cliente onde a rede não é
confiável. O que este firmware pode fazer, e faz, é ligar-se como
ponto de acesso com WPA2 por omissão, para que quem o usa numa rede
alheia não fique exposto à rede alheia, e sim só a ele.

## A4 — A superfície de ataque do firmware

O firmware é C++ sobre FreeRTOS e lwIP, compilado com o core
Arduino-ESP32, que é construído sobre o **ESP-IDF 4.4.7**. Isto é uma
base de código de 2021, e é a escolha feita em
`platformio.ini` §Porque é que o framework é `arduino` e não
`espidf`, que é uma escolha razoada para os fins deste projecto e
não é a melhor em termos absolutos.

O que não há aqui, e importa dizer:

- **nenhuma superfície de rede exposta** além de um servidor TCP numa
  porta, e o `frame_tipo` nunca é interpretado;
- **nenhuma criptografia implementada à mão** — o SHA-256 vem do
  mbedTLS, que o próprio ESP-IDF audita;
- **nenhum parser de estrutura variável** — os quatro tamanhos fixos
  do protocolo são validados antes de qualquer acesso a memória.

⚠ Isto não é uma auditoria. É uma lista do que **não** está exposto,
escrita para quem decide se esta peça entra na sua rede. Ninguém fez
uma revisão de segurança do firmware, e um documento que deixasse
sugerir o contrário estaria a mentir.

---

## Mitigações, e o que cada uma custa

| | Custo |
| --- | --- |
| Ponto de acesso com WPA2, não aberto | quem se liga tem de saber a palavra-passe |
| `SUBSCREVER` exige apresentar o `frame_tipo` certo, sem excepções | nenhum |
| Nada de registo de conteúdo, só tipos e tamanhos | ninguém pode depurar conteúdo — e ninguém precisa |
| `SemEspaco` nunca descarta em silêncio | o emissor tem de tratar o erro |
| Limite de taxa por ligação | 32 `ENVIAR` por segundo |
| Sem persistência | uma queda de corrente perde o que estava pendente |
| O que **não** há mitigação | um MiniNode na internet pública partilhado por muita gente. Ver §A2 |

## O que este documento não cobre

Escrito para não ser usado como se fosse mais do que é:

- **não é uma análise formal.** Não há modelo de atacante, nem
  probabilidade de exploração, nem revisão de código linha a linha.
- **não cobre o lado cliente.** O que o OnyxChat faz antes de
  entregar um frame ao relay, e o que faz depois de o receber, é do
  repositório-mãe.
- **não cobre a pessoa que instala a placa.** Um MiniNode bem
  configurado numa rede de casa é a melhor configuração possível; um
  mal configurado em modo station numa rede pública é exactamente o
  que §A2 descreve, e nenhum firmware corrige isso.
- **não é um modelo de ameaças substituto.** O modelo de ameaças do
  OnyxChat continua a ser o documento que vale; este acrescenta o que
  muda por causa do hardware e da licença.
