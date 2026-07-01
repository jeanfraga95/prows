#!/bin/bash
set -e

echo "=== HSProxy - Instalação ==="

# Atualiza sistema e instala dependências
apt-get update -qq
apt-get install -y build-essential cmake libevent-dev libssl-dev pkg-config openssl

# Diretório do projeto
INSTALL_DIR="/opt/hsproxy"
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

# Copia os arquivos do projeto
echo "Copiando arquivos..."
cp -r /home/workdir/HSProxy/* . 2>/dev/null || true

# Torna executável
chmod +x compile.sh

# Compila os binários
echo "Compilando binários..."
./compile.sh

# Cria diretório para certificados
mkdir -p /opt/magnumproxy
if [ ! -f "/opt/magnumproxy/cert.pem" ] || [ ! -f "/opt/magnumproxy/key.pem" ]; then
    echo "Gerando certificados SSL auto-assinados..."
    openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
        -keyout /opt/magnumproxy/key.pem \
        -out /opt/magnumproxy/cert.pem \
        -subj "/C=BR/ST=SP/L=SaoPaulo/O=HSProxy/CN=localhost" 2>/dev/null || true
fi

# Cria links dos binários
ln -sf "$INSTALL_DIR/build/proxy" /usr/local/bin/hsproxy-http 2>/dev/null || true
ln -sf "$INSTALL_DIR/build/sslproxy" /usr/local/bin/hsproxy-ssl 2>/dev/null || true
ln -sf "$INSTALL_DIR/menu.sh" /usr/local/bin/hsproxy-menu 2>/dev/null || true

echo ""
echo "=== Instalação concluída com sucesso! ==="
echo "Comandos disponíveis:"
echo "   hsproxy-menu          → Abre o menu de gerenciamento"
echo "   hsproxy-http --help   → Proxy HTTP"
echo "   hsproxy-ssl           → Proxy HTTPS"
echo ""
echo "Execute: hsproxy-menu"
