# Módulo 3 — Interpretação da Resposta DNS (Parser)

**Arquivo:** `src/dns_parse.c`
**Responsável:** Desenvolvedor C

> Este é o módulo com mais lógica do trabalho. Por isso não tem tarefa extra.

---

## 1. O que este módulo faz

Recebe os **bytes da resposta** que chegaram do servidor e descobre:
- se a resposta é válida e é da nossa consulta;
- se o domínio existe;
- se tem registro MX e, se tiver, **qual é o nome do servidor de e-mail**.

Este módulo **não** usa rede e **não** monta consultas.

```
main.c ──► Módulo 1 (monta) ──► Módulo 2 (envia/recebe) ──► [MÓDULO 3: interpreta] ──► main imprime
```

O enunciado permite usar funções auxiliares para manipular a resposta, mas a forma mais segura (e mais didática) é ler os bytes manualmente, como descrito aqui.

---

## 2. O contrato compartilhado (`dns.h`)

Este arquivo é escrito pelo trio **antes de tudo** e não é alterado sem avisar o grupo. Ele é a "interface" entre os módulos: cada um só precisa conhecer este arquivo, nunca o código dos outros.

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

**Sua parte do contrato:** `dns_parse_mx`.

---

## 3. Como é uma resposta DNS

```
┌─────────────────────────────┐
│ Cabeçalho (12 bytes)        │  ID, flags, contadores
├─────────────────────────────┤
│ Pergunta (repetida)         │  a mesma que enviamos: nome + tipo + classe
├─────────────────────────────┤
│ Respostas (ANCOUNT vezes)   │  ← o que nos interessa
├─────────────────────────────┤
│ Authority / Additional      │  pode ignorar
└─────────────────────────────┘
```

### 3.1 Cabeçalho: o que olhar

| Posição | Campo | O que fazer |
|---|---|---|
| 0–1 | ID | tem que ser igual ao `expected_id` |
| 2–3 | Flags | bit QR (`0x8000`) tem que estar ligado (é resposta)<br>RCODE = `flags & 0x000F`: `0` = ok, `3` = domínio não existe |
| 4–5 | QDCOUNT | quantas perguntas pular (normalmente 1) |
| 6–7 | ANCOUNT | quantas respostas ler |
| 8–11 | NSCOUNT, ARCOUNT | ignorar |

### 3.2 Formato de cada resposta (Resource Record)

```
NOME      (tamanho variável, pode ser comprimido)
TYPE      2 bytes   → 15 = MX, 5 = CNAME, 1 = A...
CLASS     2 bytes   → 1 = IN
TTL       4 bytes   → ignorar
RDLENGTH  2 bytes   → tamanho dos dados
RDATA     RDLENGTH bytes
```

Para um registro MX, o RDATA é:
```
PREFERENCE  2 bytes   → prioridade (menor = preferido)
EXCHANGE    nome do servidor de e-mail (pode ser comprimido!)
```

### 3.3 Compressão de nomes (a parte mais importante)

Para economizar espaço, o servidor não repete nomes. Um nome pode ser:

1. **Normal:** `03 'u' 'n' 'b' 02 'b' 'r' 00`
2. **Só um ponteiro:** `C0 0C`, que significa "o nome está na posição 12 do pacote"
3. **Misto:** começa normal e termina com um ponteiro. Ex.: `06 'u' 'n' 'b' '-' 'b' 'r' C0 2A` significa "unb-br" + "o resto está na posição 0x2A"

**Como reconhecer um ponteiro:** os 2 bits mais altos do byte são `11`.
```c
if ((b & 0xC0) == 0xC0) {
    /* é ponteiro */
    offset_destino = ((b & 0x3F) << 8) | proximo_byte;
}
```

**Cuidado com dois detalhes:**
- Depois de seguir um ponteiro, a posição de leitura "original" avança **só 2 bytes** (o tamanho do ponteiro), não o tamanho do nome inteiro.
- Um pacote malicioso pode ter um ponteiro apontando para si mesmo (loop infinito). Limite o número de saltos, por exemplo a 20.

---

## 4. Estruturas internas

Definidas dentro do `dns_parse.c` (não vão para o `dns.h`):

