#!/usr/bin/env python3
# =====================================================================
# verificar_estrutura.py — o mapa `Estrutura.txt` bate com a árvore?
# ---------------------------------------------------------------------
# Por que é que este ficheiro existe
#
# Um mapa de um repositório é uma promessa: «isto é o que há cá».
# Uma promessa que ninguém confere apodrece em silêncio, e apodrece
# pior do que a ausência de mapa, porque enquanto o mapa estiver
# escrito qualquer pessoa lhe responde com confiança — inclusive para
# perguntas cuja resposta já não é a verdadeira.
#
# A lição não é teórica. O repositório-mãe do OnyxChat teve um
# `Estrutura.txt` durante meses que listava sete ficheiros que nunca
# existiram, incluindo uma implementação de referência em Python que
# tinha sido substituída e um directório de testes de Lua apagado.
# Ninguém reparou, porque ler um mapa parece a mesma coisa que
# verificá-lo.
#
# O que este programa faz
#
# Lê `Estrutura.txt`, extrai as afirmações sobre ficheiros, e
# confronta-as com o que existe em disco. Sai com erro quando o mapa
# afirma que um ficheiro existe e ele não existe, ou quando afirma
# que um ficheiro não existe e ele existe.
#
# ## Porque é que não é uma cópia do verificador do repositório-mãe
#
# Duas razões, e as duas importam.
#
# A primeira é a licença. O verificador do OnyxChat é PolyForm
# Noncommercial e carrega o aviso que essa licença exige. Copiá-lo
# para um repositório MIT obrigaria a manter esse aviso num ficheiro
# MIT — que é tão incorrecto quanto o inverso, e que a exclusão de
# `OnyxMiniNode/` no guard de licença do pai existe precisamente
# para impedir que aconteça.
#
# A segunda é o tamanho. O do pai tem 578 linhas porque verifica
# factos que só existem no OnyxChat: a segunda árvore em
# `UI/README.md`, os métodos `async` de um componente da interface, a
# contagem de sementes do corpus de fuzzing, e as asserções de
# ausência dos sete ficheiros que a versão anterior inventava. Nada
# disso se aplica a um firmware com onze ficheiros, que é o que este
# tem. As duzentas linhas aqui são o que este repositório precisa —
# e não a versão maior com o que não se aplica retirado.
#
# ## A convenção do mapa
#
# `Estrutura.txt` é uma árvore ASCII com três convenções, e as três
# são verificadas aqui em vez de serem assumidas:
#
#   * uma linha que termina em `/` é um directório, e establish
#     contexto para as linhas seguintes;
#   * um `#` no fim de uma linha é comentário — a árvore vai até
#     ao primeiro `#`, e o resto é para quem lê;
#   * um cabeçalho de secção (`# docs/ — 3 documentos`) pode
#     declarar uma contagem, e essa contagem é conferida.
#
# Um til (`~`) antes de um nome afirma que o ficheiro **não**
# existe. É o que permite escrever «isto aqui já não existe» sem
# apagar a linha e perder a história.
# =====================================================================

from __future__ import annotations

import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
MAPA = RAIZ / "Estrutura.txt"

#: Directórios que são artefactos de construção e nunca são fonte.
#:
#: Têm de estar aqui e não num `.gitignore`, porque este programa lê o
#: disco e não o índice do Git. Um `.pio/` correctamente ignorado
#: continua a estar no disco, e ignorado não é o mesmo que ausente.
IGNORADOS = {".git", ".pio", ".vscode", ".idea", "__pycache__"}

#: `├─ src/mailbox.cpp`, `│   └─ frame.h`, `   ├─ x.cpp`.
#:
#: O grupo 1 é o **prefixo vertical** — as colunas `│` e espaços que
#: vêm antes do gancho. A profundidade de uma linha é o comprimento
#: desse prefixo, medido em unidades de indentação.
#:
#: O gancho (`├─` ou `└─`) **não** entra na medição, e essa é a parte
#: que dá a resposta certa na errada com muita facilidade. Contando-o,
#: uma linha de primeiro nível deixa de medir zero e passa a medir o
#: comprimento do gancho — o que empurra todas as profundidades um
#: nível para dentro e faz `├─ index.md` resolver para `index.md` em
#: vez de `docs/index.md`. Nenhum dos dois caminhos existe, a contagem
#: de ficheiros dá zero, e o verificador não diz nada: falha em
#: silêncio, que é a pior forma de falhar.
_MARCADOR = re.compile(r"^([│ ]*)[├└]─\s+(.*)$")

