#include "ip_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

// IP externo detectado, guardado em cache para as reverificações periódicas.
static char g_cached_ip[64] = {0};

typedef struct {
    char *data;
    size_t size;
} mem_buffer_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    mem_buffer_t *mem = (mem_buffer_t *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';

    return realsize;
}

static bool http_get(const char *url, mem_buffer_t *out, long timeout_seconds) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    out->data = malloc(1);
    if (!out->data) {
        curl_easy_cleanup(curl);
        return false;
    }
    out->size = 0;
    out->data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HSProxy-LicenseCheck/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
    // A API roda numa porta não padrão (2083); evitamos falsos negativos
    // de conexão por causa da cadeia de certificado não ser reconhecida.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    bool ok = (res == CURLE_OK);

    if (!ok) {
        fprintf(stderr, "⚠️  Erro ao acessar %s: %s\n", url, curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return ok;
}

bool get_external_ip(char *ip_out, size_t ip_out_len) {
    mem_buffer_t buf = {0};
    // Serviço simples que devolve apenas o IP em texto puro.
    bool ok = http_get("https://api.ipify.org", &buf, 8);

    if (!ok || buf.size == 0) {
        free(buf.data);
        return false;
    }

    size_t start = 0, end = buf.size;
    while (start < end && isspace((unsigned char)buf.data[start])) start++;
    while (end > start && isspace((unsigned char)buf.data[end - 1])) end--;

    size_t len = end - start;
    if (len == 0 || len >= ip_out_len) {
        free(buf.data);
        return false;
    }

    memcpy(ip_out, buf.data + start, len);
    ip_out[len] = '\0';

    free(buf.data);
    return true;
}

bool check_ip_allowed(const char *ip) {
    char url[256];
    snprintf(url, sizeof(url), "https://check.cloudjf.com.br:2083/ip=%s", ip);

    mem_buffer_t buf = {0};
    bool ok = http_get(url, &buf, 8);

    if (!ok) {
        free(buf.data);
        buf.data = NULL;
        buf.size = 0;

        // Se HTTPS falhar, tenta HTTP simples (a API pode estar em texto puro).
        snprintf(url, sizeof(url), "http://check.cloudjf.com.br:2083/ip=%s", ip);
        ok = http_get(url, &buf, 8);
        if (!ok) {
            free(buf.data);
            return false;
        }
    }

    bool allowed = (buf.data != NULL && strstr(buf.data, "\"exists\":true") != NULL);

    free(buf.data);
    return allowed;
}

bool verify_license_or_die(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    printf("🔎 Verificando autorização de IP...\n");

    if (!get_external_ip(g_cached_ip, sizeof(g_cached_ip))) {
        fprintf(stderr, "❌ Não foi possível determinar o IP externo desta máquina.\n");
        curl_global_cleanup();
        return false;
    }

    printf("🌐 IP externo detectado: %s\n", g_cached_ip);

    bool allowed = check_ip_allowed(g_cached_ip);

    if (!allowed) {
        fprintf(stderr, "\n❌ Este IP (%s) não está autorizado a executar o proxy.\n", g_cached_ip);
        fprintf(stderr, "👉 Para liberar o uso, ative este IP falando com @cloudjf2_bot\n\n");
        curl_global_cleanup();
        return false;
    }

    printf("✅ IP autorizado. Iniciando proxy...\n");
    return true;
}

static void periodic_check_cb(evutil_socket_t fd, short events, void *arg) {
    (void)fd;
    (void)events;
    struct event_base *base = (struct event_base *)arg;

    if (g_cached_ip[0] == '\0') return;

    if (!check_ip_allowed(g_cached_ip)) {
        fprintf(stderr, "\n❌ O IP (%s) deixou de estar autorizado. Encerrando o proxy.\n", g_cached_ip);
        fprintf(stderr, "👉 Para liberar o uso, ative este IP falando com @cloudjf2_bot\n\n");
        curl_global_cleanup();
        event_base_loopbreak(base);
        exit(1);
    }
}

void schedule_periodic_ip_check(struct event_base *base, int interval_seconds) {
    struct timeval tv = { interval_seconds, 0 };
    struct event *ev = event_new(base, -1, EV_PERSIST, periodic_check_cb, base);
    if (ev) {
        event_add(ev, &tv);
    }
}