```c
struct dns_header {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
};

struct dns_rr {
    uint16_t type;
    uint16_t class;
    uint16_t rdlength;
    size_t   rdata_offset;   /* posição onde começa o RDATA */
};
```

---

## 5. Mapa de chamadas

```
main.c
  └─► dns_parse_mx()                     ← única porta de entrada do módulo
         ├─► parse_header()
         │      └─► read_u16()
         ├─► skip_question()             (QDCOUNT vezes)
         │      └─► read_name()          (com out = NULL, só para pular)
         └─► para cada resposta:
                ├─► read_rr()
                │      ├─► read_name()
                │      └─► read_u16()
                └─► extract_mx()         (só se type == MX)
                       ├─► read_u16()
                       └─► read_name()
```

- **Pública**: está no `dns.h`.
- **Internas** (`static`): só existem dentro do `dns_parse.c`.

---

## 6. Função pública

### 6.1 `dns_parse_mx`

| | |
|---|---|
| **Objetivo** | Interpretar a resposta e extrair o servidor MX |
| **Assinatura** | `dns_status dns_parse_mx(const uint8_t *resp, size_t len, uint16_t expected_id, char *mx_out, size_t mx_size);` |
| **Parâmetros** | `resp`: bytes recebidos (vêm do Módulo 2)<br>`len`: quantidade de bytes<br>`expected_id`: ID que enviamos<br>`mx_out`: onde escrever o nome do MX<br>`mx_size`: tamanho de `mx_out` |
| **Retorno** | `DNS_OK`, `DNS_NXDOMAIN`, `DNS_NO_MX` ou `DNS_ERROR` |
| **Usa** | `parse_header`, `skip_question`, `read_rr`, `extract_mx` |
| **Usada por** | `main.c` |

**Lógica:**
```
se parse_header falhar             → DNS_ERROR
se h.id != expected_id             → DNS_ERROR
se bit QR desligado                → DNS_ERROR
rcode = h.flags & 0x000F
se rcode == 3                      → DNS_NXDOMAIN
se rcode != 0                      → DNS_ERROR
se h.ancount == 0                  → DNS_NO_MX

offset = 12
repete h.qdcount vezes: offset = skip_question(...)

achou = falso
repete h.ancount vezes:
    offset = read_rr(..., &rr)
    se rr.type == DNS_TYPE_MX e rr.class == DNS_CLASS_IN:
        extract_mx(..., &pref, nome)
        se ainda não achou ou pref < melhor_pref:
            guarda nome em mx_out; melhor_pref = pref; achou = verdadeiro

se achou → DNS_OK, senão → DNS_NO_MX
```

**Por que verificar o tipo de cada resposta?** Às vezes o servidor responde com um registro CNAME (apelido) em vez de MX. Se nenhuma resposta for do tipo 15, o domínio não tem MX.

**Por que escolher a menor preferência?** Domínios podem ter vários servidores MX. O de menor número é o principal. Para `unb.br` só há um, então o exemplo do enunciado sai igual.

---

## 7. Funções internas (`static`)

### 7.1 `read_u16`

| | |
|---|---|
| **Objetivo** | Ler um número de 16 bits em big-endian |
| **Assinatura** | `static uint16_t read_u16(const uint8_t *p);` |
| **Parâmetros** | `p`: posição no buffer |
| **Retorno** | o número lido |
| **Usa** | nada |
| **Usada por** | `parse_header`, `read_rr`, `extract_mx` |

**Dica:** `return (uint16_t)((p[0] << 8) | p[1]);`

**Atenção:** quem chama precisa garantir antes que existem 2 bytes disponíveis.

### 7.2 `parse_header`

| | |
|---|---|
| **Objetivo** | Ler os 12 bytes do cabeçalho para a struct |
| **Assinatura** | `static int parse_header(const uint8_t *msg, size_t len, struct dns_header *h);` |
| **Parâmetros** | `msg`, `len`, `h` (struct a preencher) |
| **Retorno** | `0` se ok, `-1` se `len < 12` |
| **Usa** | `read_u16` (6 vezes) |
| **Usada por** | `dns_parse_mx` |

### 7.3 `read_name` ⭐ (a mais importante)

