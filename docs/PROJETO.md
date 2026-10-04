# Trabalho 01 — Cliente DNS (consultas MX)

> **Disciplina:** Fundamentos de Redes de Computadores — UnB Gama — Prof. Tiago Alves
> **Linguagem:** C (padrão GNU11, compilado com GCC no Linux)
> **Equipe:** trio, cada integrante responsável por um módulo

Este documento explica **o que é o projeto, por que ele existe e como está organizado**. Serve para dois públicos:
- **Integrantes do grupo**, para entenderem o trabalho do zero;
- **Assistentes de IA** usados via CLI (Claude Code, etc.), para entenderem o escopo e as regras. A seção 11 é dirigida especificamente a eles.

Os detalhes de implementação de cada módulo ficam nos guias em `docs/`:
- `docs/modulo1_montagem.md`
- `docs/modulo2_rede.md`
- `docs/modulo3_parser.md`

---

## 1. Resumo em uma frase

Um programa de linha de comando que pergunta a um servidor DNS **"qual servidor recebe os e-mails do domínio X?"** e imprime a resposta. É uma versão simplificada do comando `dig X MX`, escrita do zero, **montando e lendo os bytes do protocolo manualmente**.

```
$ ./meu_cliente unb.br 8.8.8.8
unb.br <> unb-br.mail.protection.outlook.com
```

---

## 2. Conceitos básicos (leia se estiver perdido)

### DNS
O "catálogo telefônico" da internet. Você manda um nome para um servidor DNS e ele devolve informações sobre esse nome. Cada tipo de informação é um **registro**:

| Registro | Responde a pergunta |
|---|---|
| A | Qual o IP deste site? |
| CNAME | Este nome é apelido de qual outro? |
| **MX** | **Qual servidor recebe os e-mails deste domínio?** ← o único que usamos |

Teste no seu terminal:
```
dig unb.br MX @8.8.8.8
```
Nosso programa faz exatamente isso, só que escrito por nós.

### Servidor DNS
Um computador que responde consultas DNS. O IP dele é o **segundo argumento** do programa. Exemplos públicos: `8.8.8.8` (Google), `1.1.1.1` (Cloudflare). Sempre na **porta 53**.

### UDP
Forma de enviar dados pela rede **sem conexão e sem garantia de entrega**: você joga o pacote e torce para chegar. Por isso o programa precisa de **timeout e retentativas** (se a resposta não chegar em 2 segundos, envia de novo).

### Socket
A "tomada" que o sistema operacional oferece para um programa usar a rede. Em C: `socket()`, `sendto()`, `recvfrom()`, `close()`.

### Pacote / bytes
Uma consulta DNS não é texto: é uma sequência de bytes num formato definido pelas **RFC 1034 e 1035**. Nós montamos esses bytes um a um num `uint8_t buf[]`.

### Big-endian
Números com mais de 1 byte são enviados na rede com o **byte mais significativo primeiro**. Exemplo: o número `0x0100` vira os bytes `01 00`.

### Arquivo `.h` (header)
Em C, o `.h` contém só as **declarações** das funções (nome, parâmetros, retorno), sem implementação. É parecido com uma `interface` em Java/TypeScript. Nosso `dns.h` é o **contrato entre os módulos**: cada pessoa implementa sua parte respeitando o que está nele, e por isso as partes se encaixam no final.

---

## 3. O que o programa precisa fazer

### Uso
```
./meu_cliente <dominio> <ip_do_servidor_dns>
```

### Fluxo
1. Gerar um ID aleatório de 16 bits para a consulta.
2. Montar o pacote de consulta DNS tipo **MX**, classe **IN**, com flag de recursão.
3. Enviar por **UDP** para `<ip>:53`.
4. Esperar a resposta por **2 segundos**. Sem resposta, reenviar. **Máximo de 3 tentativas.**
5. Interpretar os bytes da resposta e extrair o nome do servidor de e-mail.
6. Imprimir o resultado.

### Saídas obrigatórias (exatamente estes textos)

| Situação | Saída |
|---|---|
| Sucesso | `unb.br <> unb-br.mail.protection.outlook.com` |
| Domínio não existe | `Dominio imagdaskdasdasj.br nao encontrado` |
| Domínio sem registro MX | `Dominio fga.unb.br nao possui entrada MX` |
| Servidor não respondeu após 3 tentativas | `Nao foi possível coletar entrada MX para unb.br` |

