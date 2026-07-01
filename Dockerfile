# Dockerfile para compilar MagnumProxy totalmente estático
FROM alpine:latest

# Instalar dependências de build
RUN apk add --no-cache musl-dev build-base wget tar perl linux-headers

# Diretório de trabalho
WORKDIR /src

# ------------------------
# 1. Compilar OpenSSL estático
# ------------------------
RUN wget https://www.openssl.org/source/openssl-1.1.1w.tar.gz \
    && tar xf openssl-1.1.1w.tar.gz \
    && cd openssl-1.1.1w \
    && CC=gcc ./Configure no-shared no-tests no-async --prefix=/usr/local --openssldir=/usr/local/ssl linux-x86_64 \
    && make -j$(nproc) \
    && make install

# ------------------------
# 2. Compilar libevent estático com suporte a OpenSSL
# ------------------------
RUN wget https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz \
    && tar xf libevent-2.1.12-stable.tar.gz \
    && cd libevent-2.1.12-stable \
    && CC=gcc ./configure --disable-shared --enable-static --prefix=/usr/local \
    && make -j$(nproc) \
    && make install

# ------------------------
# 3. Copiar código-fonte do projeto
# ------------------------
COPY . .

# ------------------------
# 4. Criar build e compilar binários estáticos
# ------------------------
RUN rm -rf build && mkdir -p build \
    && gcc -static -I/usr/local/include main_http.c -L/usr/local/lib -levent -o build/MagnumProxy_x64 \
    && gcc -static -I/usr/local/include main_https.c -L/usr/local/lib -levent -levent_openssl -lssl -lcrypto -o build/MagnumProxySSL_x64 \
    && gcc -static -I/usr/local/include main_menu.c -L/usr/local/lib -o build/menu_x64

# ------------------------
# 5. Definir diretório de saída para o volume
# ------------------------
VOLUME ["/output"]