#: `# Raiz`, `# docs/ — 3 documentos`.
#:
#: Não exige barra: `# Raiz` é uma secção como outra qualquer, e o
#: que a distingue é estar na raiz do mapa.
_SECCAO = re.compile(r"^#\s+(.+?)\s*$")

#: A linha a seguir ao cabeçalho: `====` ou `----`. Não é uma
#: afirmação sobre ficheiros, é um separador.
_SEPARADOR = re.compile(r"^[-=]{3,}\s*$")

#: `21 documentos`, `20 ficheiros`, `Os 3 documentos`.
#:
#: As palavras compostas vêm **primeiro** na alternativa. Numa
#: alternância o Python devolve a primeira que casa, e `ficheiros`
#: casa antes de `ficheiros de código` — o que faria a contagem
#: errada sem dar erro nenhum, que é a pior forma de um verificador
#: falhar.
_CONTAGEM = re.compile(
    r"\b(?:\d+|um|dois|tr[êe]s|quatro|cinco|seis|sete|oito|nove|dez|"
    r"onze|doze|treze|quatorze|quinze|dezasseis|dezassete|dezoito|"
    r"dezanove|vinte)"
    r"\s+"
    r"(?:ficheiros\s+de\s+c[óo]digo|ficheiros|ficheiro|documentos|"
    r"componentes|ecr[ãa]s|tests?|testes|m[óo]dulos|alvos)\b"
)

#: Plural português: `ficheiros` -> `ficheiro`.
_PLURAL = {"ficheiros": "ficheiro", "documentos": "documento",
           "testes": "teste", "módulos": "módulo",
           "componentes": "componente", "ecrãs": "ecrã",
           "alvos": "alvo"}

#: Um nome dentro da linha: `src/mailbox.cpp`, `Estrutura.txt`.
_NOME = re.compile(r"[\w.][\w./+-]*")

#: O til: «isto não existe». Vale para directórios e ficheiros.
_TIL = "~"


class Afirmacao:
    """O que o mapa diz sobre um caminho, e onde disse.

    Guarda a linha porque a mensagem de erro sem linha é uma
    afirmação que ninguém consegue corrigir: o mapa tem 200 linhas e
    a pessoa que o maintain é a mesma que vai procurar a linha.
    """

    def __init__(self, caminho: str, existe: bool, linha: int) -> None:
        self.caminho = caminho
        self.existe = existe
        self.linha = linha

    def __repr__(self) -> str:  # pragma: no cover - só para depuração
        return f"Afirmacao({self.caminho!r}, {self.existe}, {self.linha})"


def ficheiros_reais() -> set[str]:
    """Todos os ficheiros da árvore, relativos à raiz do repositório."""
    achados: set[str] = set()
    for caminho in RAIZ.rglob("*"):
        if not caminho.is_file():
            continue
        if set(caminho.relative_to(RAIZ).parts) & IGNORADOS:
            continue
        achados.add(str(caminho.relative_to(RAIZ)))
    return achados


def contagens() -> list[tuple[str, int | None, int, str]]:
    """As contagens declaradas nos cabeçalhos, com o que se mediu.

    Devolve tuplos `(descrição, declarado, linha, medido)` para o
    relatório, e não directamente os erros, porque o relatório é
    útil mesmo quando não há erro nenhum: «afirmações no mapa: 84» é
    informação, não apenas diagnóstico.
    """
    if not MAPA.is_file():
        return []

    saida: list[tuple[str, int | None, int, str]] = []
    linhas = MAPA.read_text(encoding="utf-8").splitlines()

    for numero, linha in enumerate(linhas, 1):
        if not linha.startswith("#") or _SEPARADOR.match(linha):
            continue
        achado = _CONTAGEM.search(linha)
        if not achado:
            continue

        declarado = _numero(achado.group(0).split()[0])
        if declarado is None:
            continue

        substantivo = achado.group(0).split(maxsplit=1)[1]
        directo = linha.split("#", 1)[1].strip()
        alvo = directo.split()[0].rstrip("/") if directo else ""
        medido = _medir(RAIZ / alvo if alvo else RAIZ, substantivo)
        saida.append((substantivo, declarado, numero, medido or ""))

    return saida