| | |
|---|---|
| **Objetivo** | Ler um nome no formato DNS (seguindo ponteiros de compressão) e convertê-lo para texto com pontos |
| **Assinatura** | `static int read_name(const uint8_t *msg, size_t len, size_t offset, char *out, size_t outsize);` |
| **Parâmetros** | `msg`: pacote **inteiro** (necessário para seguir ponteiros)<br>`len`: tamanho do pacote<br>`offset`: onde o nome começa<br>`out`: destino do texto, ou `NULL` se só quiser pular o nome<br>`outsize`: tamanho de `out` |
| **Retorno** | quantos bytes o nome ocupa **na posição original** (para o chamador avançar), ou `-1` em erro |
| **Usa** | nada |
| **Usada por** | `skip_question`, `read_rr`, `extract_mx` |

**Lógica sugerida:**
```
pos = offset; consumido = -1; saltos = 0; escrito = 0
loop:
    se pos >= len → erro
    b = msg[pos]
    se b == 0:
        se consumido == -1 → consumido = pos + 1 - offset
        termina
    se (b & 0xC0) == 0xC0:              /* ponteiro */
        se pos + 1 >= len → erro
        se consumido == -1 → consumido = pos + 2 - offset
        pos = ((b & 0x3F) << 8) | msg[pos + 1]
        saltos++; se saltos > 20 → erro
        continua
    /* rótulo normal de b letras */
    se pos + 1 + b > len → erro
    se escrito > 0 → escreve '.'
    copia b letras de msg[pos+1]
    pos += 1 + b
termina out com '\0'
retorna consumido
```

**Exemplos:**

| Bytes na posição `offset` | Texto | Retorno |
|---|---|---|
| `03 u n b 02 b r 00` | `unb.br` | 8 |
| `C0 0C` (aponta para o nome acima) | `unb.br` | 2 |
| `04 m a i l C0 0C` | `mail.unb.br` | 7 |

### 7.4 `skip_question`

| | |
|---|---|
| **Objetivo** | Pular uma entrada da seção de pergunta |
| **Assinatura** | `static int skip_question(const uint8_t *msg, size_t len, size_t offset);` |
| **Parâmetros** | `msg`, `len`, `offset` (início da pergunta) |
| **Retorno** | novo offset (logo após a pergunta), ou `-1` em erro |
| **Usa** | `read_name` (com `out = NULL`) |
| **Usada por** | `dns_parse_mx` |

**Lógica:** `n = read_name(...)`; depois soma `n + 4` (tipo + classe) e confere se não passou de `len`.

### 7.5 `read_rr`

| | |
|---|---|
| **Objetivo** | Ler o "envelope" de uma resposta (sem interpretar o RDATA) |
| **Assinatura** | `static int read_rr(const uint8_t *msg, size_t len, size_t offset, struct dns_rr *rr);` |
| **Parâmetros** | `msg`, `len`, `offset` (início da resposta), `rr` (struct a preencher) |
| **Retorno** | offset da **próxima** resposta, ou `-1` em erro |
| **Usa** | `read_name` (com `out = NULL`), `read_u16` |
| **Usada por** | `dns_parse_mx` |

**Lógica:**
```
n = read_name(msg, len, offset, NULL, 0)
p = offset + n
se p + 10 > len → erro
rr->type     = read_u16(msg + p)
rr->class    = read_u16(msg + p + 2)
              /* TTL: 4 bytes em p+4, ignorar */
rr->rdlength = read_u16(msg + p + 8)
rr->rdata_offset = p + 10
se rdata_offset + rdlength > len → erro
retorna rdata_offset + rdlength
```

### 7.6 `extract_mx`

| | |
|---|---|
| **Objetivo** | Ler o RDATA de um registro MX: preferência + nome do servidor |
| **Assinatura** | `static int extract_mx(const uint8_t *msg, size_t len, const struct dns_rr *rr, uint16_t *pref, char *out, size_t outsize);` |
| **Parâmetros** | `msg`, `len`, `rr` (resposta já lida), `pref` (saída), `out`/`outsize` (saída do nome) |
| **Retorno** | `0` se ok, `-1` em erro |
| **Usa** | `read_u16`, `read_name` |
| **Usada por** | `dns_parse_mx` |

**Lógica:**
```
se rr->rdlength < 3 → erro
*pref = read_u16(msg + rr->rdata_offset)
read_name(msg, len, rr->rdata_offset + 2, out, outsize)
```

