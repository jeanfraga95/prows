#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== HSProxy - Compilando ==="

# Dependências necessárias no Debian/Ubuntu (descomente se precisar instalar):
# sudo apt update
# sudo apt install -y build-essential cmake pkg-config \
#     libevent-dev libevent-openssl-2.1-7 openssl libssl-dev libcurl4-openssl-dev

# Limpa build anterior (opcional)
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Gera os arquivos de build com CMake
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"

# Compila
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "=== Compilação concluída ==="
echo "Binários gerados em: $BUILD_DIR"
echo "  - $BUILD_DIR/proxy     (HTTP)"
echo "  - $BUILD_DIR/sslproxy  (HTTPS)"
