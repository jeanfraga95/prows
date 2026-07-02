#!/bin/bash

# =============================================
# HSProxy - Menu Melhorado (SSL com --port e --to)
# =============================================

HTTP_BIN="/usr/local/bin/hsproxy-http"
SSL_BIN="/usr/local/bin/hsproxy-ssl"

show_menu() {
    clear
    echo "=========================================="
    echo "       HSProxy - Gerenciador de Portas     "
    echo "=========================================="
    echo "1. Abrir Porta HTTP"
    echo "2. Abrir Porta HTTPS (SSL)"
    echo "3. Fechar Porta"
    echo "4. Ver Portas Abertas"
    echo "5. Status das Conexões"
    echo "6. Sair"
    echo "=========================================="
    read -p "Escolha uma opção [1-6]: " option
}

list_open_ports() {
    echo "• SERVICOS EM EXECUCAO •"
    echo "========================================"

    ps aux | grep -E "hsproxy-(http|ssl)" | grep -v grep | while read -r line; do
        cmd=$(echo "$line" | awk '{for(i=11;i<=NF;i++) printf $i " "}')
        port=$(echo "$cmd" | grep -oE '--port ([0-9]+)' | awk '{print $2}')
        [[ -z "$port" ]] && port=$(echo "$cmd" | grep -oE '[0-9]+$' || echo "??")
        echo "SERVICO: hsproxy PORTA: $port"
    done

    echo "========================================"
}

open_http_port() {
    read -p "Digite a porta HTTP (ex: 80): " port
    read -p "Porta destino SSH [22]: " to_port
    to_port=${to_port:-22}

    service_name="hsproxy-http-$port"

    cat > /etc/systemd/system/$service_name.service << EOF
[Unit]
Description=HSProxy HTTP on port $port
After=network.target

[Service]
Type=simple
ExecStart=$HTTP_BIN --port $port --to-port $to_port
Restart=always
RestartSec=3
User=root
EOF

    systemctl daemon-reload
    systemctl enable --now $service_name >/dev/null 2>&1

    if systemctl is-active --quiet $service_name; then
        echo "✅ Porta HTTP $port aberta com sucesso!"
    else
        echo "❌ Falha ao abrir porta HTTP $port"
    fi
}

# ==================== ABRIR HTTPS (SSL) ====================
open_https_port() {
    read -p "Digite a porta HTTPS (ex: 443): " port
    read -p "Porta destino (SSH) [22]: " to_port
    to_port=${to_port:-22}

    service_name="hsproxy-ssl-$port"

    cat > /etc/systemd/system/$service_name.service << EOF
[Unit]
Description=HSProxy HTTPS on port $port → $to_port
After=network.target

[Service]
Type=simple
ExecStart=$SSL_BIN --port $port --to $to_port
Restart=always
RestartSec=3
User=root
EOF

    systemctl daemon-reload
    systemctl enable --now $service_name >/dev/null 2>&1

    sleep 1.5
    if systemctl is-active --quiet $service_name; then
        echo "✅ Porta HTTPS $port aberta com sucesso! (→ $to_port)"
    else
        echo "❌ Falha ao abrir porta HTTPS $port"
        echo "Logs:"
        journalctl -u $service_name --no-pager -n 15
    fi
}

close_port() {
    read -p "Digite a porta para fechar: " port

    for service in $(systemctl list-units --type=service | grep -E "hsproxy-(http|ssl)-.*$port" | awk '{print $1}'); do
        echo "Parando: $service"
        systemctl stop "$service" 2>/dev/null
        systemctl disable "$service" 2>/dev/null
        rm -f "/etc/systemd/system/$service"
    done

    systemctl daemon-reload
    echo "✅ Tentativa de fechamento da porta $port concluída."
}

show_status() {
    echo "=== Status das Conexões HSProxy ==="
    journalctl -u "hsproxy-*" --no-pager -n 30 --since "1 hour ago" 2>/dev/null || echo "Sem logs."
    echo ""
    ps aux | grep -E "hsproxy" | grep -v grep
}

main() {
    while true; do
        show_menu
        case $option in
            1) open_http_port ;;
            2) open_https_port ;;
            3) close_port ;;
            4) list_open_ports ;;
            5) show_status ;;
            6) echo "Saindo..."; exit 0 ;;
            *) echo "Opção inválida!" ;;
        esac
        echo ""
        read -p "[Enter] para voltar ao menu..."
    done
}

main
