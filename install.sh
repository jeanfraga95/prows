#!/bin/bash
set -e

echo "=== HSProxy - Instalação ==="

# Instala dependências
apt-get update -qq
apt-get install -y build-essential cmake libevent-dev libssl-dev pkg-config openssl

# Diretório de instalação
INSTALL_DIR="/opt/hsproxy"
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

echo "Copiando arquivos do projeto..."

# Copia todos os arquivos do diretório atual (onde está o install.sh)
cp -r "$(dirname "$0")"/* . 2>/dev/null || true

# Garante que os scripts têm permissão de execução
chmod +x compile.sh menu.sh 2>/dev/null || true

# Compila
echo "Compilando binários..."
./compile.sh

# Certificados SSL
mkdir -p /opt/magnumproxy
if [ ! -f "/opt/magnumproxy/cert.pem" ]; then
    echo "Gerando certificados SSL..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
        -keyout /opt/magnumproxy/key.pem \
        -out /opt/magnumproxy/cert.pem \
        -subj "/C=BR/ST=SP/L=SaoPaulo/O=HSProxy/CN=localhost" 2>/dev/null || true
fi

# Links dos comandos
ln -sf "$INSTALL_DIR/build/proxy" /usr/local/bin/hsproxy-http 2>/dev/null || true
ln -sf "$INSTALL_DIR/build/sslproxy" /usr/local/bin/hsproxy-ssl 2>/dev/null || true
ln -sf "$INSTALL_DIR/menu.sh" /usr/local/bin/hsproxy-menu 2>/dev/null || true

echo ""
echo "=== Instalação Concluída! ==="
echo "Agora execute: hsproxy-menu"
