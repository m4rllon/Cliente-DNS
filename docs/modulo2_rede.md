# Módulo 2 — Rede (UDP) e Programa Principal

**Arquivos:** `src/net.c` e `src/main.c`
**Responsável:** Desenvolvedor B
**Tarefa extra:** `Makefile`

---

## 1. O que este módulo faz

Duas responsabilidades:

1. **`net.c`**: pega os bytes da consulta, envia via UDP para o servidor DNS (porta 53), espera a resposta por **2 segundos** e tenta **até 3 vezes**.
2. **`main.c`**: o "maestro". Lê os argumentos da linha de comando, chama os três módulos na ordem certa e imprime o resultado no formato do enunciado.

Este módulo **não** monta o pacote e **não** interpreta a resposta. Para ele, são só bytes.

```
main.c ──► Módulo 1 (monta) ──► [MÓDULO 2: envia/recebe] ──► Módulo 3 (interpreta) ──► [main imprime]
```

**Restrição do enunciado:** só é permitido usar a biblioteca de **socket UDP** (`<sys/socket.h>`). Nada de `getaddrinfo`, `gethostbyname`, `res_query` ou qualquer biblioteca de DNS.

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

**Sua parte do contrato:** `udp_query`. Você também é o **único que chama** as funções dos outros dois módulos (no `main.c`).

---

## 3. Mapa de chamadas

```
main()                                   [main.c]
  ├─► print_usage()                      (se argumentos errados)
  ├─► dns_generate_id()                  → Módulo 1
  ├─► dns_build_query()                  → Módulo 1
  ├─► udp_query()                        [net.c]
  │      ├─► build_server_addr()
  │      ├─► create_udp_socket()
  │      └─► send_and_receive()          (até 3 vezes)
  └─► dns_parse_mx()                     → Módulo 3
```

- **Públicas**: estão no `dns.h`.
- **Internas** (`static`): só existem dentro do próprio arquivo.

Constantes internas do `net.c` (não vão para o `dns.h`):
```c
#define UDP_TIMEOUT_SEC  2
#define UDP_MAX_TRIES    3
```

---

## 4. `net.c` — Função pública

### 4.1 `udp_query`

| | |
|---|---|
| **Objetivo** | Enviar a consulta e receber a resposta, com timeout de 2s e até 3 tentativas |
| **Assinatura** | `int udp_query(const char *server_ip, const uint8_t *query, size_t qlen, uint8_t *resp, size_t resp_size);` |
| **Parâmetros** | `server_ip`: IP em texto, ex. `"8.8.8.8"`<br>`query`: bytes vindos do Módulo 1<br>`qlen`: tamanho da consulta<br>`resp`: onde guardar a resposta<br>`resp_size`: tamanho de `resp` |
| **Retorno** | tamanho da resposta em bytes, ou `-1` se: IP inválido, erro de socket ou 3 tentativas sem resposta |
| **Usa** | `build_server_addr`, `create_udp_socket`, `send_and_receive`, `close` |
| **Usada por** | `main.c` |

**Lógica:**
```
se build_server_addr falhar → -1
fd = create_udp_socket()
se fd < 0 → -1
para tentativa de 1 até 3:
    n = send_and_receive(...)
    se n > 0 → close(fd); retorna n
close(fd)
retorna -1
```

**Importante:** as retentativas reenviam **o mesmo pacote** (mesmo ID). Não gere um ID novo a cada tentativa.

---

## 5. `net.c` — Funções internas (`static`)

### 5.1 `build_server_addr`

| | |
|---|---|
| **Objetivo** | Converter o IP em texto para a estrutura que o socket entende |
| **Assinatura** | `static int build_server_addr(const char *ip, struct sockaddr_in *addr);` |
| **Parâmetros** | `ip`: texto, ex. `"8.8.8.8"`; `addr`: estrutura a preencher |
| **Retorno** | `0` se ok, `-1` se o IP for inválido |
| **Usa** | `memset`, `htons`, `inet_pton` (de `<arpa/inet.h>`) |
| **Usada por** | `udp_query` |

