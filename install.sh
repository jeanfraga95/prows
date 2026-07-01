#!/bin/bash
set -e

echo "=== HSProxy - Instalação ==="

# Instala dependências
apt-get update -qq
apt-get install -y build-essential cmake libevent-dev libssl-dev pkg-config openssl

# Diretório de instalação
INSTALL_DIR="/opt/hsproxy"
mkdir -p "$INSTALL_DIR"

echo "Copiando arquivos do projeto..."
# Copia todos os arquivos necessários
cp -f CMakeLists.txt compile.sh main_http.c main_https.c menu.sh "$INSTALL_DIR/" 2>/dev/null || true

cd "$INSTALL_DIR"

# Permissões
chmod +x compile.sh menu.sh

# Compila
echo "Compilando binários..."
./compile.sh

# Certificados SSL
mkdir -p /opt/magnumproxy
if [ ! -f "/opt/magnumproxy/cert.pem" ]; then
    echo "Gerando certificados SSL auto-assinados..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
        -keyout /opt/magnumproxy/key.pem \
        -out /opt/magnumproxy/cert.pem \
        -subj "/C=BR/ST=SP/L=SaoPaulo/O=HSProxy/CN=localhost" 2>/dev/null || true
fi

# Cria links dos comandos
ln -sf "$INSTALL_DIR/build/proxy"     /usr/local/bin/hsproxy-http
ln -sf "$INSTALL_DIR/build/sslproxy"  /usr/local/bin/hsproxy-ssl
ln -sf "$INSTALL_DIR/menu.sh"         /usr/local/bin/hsproxy-menu

echo ""
echo "=== Instalação Concluída com Sucesso! ==="
echo "Comandos disponíveis:"
echo "   hsproxy-menu          → Menu principal"
echo "   hsproxy-http          → Proxy HTTP"
echo "   hsproxy-ssl           → Proxy HTTPS"
echo ""
echo "Execute agora: hsproxy-menu"
