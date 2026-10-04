/*
 * dns.h — Contrato compartilhado entre os módulos do cliente DNS.
 *
 * Este arquivo declara as funções públicas de cada módulo e as constantes
 * do protocolo. NÃO altere sem combinar com o trio.
 *
 *   Módulo 1 (dns_query.c) → monta o pacote de consulta
 *   Módulo 2 (net.c)       → envia e recebe via UDP
 *   Módulo 3 (dns_parse.c) → interpreta a resposta
 */

#ifndef DNS_H
#define DNS_H

#include <stdint.h>   /* uint8_t, uint16_t */
#include <stddef.h>   /* size_t */

/* ------------------------------------------------------------------ */
/* Constantes do protocolo (RFC 1035)                                  */
/* ------------------------------------------------------------------ */

#define DNS_PORT      53      /* porta padrão do serviço DNS (UDP) */
#define DNS_MAX_PKT   512     /* tamanho máximo de uma mensagem DNS via UDP */
#define DNS_MAX_NAME  256     /* tamanho máximo de um nome em texto (+ '\0') */

#define DNS_TYPE_MX   15      /* tipo de registro: Mail Exchanger */
#define DNS_CLASS_IN  1       /* classe: Internet */
#define DNS_FLAGS_RD  0x0100  /* flags da consulta: recursão desejada */

/* ------------------------------------------------------------------ */
/* Resultado da interpretação da resposta (Módulo 3 → main)            */
/* ------------------------------------------------------------------ */

typedef enum {
    DNS_OK,        /* encontrou o servidor MX */
    DNS_NXDOMAIN,  /* domínio não existe (RCODE = 3) */
    DNS_NO_MX,     /* domínio existe, mas não tem registro MX */
    DNS_ERROR      /* resposta inválida, corrompida ou com ID errado */
} dns_status;

/* ------------------------------------------------------------------ */
/* Módulo 1 — Montagem da consulta (dns_query.c)                       */
/* ------------------------------------------------------------------ */

/* Gera um Transaction ID aleatório de 16 bits.
 * O srand() deve ser chamado uma única vez no main. */
uint16_t dns_generate_id(void);

/* Monta a consulta MX para 'domain' dentro de 'buf'.
 * Retorna o tamanho do pacote em bytes, ou -1 se o nome for inválido
 * ou não couber no buffer. */
int dns_build_query(const char *domain, uint16_t id,
                    uint8_t *buf, size_t bufsize);

/* ------------------------------------------------------------------ */
/* Módulo 2 — Comunicação UDP (net.c)                                  */
/* ------------------------------------------------------------------ */

/* Envia 'query' para server_ip:53 e espera a resposta por 2 segundos,
 * tentando até 3 vezes. Retorna o tamanho da resposta gravada em 'resp',
 * ou -1 se o IP for inválido ou nenhuma resposta chegar. */
int udp_query(const char *server_ip,
              const uint8_t *query, size_t qlen,
              uint8_t *resp, size_t resp_size);

/* ------------------------------------------------------------------ */
/* Módulo 3 — Interpretação da resposta (dns_parse.c)                  */
/* ------------------------------------------------------------------ */

/* Interpreta a resposta e, se encontrar MX, copia o nome do servidor
 * de e-mail (o de menor preferência) para 'mx_out'. */
dns_status dns_parse_mx(const uint8_t *resp, size_t len,
                        uint16_t expected_id,
                        char *mx_out, size_t mx_size);

#endif /* DNS_H */