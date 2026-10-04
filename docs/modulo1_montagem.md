# Módulo 1 — Montagem da Consulta DNS

**Arquivo:** `src/dns_query.c`
**Responsável:** Desenvolvedor A
**Tarefa extra:** README do projeto (vale 20% da nota)

---

## 1. O que este módulo faz

Recebe um nome de domínio em texto (ex.: `"unb.br"`) e devolve um **array de bytes** pronto para ser enviado pela rede: o pacote de consulta DNS do tipo MX.

Este módulo **não** usa rede e **não** lê respostas. Ele só monta bytes.

```
main.c ──► [MÓDULO 1: monta consulta] ──► Módulo 2 (envia) ──► Módulo 3 (interpreta) ──► imprime
```

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

**Sua parte do contrato:** `dns_generate_id` e `dns_build_query`.

---

## 3. O pacote que você vai gerar

Exemplo para `unb.br` com ID `0xABCD` (24 bytes no total):

```
AB CD   01 00   00 01   00 00   00 00   00 00     ← cabeçalho (12 bytes)
 ID     Flags   QDCOUNT ANCOUNT NSCOUNT ARCOUNT

03 75 6E 62 02 62 72 00                            ← nome: 3"unb" 2"br" 0
00 0F                                              ← tipo MX (15)
00 01                                              ← classe IN (1)
```

Todos os números de 16 bits vão em **big-endian** (byte mais significativo primeiro).

---

## 4. Mapa de chamadas

```
main.c
  ├─► dns_generate_id()
  └─► dns_build_query()                 ← única porta de entrada do módulo
         ├─► write_header()
         │      └─► write_u16()
         └─► write_question()
                ├─► encode_name()
                └─► write_u16()
```

- **Públicas**: estão no `dns.h`, outros arquivos podem chamar.
- **Internas** (`static`): só existem dentro do `dns_query.c`.

---

## 5. Funções públicas

### 5.1 `dns_generate_id`

| | |
|---|---|
| **Objetivo** | Gerar o Transaction ID aleatório de 16 bits |
| **Assinatura** | `uint16_t dns_generate_id(void);` |
| **Parâmetros** | nenhum |
| **Retorno** | número entre 0 e 65535 |
| **Usa** | `rand()` (de `<stdlib.h>`) |
| **Usada por** | `main.c` |

**Atenção:** o `srand(time(NULL))` é chamado **uma única vez no `main`**, não aqui dentro.

O `main` guarda esse ID porque ele também é passado ao Módulo 3, que confere se a resposta é da nossa consulta.

### 5.2 `dns_build_query`

| | |
|---|---|
| **Objetivo** | Montar o pacote completo (cabeçalho + pergunta) |
| **Assinatura** | `int dns_build_query(const char *domain, uint16_t id, uint8_t *buf, size_t bufsize);` |
| **Parâmetros** | `domain`: nome em texto, ex. `"unb.br"`<br>`id`: ID gerado por `dns_generate_id`<br>`buf`: onde escrever os bytes<br>`bufsize`: tamanho de `buf` |
| **Retorno** | tamanho do pacote em bytes, ou `-1` em erro |
| **Usa** | `write_header`, `write_question` |
| **Usada por** | `main.c` (o retorno vai para `udp_query` do Módulo 2) |

**Lógica:**
```
n1 = write_header(buf, bufsize, id)
se n1 < 0 → retorna -1
n2 = write_question(domain, buf + n1, bufsize - n1)
se n2 < 0 → retorna -1
retorna n1 + n2
```

---

## 6. Funções internas (`static`)

### 6.1 `write_u16`

| | |
|---|---|
| **Objetivo** | Escrever um número de 16 bits em big-endian |
| **Assinatura** | `static void write_u16(uint8_t *p, uint16_t value);` |
| **Parâmetros** | `p`: posição no buffer; `value`: número |
| **Retorno** | nada (escreve 2 bytes em `p`) |
| **Usa** | nada |
| **Usada por** | `write_header`, `write_question` |

**Dica:**
```c
p[0] = (value >> 8) & 0xFF;   /* byte alto */
p[1] = value & 0xFF;          /* byte baixo */
```

### 6.2 `write_header`

| | |
|---|---|
| **Objetivo** | Escrever os 12 bytes do cabeçalho |
| **Assinatura** | `static int write_header(uint8_t *buf, size_t bufsize, uint16_t id);` |
| **Parâmetros** | `buf`, `bufsize`, `id` |
| **Retorno** | `12`, ou `-1` se `bufsize < 12` |
| **Usa** | `write_u16` (6 vezes) |
| **Usada por** | `dns_build_query` |

**Valores, nesta ordem:**

