#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <curl/curl.h>

// --- Configurações Padrão ---
int PORT = 80;
int TO_PORT = 22;
char* RESPONSE = "OK"; // Payload padrão para ambas as respostas

// Estrutura de contexto para cada par de conexões (cliente <-> remoto)
typedef struct {
    struct bufferevent *client;
    struct bufferevent *remote;
    bool connected_to_remote;
    bool ip_verified;  // Nova flag para controle
} proxy_ctx_t;

struct event_base *base;

// Função para callback do CURL
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("Sem memória suficiente\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Função para verificar IP na API
bool check_ip_authorization(const char *ip) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    char url[256];
    snprintf(url, sizeof(url), "https://check.cloudjf.com.br:2083/ip=%s", ip);

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if(!curl_handle) {
        free(chunk.memory);
        return false;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl_handle);

    bool authorized = false;
    if(res == CURLE_OK) {
        // Procura por "exists":true no JSON
        if(strstr(chunk.memory, "\"exists\":true") != NULL) {
            authorized = true;
        } else if(strstr(chunk.memory, "\"exists\":false") != NULL) {
            authorized = false;
        }
    }

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    free(chunk.memory);

    return authorized;
}

// Protótipos
void client_read_cb(struct bufferevent *bev, void *user_data);
void remote_read_cb(struct bufferevent *bev, void *user_data);
void event_cb(struct bufferevent *bev, short events, void *user_data);
void accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx);

void free_proxy_ctx(proxy_ctx_t *ctx) {
    if (ctx) {
        if (ctx->client) bufferevent_free(ctx->client);
        if (ctx->remote) bufferevent_free(ctx->remote);
        free(ctx);
    }
}

void remote_read_cb(struct bufferevent *bev, void *user_data) {
    proxy_ctx_t *ctx = user_data;
    struct evbuffer *input = bufferevent_get_input(bev);
    if (ctx && ctx->client) {
        struct evbuffer *output = bufferevent_get_output(ctx->client);
        evbuffer_add_buffer(output, input);
    }
}

// --- FUNÇÃO client_read_cb CORRIGIDA COM VERIFICAÇÃO DE IP ---
void client_read_cb(struct bufferevent *bev, void *user_data) {
    proxy_ctx_t *ctx = user_data;
    struct evbuffer *input = bufferevent_get_input(bev);

    // VERIFICAÇÃO DE IP (executada apenas uma vez por conexão)
    if (!ctx->ip_verified) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getpeername(bufferevent_getfd(bev), (struct sockaddr*)&addr, &addr_len) == 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
            
            printf("Verificando IP: %s\n", client_ip);
            
            if (!check_ip_authorization(client_ip)) {
                printf("IP %s não autorizado! Encerrando conexão.\n", client_ip);
                // Envia mensagem de erro
                char error_msg[] = "HTTP/1.1 403 Forbidden\r\n\r\nIP não autorizado. Ative em @cloudjf2_bot";
                bufferevent_write(bev, error_msg, strlen(error_msg));
                bufferevent_flush(bev, EV_WRITE, BEV_FLUSH);
                // Encerra a conexão após enviar a mensagem
                free_proxy_ctx(ctx);
                return;
            }
            
            printf("IP %s autorizado! Continuando...\n", client_ip);
            ctx->ip_verified = true;
        } else {
            // Não foi possível obter o IP, encerra a conexão
            free_proxy_ctx(ctx);
            return;
        }
    }

    // Se já estiver conectado ao remoto, repassa tudo normalmente
    if (ctx->connected_to_remote && ctx->remote) {
        struct evbuffer *output = bufferevent_get_output(ctx->remote);
        evbuffer_add_buffer(output, input);
        return;
    }

    // 1. VERIFICAÇÃO PRIORITÁRIA: npv-x
    struct evbuffer_ptr p_npv = evbuffer_search(input, "npv-x", 5, NULL);
    if (p_npv.pos != -1) {
        // Envia 200 OK
        char response_buffer_200[256];
        snprintf(response_buffer_200, sizeof(response_buffer_200),
                 "HTTP/1.1 200 %s\r\n\r\n", RESPONSE);
        bufferevent_write(bev, response_buffer_200, strlen(response_buffer_200));
        bufferevent_flush(bev, EV_WRITE, BEV_FLUSH);

        // Drena TODO o buffer atual (conexão limpa)
        evbuffer_drain(input, evbuffer_get_length(input));

        // Inicia conexão remota
        ctx->connected_to_remote = true;
        bufferevent_set_timeouts(bev, NULL, NULL);

        ctx->remote = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
        if (!ctx->remote) { free_proxy_ctx(ctx); return; }

        bufferevent_setcb(ctx->remote, remote_read_cb, NULL, event_cb, ctx);
        bufferevent_enable(ctx->remote, EV_READ | EV_WRITE);

        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(TO_PORT);
        remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        bufferevent_socket_connect(ctx->remote,
                                   (struct sockaddr *)&remote_addr,
                                   sizeof(remote_addr));
        return;
    }

    // 2. VERIFICAÇÃO SECUNDÁRIA: handshake SSH ("SSH-")
    struct evbuffer_ptr ssh_ptr = evbuffer_search(input, "SSH-", 4, NULL);
    if (ssh_ptr.pos == -1) {
        return;  // AGUARDA MAGICAMENTE!
    }

    // "SSH-" encontrado! (exatamente como você tinha)
    char response_buffer_200[256];
    snprintf(response_buffer_200, sizeof(response_buffer_200), "HTTP/1.1 200 %s\r\n\r\n", RESPONSE);
    bufferevent_write(bev, response_buffer_200, strlen(response_buffer_200));
    bufferevent_flush(bev, EV_WRITE, BEV_FLUSH);

    // Drena o lixo antes do "SSH-" (SUA LÓGICA GENIAL)
    evbuffer_drain(input, ssh_ptr.pos);

    // Continua para conectar ao servidor remoto (igual)
    ctx->connected_to_remote = true;
    bufferevent_set_timeouts(bev, NULL, NULL);

    ctx->remote = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!ctx->remote) {
        fprintf(stderr, "Erro ao criar bufferevent remoto\n");
        free_proxy_ctx(ctx);
        return;
    }

    bufferevent_setcb(ctx->remote, remote_read_cb, NULL, event_cb, ctx);
    bufferevent_enable(ctx->remote, EV_READ | EV_WRITE);

    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(TO_PORT);
    remote_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bufferevent_socket_connect(ctx->remote, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        fprintf(stderr, "Erro ao iniciar conexão remota\n");
        free_proxy_ctx(ctx);
    }
}

