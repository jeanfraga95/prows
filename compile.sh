#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== HSProxy - Compilando ==="

# Verifica dependências
echo "Verificando dependências..."

# Verifica libevent
if ! pkg-config --exists libevent; then
    echo "❌ libevent não encontrada. Instale com:"
    echo "  apt-get install libevent-dev"
    exit 1
fi

# Verifica OpenSSL
if ! pkg-config --exists openssl; then
    echo "❌ OpenSSL não encontrada. Instale com:"
    echo "  apt-get install libssl-dev"
    exit 1
fi

# Verifica libcurl
if ! pkg-config --exists libcurl; then
    echo "❌ libcurl não encontrada. Instale com:"
    echo "  apt-get install libcurl4-openssl-dev"
    exit 1
fi

echo "✅ Todas as dependências encontradas"

# Limpa build anterior
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Obtém flags de compilação
LIBEVENT_CFLAGS=$(pkg-config --cflags libevent)
LIBEVENT_LIBS=$(pkg-config --libs libevent)
OPENSSL_CFLAGS=$(pkg-config --cflags openssl)
OPENSSL_LIBS=$(pkg-config --libs openssl)
CURL_CFLAGS=$(pkg-config --cflags libcurl)
CURL_LIBS=$(pkg-config --libs libcurl)

echo "Compilando proxy (HTTP)..."
gcc -o "$BUILD_DIR/proxy" \
    "$SCRIPT_DIR/proxy.c" \
    $LIBEVENT_CFLAGS $LIBEVENT_LIBS \
    $CURL_CFLAGS $CURL_LIBS \
    -Wall -O2

echo "Compilando sslproxy (HTTPS)..."
gcc -o "$BUILD_DIR/sslproxy" \
    "$SCRIPT_DIR/sslproxy.c" \
    $LIBEVENT_CFLAGS $LIBEVENT_LIBS \
    $OPENSSL_CFLAGS $OPENSSL_LIBS \
    $CURL_CFLAGS $CURL_LIBS \
    -Wall -O2

# Verifica se os binários foram criados
if [[ -f "$BUILD_DIR/proxy" && -f "$BUILD_DIR/sslproxy" ]]; then
    echo ""
    echo "=== Compilação concluída com sucesso! ==="
    echo "Binários gerados em: $BUILD_DIR"
    echo "  - $BUILD_DIR/proxy     (HTTP)"
    echo "  - $BUILD_DIR/sslproxy  (HTTPS)"
    
    # Mostra tamanho dos binários
    echo ""
    echo "Tamanho dos binários:"
    ls -lh "$BUILD_DIR/proxy" "$BUILD_DIR/sslproxy" | awk '{print "  " $9 " (" $5 ")"}'
else
    echo "❌ Erro na compilação. Verifique os logs acima."
    exit 1
fi