| Posição | Campo | Valor |
|---|---|---|
| 0–1 | ID | `id` |
| 2–3 | Flags | `DNS_FLAGS_RD` (`0x0100`) |
| 4–5 | QDCOUNT | `0x0001` |
| 6–7 | ANCOUNT | `0x0000` |
| 8–9 | NSCOUNT | `0x0000` |
| 10–11 | ARCOUNT | `0x0000` |

### 6.3 `encode_name`

| | |
|---|---|
| **Objetivo** | Converter `"unb.br"` em `03 'u' 'n' 'b' 02 'b' 'r' 00` |
| **Assinatura** | `static int encode_name(const char *domain, uint8_t *out, size_t outsize);` |
| **Parâmetros** | `domain`: nome em texto; `out`: destino; `outsize`: espaço disponível |
| **Retorno** | bytes escritos, ou `-1` se o nome for inválido ou não couber |
| **Usa** | `strlen`, `memcpy` (de `<string.h>`) |
| **Usada por** | `write_question` |

**Lógica sugerida:**
```
para cada parte separada por '.':
    tamanho = comprimento da parte
    se tamanho == 0 ou tamanho > 63 → erro
    escreve 1 byte com o tamanho
    copia as letras
escreve o byte 0 final
se total > 255 → erro
```

**Casos que precisa tratar:**

| Entrada | Resultado esperado |
|---|---|
| `"unb.br"` | 8 bytes: `03 75 6E 62 02 62 72 00` |
| `"unb.br."` (ponto final) | mesmo resultado acima (ignorar o ponto final) |
| `""` | `-1` |
| `"unb..br"` ou `".unb.br"` | `-1` (parte vazia) |
| parte com 64+ letras | `-1` (limite da RFC 1035) |
| nome codificado > 255 bytes | `-1` |
| buffer pequeno demais | `-1` |

### 6.4 `write_question`

| | |
|---|---|
| **Objetivo** | Escrever a seção de pergunta: nome + tipo + classe |
| **Assinatura** | `static int write_question(const char *domain, uint8_t *buf, size_t bufsize);` |
| **Parâmetros** | `domain`; `buf` já posicionado após o cabeçalho; `bufsize` = espaço restante |
| **Retorno** | bytes escritos, ou `-1` em erro |
| **Usa** | `encode_name`, `write_u16` |
| **Usada por** | `dns_build_query` |

**Lógica:**
```
n = encode_name(domain, buf, bufsize)
se n < 0 ou não sobram 4 bytes → -1
write_u16(buf + n,     DNS_TYPE_MX)
write_u16(buf + n + 2, DNS_CLASS_IN)
retorna n + 4
```

---

## 7. Como testar sozinho

Você não depende dos outros módulos. Crie um `test_query.c`:

```c
#include <stdio.h>
#include "dns.h"

int main(void) {
    uint8_t buf[DNS_MAX_PKT];
    int n = dns_build_query("unb.br", 0xABCD, buf, sizeof buf);
    printf("tamanho = %d\n", n);
    for (int i = 0; i < n; i++) printf("%02X ", buf[i]);
    printf("\n");
    return 0;
}
```

Salve em `tests/test_query.c` e compile a partir da raiz do repositório:
```
gcc -Wall -Isrc -o tests/test_query tests/test_query.c src/dns_query.c
./tests/test_query
```

**Saída esperada:**
```
tamanho = 24
AB CD 01 00 00 01 00 00 00 00 00 00 03 75 6E 62 02 62 72 00 00 0F 00 01
```

**Comparação com o mundo real:** abra o Wireshark, filtre por `dns`, rode `dig unb.br MX @8.8.8.8` e compare os bytes da consulta com os seus. Tudo deve bater, exceto o ID. (O `dig` pode adicionar uma seção extra chamada EDNS/OPT; nesse caso use `dig +noedns unb.br MX @8.8.8.8`.)

Teste também os casos de erro da tabela da seção 6.3.

---

## 8. Tarefa extra: README

O README vale **20% da nota**. O enunciado exige:

- [ ] Sistema operacional usado
- [ ] Ambiente de desenvolvimento (editor, versão do GCC)
- [ ] Como compilar (`make`)
- [ ] Como executar (`./meu_cliente <dominio> <servidor_dns>`)
- [ ] Telas / instruções de uso (os 4 exemplos de saída)
- [ ] Limitações conhecidas (colete com os colegas durante a integração)

---

## 9. Checklist de "pronto"

- [ ] `dns_generate_id` implementada
- [ ] `dns_build_query` gera os 24 bytes esperados para `unb.br`
- [ ] Bytes conferidos com o Wireshark
- [ ] Todos os casos de erro de `encode_name` retornam `-1`
- [ ] Compila sem warnings com `gcc -Wall -Wextra`
- [ ] Funções internas marcadas como `static`
- [ ] README finalizado