void event_cb(struct bufferevent *bev, short events, void *user_data) {
    proxy_ctx_t *ctx = user_data;

    if (events & BEV_EVENT_CONNECTED) {
        if (bev == ctx->remote) {
            // Conectado ao localhost.
            // Se sobrou algo no buffer (caso SSH- ou fallback), envia agora.
            struct evbuffer *client_input = bufferevent_get_input(ctx->client);
            if (evbuffer_get_length(client_input) > 0) {
                bufferevent_write_buffer(ctx->remote, client_input);
            }
        }
        return;
    }

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        if (events & BEV_EVENT_ERROR && errno != ECONNRESET) {
            // Opcional: log de erro
        }
        free_proxy_ctx(ctx);
    } else if (events & BEV_EVENT_TIMEOUT) {
        free_proxy_ctx(ctx);
    }
}

void accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
                    struct sockaddr *address, int socklen, void *ctx_arg) {
    struct event_base *base_local = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(base_local, fd,
                                                     BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        fprintf(stderr, "Erro ao criar bufferevent\n");
        evutil_closesocket(fd);
        return;
    }

    struct sockaddr_in *addr_in = (struct sockaddr_in *)address;
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);
    int port = ntohs(addr_in->sin_port);
    printf("Nova conexão de %s:%d\n", ip, port);

    proxy_ctx_t *proxy_ctx = calloc(1, sizeof(proxy_ctx_t));
    if (!proxy_ctx) {
        fprintf(stderr, "Sem memória\n");
        bufferevent_free(bev);
        return;
    }
    proxy_ctx->client = bev;
    proxy_ctx->remote = NULL;
    proxy_ctx->connected_to_remote = false;
    proxy_ctx->ip_verified = false;  // Nova flag

    // Envia a resposta HTTP 101 imediatamente.
    char response_buffer_101[256];
    snprintf(response_buffer_101, sizeof(response_buffer_101),
             "HTTP/1.1 101 %s\r\n\r\n", RESPONSE);
    bufferevent_write(bev, response_buffer_101, strlen(response_buffer_101));
    bufferevent_flush(bev, EV_WRITE, BEV_FLUSH);

    bufferevent_setcb(bev, client_read_cb, NULL, event_cb, proxy_ctx);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    // Timeout mais curto só para não ficar pendurado.
    struct timeval tv = {10, 0};
    bufferevent_set_timeouts(bev, &tv, &tv);
}

// --- Funções Utilitárias ---

void print_usage_and_exit(const char *prog) {
    fprintf(stderr, "Uso: %s [--to-port <porta>] [--port <porta>] [--response <texto>]\n", prog);
    exit(1);
}

// Verificação de numero
bool is_number(const char *str) {
    char *endptr;
    errno = 0;

    const long val = strtol(str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || str == endptr) {
        return false;
    }

    return val >= 1 && val <= 65535;
}

// --- Função Principal ---
int main(int argc, char *argv[]) {
    // Parsing de argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--to-port") == 0) {
            if (i + 1 < argc && is_number(argv[i + 1])) TO_PORT = atoi(argv[++i]);
            else print_usage_and_exit(argv[0]);
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc && is_number(argv[i + 1])) PORT = atoi(argv[++i]);
            else print_usage_and_exit(argv[0]);
        } else if (strcmp(argv[i], "--response") == 0) {
            if (i + 1 < argc) RESPONSE = argv[++i];
            else print_usage_and_exit(argv[0]);
        }
    }

    signal(SIGPIPE, SIG_IGN);

    base = event_base_new();
    if (!base) {
        fprintf(stderr, "Não foi possível inicializar a libevent\n");
        return 1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(PORT);

    struct evconnlistener *listener = evconnlistener_new_bind(
        base, accept_conn_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        -1, (struct sockaddr*)&sin, sizeof(sin)
    );

    if (!listener) {
        fprintf(stderr, "Não foi possível criar o listener na porta %d\n", PORT);
        event_base_free(base);
        return 1;
    }

    printf("Servidor iniciado na porta %d, encaminhando para 127.0.0.1:%d...\n",
           PORT, TO_PORT);
    event_base_dispatch(base);

    evconnlistener_free(listener);
    event_base_free(base);
    return 0;
}