def _numero(texto: str) -> int | None:
    """Converte o numeral que o mapa usou. Devolve `None` se não der.

    Aceita os algarismos e as palavras até vinte, porque o mapa é
    escrito por pessoas e `--- documentos` não é uma contagem, é um
    erro de digitação que a auditoria tem de apanhar.
    """
    try:
        return int(texto)
    except ValueError:
        pass
    palavras = {
        "um": 1, "dois": 2, "três": 3, "cuatro": 4,
        "quatro": 4, "cinco": 5, "seis": 6, "sete": 7,
        "oito": 8, "nove": 9, "dez": 10, "onze": 11,
        "doze": 12, "treze": 13, "catorce": 14, "quatorze": 14,
        "quinze": 15, "dezasseis": 16, "dezassete": 17,
        "dezoito": 18, "dezanove": 19, "vinte": 20,
    }
    return palavras.get(texto)


def _medir(directorio: Path, substantivo: str) -> int | None:
    """Conta o que o substantivo diz que existe.

    Devolve `None` quando não é contável, e não `0`. A diferença é
    entre «medir mal» e «deixar por medir», e medir mal é o pior
    resultado possível: produz um número plausível e errado.
    """
    if not directorio.is_dir():
        return None
    if substantivo in _PLURAL:
        substantivo = _PLURAL[substantivo]
    if substantivo in ("documento", "ficheiro", "módulo"):
        return len([p for p in directorio.rglob("*") if p.is_file()])
    if substantivo == "teste":
        return len([p for p in directorio.rglob("*")
                    if p.is_file() and p.name.startswith("test")])
    return None


