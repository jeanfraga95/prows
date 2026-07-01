#!/bin/bash

# =============================================
# HSProxy - Menu de Gerenciamento de Portas
# =============================================

CONFIG_DIR="/etc/hsproxy"
LOG_DIR="/var/log/hsproxy"
mkdir -p "$CONFIG_DIR" "$LOG_DIR"

HTTP_BIN="/usr/local/bin/hsproxy-http"
SSL_BIN="/usr/local/bin/hsproxy-ssl"

show_menu() {
    clear
    echo "=========================================="
    echo "       HSProxy - Gerenciador de Portas     "
    echo "=========================================="
    echo "1. Abrir Porta HTTP (comum)"
    echo "2. Abrir Porta HTTPS (SSL)"
    echo "3. Fechar Porta"
    echo "4. Ver Portas Abertas"
    echo "5. Status das Conexões"
    echo "6. Sair"
    echo "=========================================="
    read -p "Escolha uma opção [1-6]: " option
}

list_open_ports() {
    echo "=== Portas Ativas ==="
    ps aux | grep -E "(hsproxy-http|hsproxy-ssl)" | grep -v grep || echo "Nenhuma porta ativa."
    echo ""
    echo "=== Portas TCP abertas ==="
    ss -tuln | grep -E ':(80|443|8080|22)' || echo "Nenhuma porta detectada."
}

open_http_port() {
    read -p "Digite a porta HTTP (ex: 80): " port
    read -p "Porta destino (SSH) [22]: " to_port
    to_port=${to_port:-22}

    service_name="hsproxy-http-$port"

    cat > /etc/systemd/system/$service_name.service << EOF
[Unit]
Description=HSProxy HTTP Porta $port
After=network.target

[Service]
Type=simple
ExecStart=$HTTP_BIN --port $port --to-port $to_port
Restart=always
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable --now $service_name
    echo "✅ Porta HTTP $port aberta (→ $to_port)"
}

open_https_port() {
    read -p "Digite a porta HTTPS (ex: 443): " port
    read -p "Porta destino (SSH) [22]: " to_port
    to_port=${to_port:-22}

    service_name="hsproxy-ssl-$port"

    cat > /etc/systemd/system/$service_name.service << EOF
[Unit]
Description=HSProxy HTTPS Porta $port
After=network.target

[Service]
Type=simple
ExecStart=$SSL_BIN $to_port $port
Restart=always
RestartSec=3
User=root

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable --now $service_name
    echo "✅ Porta HTTPS $port aberta (→ $to_port)"
}

close_port() {
    read -p "Digite a porta para fechar: " port
    for svc in $(systemctl list-units --type=service | grep -o "hsproxy-.*-$port[^ ]*"); do
        systemctl stop "$svc" 2>/dev/null
        systemctl disable "$svc" 2>/dev/null
        rm -f "/etc/systemd/system/$svc"
    done
    systemctl daemon-reload
    echo "✅ Porta $port fechada."
}

show_status() {
    echo "=== Status das Conexões ==="
    journalctl -u "hsproxy-*" --no-pager -n 30 --since "2 hours ago" 2>/dev/null || true
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
        read -p "Pressione Enter para voltar ao menu..."
    done
}

main