---

## 4. Regras do enunciado (não negociáveis)

- **Proibido usar bibliotecas de DNS/comunicação de alto nível.** Nada de `getaddrinfo`, `gethostbyname`, `res_query`, `libresolv`, `ldns`, `c-ares`. Só **sockets UDP** puros.
- **O pacote de consulta deve ser montado programaticamente**, byte a byte.
- A leitura da resposta pode usar funções auxiliares de manipulação de bytes, mas nós também fazemos manualmente.
- **Código que não compila zera a nota.**
- **Não enviar executáveis** no ZIP.
- Plágio ou cópia anula o trabalho de todos os envolvidos.
- Deflator por linguagem: C/Rust 100%, C++/Go 95%, interpretadas/Java 90%. **Escolhemos C.**

### Campos obrigatórios da consulta

| Campo | Valor |
|---|---|
| Transaction ID | aleatório, 16 bits |
| Flags | `0x0100` (consulta recursiva) |
| Questions | `0x0001` |
| Answer RRs / Authority RRs / Additional RRs | `0x0000` |
| Query | Name = domínio informado, Type = MX (15), Class = IN (1) |

---

## 5. Arquitetura

### Visão geral

```
                    ┌──────────────┐
  argv[1], argv[2]  │   main.c     │  coordena tudo e imprime
 ──────────────────►│  (Módulo 2)  │
                    └──────┬───────┘
          ┌────────────────┼────────────────────┐
          ▼                ▼                    ▼
 ┌─────────────────┐ ┌──────────────┐ ┌──────────────────┐
 │ dns_query.c     │ │ net.c        │ │ dns_parse.c      │
 │ (Módulo 1)      │ │ (Módulo 2)   │ │ (Módulo 3)       │
 │ monta os bytes  │ │ envia/recebe │ │ interpreta bytes │
 │ da consulta     │ │ via UDP      │ │ da resposta      │
 └─────────────────┘ └──────────────┘ └──────────────────┘
          ▲                ▲                    ▲
          └────────────────┴────────────────────┘
                     todos incluem dns.h
```

### Divisão de responsabilidades

| Módulo | Arquivos | Faz | Não faz | Extra |
|---|---|---|---|---|
| **1 — Montagem** | `dns_query.c` | Gera ID e monta o pacote de consulta | rede, leitura de resposta | README |
| **2 — Rede** | `net.c`, `main.c` | Socket UDP, timeout, retentativas, fluxo principal, mensagens | montar/ler pacotes | Makefile |
| **3 — Parser** | `dns_parse.c` | Valida a resposta, segue compressão de nomes, extrai o MX | rede, montar consulta | — |

### O contrato (`dns.h`)

**Não alterar sem combinar com o trio.**

```c
#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stddef.h>

#define DNS_PORT      53
#define DNS_MAX_PKT   512
#define DNS_MAX_NAME  256
#define DNS_TYPE_MX   15
#define DNS_CLASS_IN  1
#define DNS_FLAGS_RD  0x0100

typedef enum {
    DNS_OK,        /* achou o MX */
    DNS_NXDOMAIN,  /* domínio não existe */
    DNS_NO_MX,     /* domínio existe, mas sem MX */
    DNS_ERROR      /* resposta inválida/corrompida */
} dns_status;

/* Módulo 1 — Montagem */
uint16_t dns_generate_id(void);
int dns_build_query(const char *domain, uint16_t id, uint8_t *buf, size_t bufsize);

/* Módulo 2 — Rede */
int udp_query(const char *server_ip, const uint8_t *query, size_t qlen,
              uint8_t *resp, size_t resp_size);

/* Módulo 3 — Parser */
dns_status dns_parse_mx(const uint8_t *resp, size_t len, uint16_t expected_id,
                        char *mx_out, size_t mx_size);

#endif
```

### Fluxo de dados no `main`

```
id     = dns_generate_id()                         → Módulo 1
qlen   = dns_build_query(dominio, id, query, ...)  → Módulo 1
rlen   = udp_query(ip, query, qlen, resp, ...)     → Módulo 2
status = dns_parse_mx(resp, rlen, id, mx, ...)     → Módulo 3
imprime conforme status
```

### Mapeamento de status para mensagem