def afirmacoes() -> list[Afirmacao]:
    """Lê o mapa e devolve o que ele afirma sobre cada caminho.

    ## A pilha
    #
    # Numa árvore, o pai de um nó de profundidade `d` é o nó de
    # profundidade `d - 1`. É isso que faz `├─ c_cpp/` dentro de
    # `# 1. Criptografia` resolver para `crypto/c_cpp` e não para
    # `c_cpp` — e o erro oposto já aconteceu no mapa do
    # repositório-mãe, onde componentes que deveriam ficar em
    # `UI/src/components/` acabaram lidos como `components/`, um
    # caminho que não existe, com contagem zero e sem erro nenhum.
    #
    # ## A medição da profundidade
    #
    # A unidade mede-se pelo menor prefixo não vazio do documento. É a
    # única definição que não pressupõe nada sobre quem escreveu a
    # árvore — medir a partir da indentação absoluta daria profundidades
    # diferentes para o mesmo formato conforme o ficheiro.
    """
    if not MAPA.is_file():
        return []

    linhas = MAPA.read_text(encoding="utf-8").splitlines()
    unidade = _unidade(linhas)
    contexto: str | None = None
    pilha: list[str] = []
    saida: list[Afirmacao] = []

    def fechar(nome: str, profundidade: int) -> str:
        """Fecha um nome com o directório a que pertence."""
        if profundidade == 0:
            return nome if contexto is None else f"{contexto}/{nome}"
        base = pilha[profundidade - 1] if profundidade - 1 < len(pilha) else None
        if base is None:
            return nome
        return nome if "/" in nome else f"{base}/{nome}"

    for numero, linha in enumerate(linhas, 1):
        if _SEPARADOR.match(linha):
            continue

        if linha.startswith("#"):
            # Um cabeçalho de secção só estabelece contexto se o seu
            # primeiro token **terminar em barra**. `# docs/ — 3
            # documentos` estabelece `docs`; `# Raiz` não estabelece
            # nada, porque a raiz não é um directório dentro de si
            # própria. Confundir os dois faz toda a secção Raiz
            # resolver para `Raiz/Cargo.toml`.
            #
            # A primeira linha do ficheiro — o título do projecto — não
            # começa por `#` e é ignorada como prosa. É o
            # comportamento certo, e é o que faz `contexto` valer
            # `None` à entrada da secção Raiz.
            titulo = linha[1:].strip()
            primeiro = titulo.split()[0] if titulo else ""
            contexto = primeiro.rstrip("/") if primeiro.endswith("/") else None
            pilha = []
            continue

        if (m := _MARCADOR.match(linha)) is None:
            # Uma linha sem marcador é prosa, e prosa não é uma
            # afirmação sobre ficheiros. Ignorá-la em silêncio é
            # correcto; deixá-la passar por uma afirmação daria ao
            # mapa a capacidade de declarar ficheiros que o
            # verificador nunca vê.
            continue

        profundidade = len(m.group(1)) // unidade

        # O comentário à direita não é parte da árvore. Os nomes de
        # ficheiro que aparecem lá — `docs/protocolo.md` numa
        # referência cruzada, por exemplo — passariam a ser
        # verificados como se fossem entradas do mapa, e o relatório
        # passaria a acusar em falta ficheiros que existem.
        corpo = linha.split("#", 1)[0]

        for token in _NOME.findall(corpo):
            existe = not token.startswith(_TIL)
            nome = token.lstrip(_TIL).rstrip("/")
            if not nome:
                continue

            caminho = fechar(nome, profundidade)

            if token.endswith("/"):
                # Um directório establish contexto. **Não** é uma
                # afirmação de existência: o verificador só fala de
                # ficheiros, e um directório existe ou não existe
                # sem que o mapa tenha algo a dizer sobre isso.
                #
                # Esta distinção é o que permite ao mapa do
                # repositório-mãe listar um submodule que o CI não
                # clona: a entrada fica correcta com e sem ele.
                del pilha[profundidade:]
                pilha.append(caminho)
                continue

            saida.append(Afirmacao(caminho, existe, numero))

    return saida


def _unidade(linhas: list[str]) -> int:
    """Quantas colunas tem um nível de indentação neste documento."""
    varios: list[int] = []
    for linha in linhas:
        if m := _MARCADOR.match(linha):
            n = len(m.group(1))
            if n:
                varios.append(n)
    return min(varios) if varios else 1


def main() -> int:
    if not MAPA.is_file():
        print(f"falta o mapa: {MAPA}", file=sys.stderr)
        return 2

    af = afirmacoes()
    if not af:
        print("nenhuma afirmação lida do mapa — o formato mudou?",
              file=sys.stderr)
        return 2

    reais = ficheiros_reais()
    erros: list[str] = []

    for a in af:
        existe = a.caminho in reais
        if a.existe and not existe:
            erros.append(
                f"em falta   {a.caminho}  ({MAPA.name}:{a.linha})")
        elif not a.existe and existe:
            erros.append(
                f"afirma que falta, mas existe  {a.caminho}  "
                f"({MAPA.name}:{a.linha})")

    afirmadas = sum(1 for a in af if a.existe)
    print(f"afirmações no mapa: {len(af)}  ({afirmadas} de existência, "
          f"{len(af) - afirmadas} de ausência)")
    print(f"ficheiros na árvore: {len(reais)}")

    divergentes: list[str] = []
    for substantivo, declarado, linha, medido in contagens():
        if declarado is None or not medido:
            continue
        if declarado != medido:
            onde = f"{MAPA.name}:{linha} — «{substantivo}»"
            divergentes.append(
                f"contagem  {onde}: mapa diz {declarado}, "
                f"árvore tem {medido}")

    if divergentes:
        erros.extend(divergentes)

    if erros:
        print()
        for e in erros:
            print(f"  {e}")
        print()
        print("Actualiza `Estrutura.txt` para que corresponda à árvore, "
              "ou apaga a linha que deixou de ser verdadeira.",
              file=sys.stderr)
        return 1

    print("o mapa corresponde à árvore")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