**Atenção:** passe o **pacote inteiro** (`msg`) para o `read_name`, não só o RDATA. O nome do servidor quase sempre tem ponteiros para outras partes do pacote.

---

## 8. Como testar sozinho

Você não depende dos outros módulos. Capture respostas reais e salve como arquivos de teste.

### 8.1 Capturando respostas com o Wireshark

1. Abra o Wireshark, filtre por `dns`.
2. Rode no terminal:
   ```
   dig +noedns unb.br MX @8.8.8.8
   dig +noedns fga.unb.br MX @8.8.8.8
   dig +noedns imagdaskdasdasj.br MX @8.8.8.8
   ```
3. Em cada pacote de **resposta**, clique com o botão direito na camada **Domain Name System** → *Copy* → *...as a Hex Stream*.
4. Cole num arquivo e converta para binário:
   ```
   echo "COLE_O_HEX_AQUI" | xxd -r -p > resp_unb.bin
   ```
5. Anote o ID de cada pacote (os 2 primeiros bytes). Você vai precisar dele no teste.

### 8.2 Programa de teste

```c
/* test_parse.c */
#include <stdio.h>
#include <stdlib.h>
#include "dns.h"

int main(int argc, char *argv[]) {
    if (argc != 3) { printf("uso: %s arquivo.bin id_hex\n", argv[0]); return 1; }
    uint8_t buf[DNS_MAX_PKT];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    size_t len = fread(buf, 1, sizeof buf, f);
    fclose(f);

    uint16_t id = (uint16_t)strtol(argv[2], NULL, 16);
    char mx[DNS_MAX_NAME];
    dns_status s = dns_parse_mx(buf, len, id, mx, sizeof mx);

    const char *nomes[] = { "DNS_OK", "DNS_NXDOMAIN", "DNS_NO_MX", "DNS_ERROR" };
    printf("status = %s\n", nomes[s]);
    if (s == DNS_OK) printf("mx = %s\n", mx);
    return 0;
}
```

Salve em `tests/test_parse.c`, guarde as fixtures em `tests/fixtures/` e compile a partir da raiz do repositório:
```
gcc -Wall -Isrc -o tests/test_parse tests/test_parse.c src/dns_parse.c
./tests/test_parse tests/fixtures/resp_unb.bin ABCD
```

### 8.3 Casos de teste

| Arquivo | Comando | Esperado |
|---|---|---|
| `resp_unb.bin` | `./test_parse resp_unb.bin <id>` | `DNS_OK`, `mx = unb-br.mail.protection.outlook.com` |
| `resp_fga.bin` | `./test_parse resp_fga.bin <id>` | `DNS_NO_MX` |
| `resp_nx.bin` | `./test_parse resp_nx.bin <id>` | `DNS_NXDOMAIN` |
| `resp_unb.bin` com ID errado | `./test_parse resp_unb.bin 0000` | `DNS_ERROR` |
| arquivo cortado ao meio | `head -c 20 resp_unb.bin > cortado.bin` | `DNS_ERROR` (sem travar!) |
| domínio com vários MX (ex.: `gmail.com`) | | o MX de **menor** preferência |

O teste do arquivo cortado é importante: o parser **nunca** pode ler além de `len`, senão o programa pode travar com *segmentation fault*.

---

## 9. Limitações conhecidas (repassar para o README)

- Respostas truncadas (bit TC = `0x0200` ligado) não são tratadas; o enunciado não pede fallback para TCP.
- Só o MX de menor preferência é exibido.
- Não há suporte a EDNS (pacotes maiores que 512 bytes).

---

## 10. Checklist de "pronto"

- [ ] `read_name` funciona com nomes normais, ponteiros e mistos
- [ ] `read_name` tem limite de saltos (sem loop infinito)
- [ ] Toda leitura confere os limites contra `len`
- [ ] Os 6 casos de teste da seção 8.3 passam
- [ ] Respostas CNAME (tipo ≠ 15) são ignoradas
- [ ] Compila sem warnings com `gcc -Wall -Wextra`
- [ ] Funções internas marcadas como `static`
- [ ] Limitações enviadas para quem está escrevendo o README