| Condição | Mensagem |
|---|---|
| `DNS_OK` | `<dominio> <> <mx>` |
| `DNS_NXDOMAIN` | `Dominio <dominio> nao encontrado` |
| `DNS_NO_MX` | `Dominio <dominio> nao possui entrada MX` |
| `udp_query` retornou `-1` **ou** `DNS_ERROR` | `Nao foi possível coletar entrada MX para <dominio>` |

---

## 6. O protocolo em uma página

### Consulta para `unb.br` (24 bytes)
```
AB CD  01 00  00 01  00 00  00 00  00 00    cabeçalho: ID, flags, contadores
03 75 6E 62 02 62 72 00                     nome: 3"unb" 2"br" 0
00 0F  00 01                                tipo MX, classe IN
```

### Resposta
```
[cabeçalho 12 bytes] [pergunta repetida] [respostas] [authority] [additional]
```
- **ID** da resposta = ID enviado.
- **RCODE** = `flags & 0x000F` → `0` ok, `3` domínio não existe.
- **ANCOUNT** = quantas respostas. `0` = sem MX.
- Cada resposta: `nome | tipo(2) | classe(2) | TTL(4) | tamanho(2) | dados`.
- Dados de um MX: `preferência(2) | nome do servidor`.

### Compressão de nomes (a pegadinha principal)
Um byte com os 2 bits mais altos em `11` (ex.: `C0`) é um **ponteiro**: o nome continua na posição indicada pelos 14 bits seguintes. Ex.: `C0 0C` significa "o nome está no byte 12 do pacote". O parser precisa seguir esses ponteiros. Detalhes em `docs/modulo3_parser.md`.

---

## 7. Estrutura do repositório

```
trab01/
├── CLAUDE.md             ← cópia deste arquivo (lida automaticamente pela IA)
├── PROJETO.md            ← este arquivo
├── README.md             ← documentação de entrega (20% da nota)
├── Makefile
├── .gitignore
├── src/                  ← todo o código-fonte (.h e .c)
│   ├── dns.h             ← contrato
│   ├── main.c            ← Módulo 2
│   ├── net.c             ← Módulo 2
│   ├── dns_query.c       ← Módulo 1
│   └── dns_parse.c       ← Módulo 3
├── docs/
│   ├── modulo1_montagem.md
│   ├── modulo2_rede.md
│   └── modulo3_parser.md
└── tests/
    ├── test_query.c
    ├── test_net.c
    ├── test_parse.c
    └── fixtures/         ← respostas reais capturadas (.bin)
```

---

## 8. Compilar, executar e testar

```bash
make                         # gera ./meu_cliente
make clean                   # remove .o e executável

./meu_cliente unb.br 8.8.8.8
./meu_cliente imagdaskdasdasj.br 1.1.1.1
./meu_cliente fga.unb.br 8.8.8.8
./meu_cliente unb.br 1.2.3.4             # deve levar ~6s e falhar
```

Testes isolados de cada módulo (a partir da raiz):
```bash
gcc -Wall -Isrc -o tests/test_query tests/test_query.c src/dns_query.c
gcc -Wall -Isrc -o tests/test_net   tests/test_net.c   src/net.c
gcc -Wall -Isrc -o tests/test_parse tests/test_parse.c src/dns_parse.c
```

Ferramentas úteis:
- `dig +noedns <dominio> MX @8.8.8.8`: resposta "oficial" para comparar.
- **Wireshark** (filtro `dns`): ver os bytes reais da consulta e da resposta.
- `xxd`: ver ou converter bytes em hexadecimal.

**Firewall:** algumas redes (como a da universidade) bloqueiam DNS externo. Se tudo der timeout, teste em outra rede ou use o DNS da rede local (veja em `/etc/resolv.conf`).

---

## 9. Fluxo de trabalho do trio

1. **Encontro inicial (juntos):** criar o repositório, escrever `dns.h` e `Makefile`, capturar as respostas de teste com o Wireshark.
2. **Desenvolvimento em paralelo:** uma branch por módulo (`feat/query`, `feat/net`, `feat/parse`). Cada um testa com seu `tests/test_*.c`, sem depender dos outros.
3. **Integração (juntos):** merge na `main`, rodar os 4 exemplos do enunciado, corrigir.
4. **Fechamento:** finalizar o README, testar `make` numa máquina limpa, montar o ZIP.

**Regra de ouro:** mudanças no `dns.h` só com aviso a todos.

---

## 10. Entrega

