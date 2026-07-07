#ifndef IP_CHECK_H
#define IP_CHECK_H

#include <stdbool.h>
#include <event2/event.h>

// Obtém o IP externo desta máquina.
// Retorna true em caso de sucesso e preenche ip_out (buffer >= 64 bytes).
bool get_external_ip(char *ip_out, size_t ip_out_len);

// Verifica junto à API se o IP informado está autorizado a executar o proxy.
// Em caso de erro de rede, é tratado como NÃO autorizado (fail-closed).
bool check_ip_allowed(const char *ip);

// Detecta o IP externo, consulta a API e imprime mensagem de erro se
// o IP não estiver autorizado. Retorna true se o proxy pode iniciar.
bool verify_license_or_die(void);

// Agenda uma reverificação periódica em background. Se o IP deixar de
// estar autorizado durante a execução, o processo é encerrado (exit).
void schedule_periodic_ip_check(struct event_base *base, int interval_seconds);

#endif // IP_CHECK_H