**Dica:**
```c
memset(addr, 0, sizeof *addr);
addr->sin_family = AF_INET;
addr->sin_port   = htons(DNS_PORT);
if (inet_pton(AF_INET, ip, &addr->sin_addr) != 1) return -1;
```
`inet_pton` só converte texto em número, não faz consulta DNS. Por isso é permitido.

### 5.2 `create_udp_socket`

| | |
|---|---|
| **Objetivo** | Criar o socket UDP já configurado com timeout de 2s na leitura |
| **Assinatura** | `static int create_udp_socket(void);` |
| **Parâmetros** | nenhum |
| **Retorno** | descritor do socket (número ≥ 0), ou `-1` em erro |
| **Usa** | `socket(AF_INET, SOCK_DGRAM, 0)`, `setsockopt` com `SO_RCVTIMEO` |
| **Usada por** | `udp_query` |

**Dica:** o timeout é configurado assim:
```c
struct timeval tv = { .tv_sec = UDP_TIMEOUT_SEC, .tv_usec = 0 };
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
```
Com isso, `recvfrom` desiste sozinho depois de 2 segundos, sem precisar de `select` ou threads.

### 5.3 `send_and_receive`

| | |
|---|---|
| **Objetivo** | Fazer **uma** tentativa: enviar e esperar a resposta |
| **Assinatura** | `static int send_and_receive(int fd, const struct sockaddr_in *addr, const uint8_t *query, size_t qlen, uint8_t *resp, size_t resp_size);` |
| **Parâmetros** | `fd`: socket; `addr`: servidor; demais iguais a `udp_query` |
| **Retorno** | bytes recebidos, ou `-1` se deu timeout ou erro |
| **Usa** | `sendto`, `recvfrom` |
| **Usada por** | `udp_query` |

**Dica:** quando o timeout estoura, `recvfrom` retorna `-1` e `errno` fica `EAGAIN` ou `EWOULDBLOCK`. Para esta função, timeout e erro dão o mesmo resultado: `-1`, e o `udp_query` tenta de novo.

**Includes necessários no `net.c`:**
```c
#include <string.h>
#include <unistd.h>       /* close */
#include <sys/socket.h>
#include <sys/time.h>     /* struct timeval */
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dns.h"
```

---

## 6. `main.c`

### 6.1 `main`

| | |
|---|---|
| **Objetivo** | Coordenar o programa inteiro e imprimir o resultado |
| **Assinatura** | `int main(int argc, char *argv[]);` |
| **Parâmetros** | `argv[1]`: domínio; `argv[2]`: IP do servidor DNS |
| **Retorno** | `0` se achou o MX, `1` em qualquer falha |
| **Usa** | `print_usage`, `srand`, `dns_generate_id`, `dns_build_query`, `udp_query`, `dns_parse_mx` |
| **Usada por** | o sistema operacional |

**Lógica:**
```
se argc != 3 → print_usage; retorna 1
srand(time(NULL))
id   = dns_generate_id()
qlen = dns_build_query(argv[1], id, query, sizeof query)
se qlen < 0 → imprime "Nome de dominio invalido: X"; retorna 1
rlen = udp_query(argv[2], query, qlen, resp, sizeof resp)
se rlen < 0 → imprime mensagem de falha de coleta; retorna 1
status = dns_parse_mx(resp, rlen, id, mx, sizeof mx)
conforme status → imprime a mensagem correspondente
```

### 6.2 `print_usage`

| | |
|---|---|
| **Objetivo** | Mostrar como usar o programa quando os argumentos estão errados |
| **Assinatura** | `static void print_usage(const char *prog);` |
| **Parâmetros** | `prog`: nome do programa (`argv[0]`) |
| **Retorno** | nada |
| **Usa** | `fprintf(stderr, ...)` |
| **Usada por** | `main` |

Exemplo: `Uso: ./meu_cliente <dominio> <ip_servidor_dns>`

### 6.3 Mensagens de saída (copiar exatamente do enunciado)