- **Arquivo:** `nome_sobrenome_matricula_nome_sobrenome_matricula_trab01.zip`, via Sigaa. ⚠️ O enunciado fala em **duplas**: confirmar com o professor se o trio é aceito e como nomear o ZIP.
- **Conteúdo:** `src/` + `Makefile` + `README.md`. **Sem executáveis nem `.o`.** A pasta `tests/` **não** vai (o enunciado não pede testes, e um arquivo que não compila poderia anular a nota). `docs/`, `PROJETO.md` e `CLAUDE.md` também não vão, porque são material interno.
- **Gerar o ZIP:** `make zip`.
- **Nota:** código = 80%, README = 20%.
- **O README deve conter:** sistema operacional usado; ambiente de desenvolvimento; como compilar; como executar; telas/instruções de uso; limitações conhecidas.

### Limitações conhecidas (até agora)
- Só consultas MX, só IPv4.
- Respostas truncadas (flag TC) não são tratadas; não há fallback para TCP.
- Sem EDNS: respostas limitadas a 512 bytes.
- Com vários MX, apenas o de menor preferência é exibido.

---

## 11. Instruções para assistentes de IA

Se você é um assistente de IA trabalhando neste repositório, siga estas regras:

### Contexto
- Projeto acadêmico de estudantes que **ainda estão aprendendo** redes e C. Ao gerar ou alterar código, **explique o que fez e por quê**, em português, de forma didática.
- Idioma de comentários, mensagens de commit e explicações: **português (pt-BR)**. Mensagens de saída do programa: **exatamente** como na seção 3 (sem acentos, exceto "possível", que está assim no enunciado).

### Restrições técnicas obrigatórias
- **Linguagem C apenas.** Não sugerir C++, Rust, Python, etc.
- Todo código-fonte (`.h` e `.c`) fica em `src/`. Testes ficam em `tests/` e incluem o header com `-Isrc`.
- **Proibido** usar `getaddrinfo`, `gethostbyname`, `res_query`, `<resolv.h>` ou qualquer biblioteca de DNS. Permitido: `socket`, `sendto`, `recvfrom`, `setsockopt`, `close`, `inet_pton`, `htons` e a biblioteca padrão de C.
- O pacote de consulta deve ser montado **byte a byte** em `uint8_t[]`.
- Todo código deve compilar **sem warnings** com `gcc -Wall -Wextra -std=gnu11`.
- Toda leitura de buffer deve verificar limites (nunca ler além de `len`).
- Funções auxiliares de cada módulo devem ser `static`.

### Limites de escopo
- **Não altere `dns.h`** sem que o usuário peça explicitamente. É o contrato do trio.
- **Respeite os limites dos módulos:** cada arquivo `.c` pertence a um integrante. Se o usuário pedir ajuda no Módulo X, altere apenas os arquivos do Módulo X (veja a tabela da seção 5), a menos que ele peça outra coisa.
- Antes de implementar um módulo, **leia o guia correspondente em `docs/`**: ele define as funções, assinaturas e casos de teste esperados.
- Não adicione funcionalidades fora do enunciado (outros tipos de registro, IPv6, TCP, cache, flags de linha de comando).
- Não inclua executáveis nem arquivos `.o` em commits ou no ZIP.

### Como validar mudanças
1. `make` compila sem erros nem warnings.
2. Teste do módulo alterado em `tests/` passa.
3. Os 4 exemplos da seção 3 produzem as saídas exatas.

---

## 12. Glossário rápido

| Termo | Significado |
|---|---|
| **MX** | Mail Exchanger: registro que indica o servidor de e-mail de um domínio |
| **RR** | Resource Record: cada registro dentro de uma resposta DNS |
| **RCODE** | Código de resultado da resposta (0 = ok, 3 = NXDOMAIN) |
| **NXDOMAIN** | "Non-Existent Domain": o domínio não existe |
| **QDCOUNT / ANCOUNT** | Quantidade de perguntas / respostas no pacote |
| **Recursiva** | Pedimos ao servidor que busque a resposta completa por nós |
| **Timeout** | Tempo máximo de espera pela resposta (2s) |
| **Header (`.h`)** | Arquivo de declarações em C; o contrato entre módulos |
| **`static`** | Função visível só dentro do próprio arquivo `.c` |
| **Big-endian** | Byte mais significativo primeiro (ordem usada na rede) |
