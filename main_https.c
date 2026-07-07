#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <getopt.h>

#include <event2/buffer.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>

#include "ip_check.h"

int PORT = 443;
int TO_PORT = 22;

// Intervalo (em segundos) entre reverificações de autorização de IP.
#define IP_RECHECK_INTERVAL_SECONDS 300

SSL_CTX *ssl_ctx;
struct event_base *base;

bool is_number(const char *str) {
    if (!str || *str == '\0') return false;
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || str == endptr) {
        return false;
    }
    return val >= 1 && val <= 65535;
}

void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX *create_context() {
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, "/opt/magnumproxy/cert.pem", SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, "/opt/magnumproxy/key.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

typedef struct {
    struct bufferevent *client_bev;
    struct bufferevent *remote_bev;
} proxy_ctx_t;

void proxy_ctx_free(proxy_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->client_bev) bufferevent_free(ctx->client_bev);
    if (ctx->remote_bev) bufferevent_free(ctx->remote_bev);
    free(ctx);
}

void event_cb(struct bufferevent *bev, short events, void *ctx_ptr) {
    proxy_ctx_t *ctx = ctx_ptr;
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        proxy_ctx_free(ctx);
    }
}

void remote_read_cb(struct bufferevent *bev, void *ctx_ptr) {
    proxy_ctx_t *ctx = (proxy_ctx_t *)ctx_ptr;
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(ctx->client_bev);
    evbuffer_add_buffer(output, input);
}

void client_read_cb(struct bufferevent *bev, void *ctx_ptr) {
    proxy_ctx_t *ctx = (proxy_ctx_t *)ctx_ptr;
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(ctx->remote_bev);
    evbuffer_add_buffer(output, input);
}

void on_accept(evutil_socket_t listener, short event, void *arg) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(listener, (struct sockaddr *)&client_addr, &len);
    if (client_fd < 0) return;

    SSL *ssl = SSL_new(ssl_ctx);
    struct bufferevent *bev_ssl = bufferevent_openssl_socket_new(base, client_fd, ssl,
                                                                 BUFFEREVENT_SSL_ACCEPTING,
                                                                 BEV_OPT_CLOSE_ON_FREE);

    int remote_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in remote_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TO_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };

    if (connect(remote_fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        close(remote_fd);
        bufferevent_free(bev_ssl);
        return;
    }

    struct bufferevent *bev_remote = bufferevent_socket_new(base, remote_fd, BEV_OPT_CLOSE_ON_FREE);

    proxy_ctx_t *ctx = calloc(1, sizeof(proxy_ctx_t));
    ctx->client_bev = bev_ssl;
    ctx->remote_bev = bev_remote;

    bufferevent_setcb(bev_ssl, client_read_cb, NULL, event_cb, ctx);
    bufferevent_setcb(bev_remote, remote_read_cb, NULL, event_cb, ctx);

    bufferevent_enable(bev_ssl, EV_READ | EV_WRITE);
    bufferevent_enable(bev_remote, EV_READ | EV_WRITE);
}

void print_usage(const char *progname) {
    printf("Uso: %s [opções]\n", progname);
    printf("Opções:\n");
    printf("  -p, --port <número>     Porta de escuta (padrão: 443)\n");
    printf("  -t, --to <número>       Porta de destino (padrão: 22)\n");
    printf("  -h, --help              Mostra esta ajuda\n");
    printf("\nExemplos:\n");
    printf("  %s --port 8443\n", progname);
    printf("  %s -port 9443 --to 80\n", progname);
    printf("  %s -p 8443 -t 22\n", progname);
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"to",   required_argument, 0, 't'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:t:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                if (!is_number(optarg) || (PORT = atoi(optarg)) < 1 || PORT > 65535) {
                    fprintf(stderr, "❌ Porta de escuta inválida: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                if (!is_number(optarg) || (TO_PORT = atoi(optarg)) < 1 || TO_PORT > 65535) {
                    fprintf(stderr, "❌ Porta de destino inválida: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case '?':
                print_usage(argv[0]);
                return 1;
            default:
                break;
        }
    }

    // --- Verificação de licença por IP ---
    // O proxy só inicia se o IP externo desta máquina estiver autorizado.
    if (!verify_license_or_die()) {
        return 1;
    }

    printf("🔐 SSL Proxy iniciado: %d → %d\n", PORT, TO_PORT);

    init_openssl();
    ssl_ctx = create_context();
    configure_context(ssl_ctx);
    base = event_base_new();

    int listener_fd = -1;
    int ipv6_available = 0;

    int test_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        int off = 0;
        if (setsockopt(test_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) == 0) {
            ipv6_available = 1;
        }
        close(test_fd);
    }

    if (ipv6_available) {
        listener_fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (listener_fd >= 0) {
            int optval = 1;
            setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
            int off = 0;
            setsockopt(listener_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

            struct sockaddr_in6 addr6 = {
                .sin6_family = AF_INET6,
                .sin6_port = htons(PORT),
                .sin6_addr = in6addr_any
            };

            if (bind(listener_fd, (struct sockaddr *)&addr6, sizeof(addr6)) == 0) {
                printf("✅ Dual Stack (IPv4+IPv6) na porta %d\n", PORT);
            } else {
                close(listener_fd);
                listener_fd = -1;
                ipv6_available = 0;
            }
        }
    }

    if (!ipv6_available) {
        listener_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_fd < 0) {
            fprintf(stderr, "❌ Falha ao criar socket\n");
            goto cleanup;
        }

        int optval = 1;
        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(PORT),
            .sin_addr.s_addr = INADDR_ANY,
        };

        if (bind(listener_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            fprintf(stderr, "❌ Falha ao bind na porta %d\n", PORT);
            goto cleanup;
        }
        printf("✅ IPv4 na porta %d\n", PORT);
    }

    if (listen(listener_fd, 16) != 0) {
        fprintf(stderr, "❌ Falha no listen\n");
        goto cleanup;
    }

    struct event *listener_event = event_new(base, listener_fd, EV_READ | EV_PERSIST, on_accept, NULL);
    event_add(listener_event, NULL);

    // Reverifica periodicamente se o IP continua autorizado.
    // Se deixar de estar, o processo é encerrado automaticamente.
    schedule_periodic_ip_check(base, IP_RECHECK_INTERVAL_SECONDS);

    printf("🚀 Aguardando conexões na porta %d...\n", PORT);
    event_base_dispatch(base);

cleanup:
    if (listener_fd >= 0) close(listener_fd);
    if (base) event_base_free(base);
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    cleanup_openssl();
    return 0;
}