| Situação | Origem | Mensagem |
|---|---|---|
| Sucesso | `DNS_OK` | `unb.br <> unb-br.mail.protection.outlook.com` |
| Domínio não existe | `DNS_NXDOMAIN` | `Dominio X nao encontrado` |
| Sem registro MX | `DNS_NO_MX` | `Dominio X nao possui entrada MX` |
| Servidor não respondeu | `udp_query` retornou `-1` | `Nao foi possível coletar entrada MX para X` |
| Resposta corrompida | `DNS_ERROR` | mesma mensagem da linha acima |

Formatos de `printf`:
```c
printf("%s <> %s\n", dominio, mx);
printf("Dominio %s nao encontrado\n", dominio);
printf("Dominio %s nao possui entrada MX\n", dominio);
printf("Nao foi possível coletar entrada MX para %s\n", dominio);
```

---

## 7. Tarefa extra: `Makefile`

O código-fonte fica em `src/`; o Makefile e o executável ficam na raiz.

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -O2 -Isrc
SRC     = src/main.c src/dns_query.c src/net.c src/dns_parse.c
OBJS    = $(SRC:.c=.o)
BIN     = meu_cliente
ZIPNAME = nome_sobrenome_matricula_nome_sobrenome_matricula_trab01.zip

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

src/%.o: src/%.c src/dns.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(BIN)

zip: clean
	rm -f $(ZIPNAME)
	zip -r $(ZIPNAME) src Makefile README.md

.PHONY: clean zip
```

- `make` compila e gera `./meu_cliente` na raiz.
- `make clean` apaga os `.o` e o executável.
- `make zip` gera o pacote de entrega só com `src/`, `Makefile` e `README.md`.

**Atenção:** as linhas de comando do Makefile (abaixo de cada alvo) precisam começar com **TAB**, não com espaços. Usamos `-std=gnu11` em vez de `-std=c11` porque o padrão puro esconde funções POSIX como `inet_pton`.

---

## 8. Como testar sozinho

Você não precisa esperar os outros módulos. Use uma consulta **fixa** no código (ela é exatamente o que o Módulo 1 vai gerar para `unb.br`):

```c
/* test_net.c */
#include <stdio.h>
#include "dns.h"

int main(int argc, char *argv[]) {
    uint8_t query[] = {
        0xAB,0xCD, 0x01,0x00, 0x00,0x01, 0x00,0x00, 0x00,0x00, 0x00,0x00,
        0x03,'u','n','b', 0x02,'b','r', 0x00,
        0x00,0x0F, 0x00,0x01
    };
    uint8_t resp[DNS_MAX_PKT];
    const char *ip = (argc > 1) ? argv[1] : "8.8.8.8";
    int n = udp_query(ip, query, sizeof query, resp, sizeof resp);
    printf("recebidos = %d bytes\n", n);
    return 0;
}
```

Salve em `tests/test_net.c` e compile a partir da raiz do repositório:
```
gcc -Wall -Isrc -o tests/test_net tests/test_net.c src/net.c
```

| Teste | Resultado esperado |
|---|---|
| `./tests/test_net 8.8.8.8` | algumas dezenas de bytes, quase instantâneo |
| `./tests/test_net 1.2.3.4` | `-1` depois de **~6 segundos** (3 × 2s) |
| `./tests/test_net abc` | `-1` imediato (IP inválido) |

**Sobre firewalls:** algumas redes (inclusive a da universidade) bloqueiam DNS para servidores externos como `8.8.8.8`. Se der timeout sempre, teste em outra rede ou use o servidor DNS da própria rede (veja em `/etc/resolv.conf`).

Depois que os módulos 1 e 3 estiverem prontos, teste o programa completo com os 4 exemplos do enunciado.

---

## 9. Checklist de "pronto"

- [ ] `udp_query` recebe resposta de `8.8.8.8`
- [ ] Timeout de 2s funcionando (3 tentativas ≈ 6s com `1.2.3.4`)
- [ ] IP inválido retorna `-1` sem travar
- [ ] Socket sempre fechado com `close`, inclusive nos caminhos de erro
- [ ] `main.c` imprime as 4 mensagens exatamente como no enunciado
- [ ] Argumentos errados mostram o uso correto
- [ ] `Makefile` compila tudo com `make`, limpa com `make clean` e gera a entrega com `make zip`
- [ ] Compila sem warnings com `-Wall -Wextra`
