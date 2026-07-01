cmake_minimum_required(VERSION 3.10)
project(HSProxy C)

set(CMAKE_C_STANDARD 11)

# Encontrando o pkg-config e verificando dependências
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBEVENT_OPENSSL REQUIRED libevent_openssl)
pkg_check_modules(OPENSSL REQUIRED openssl)

# Diretórios de headers e libs
include_directories(${LIBEVENT_INCLUDE_DIRS} ${LIBEVENT_OPENSSL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})
link_directories(${LIBEVENT_LIBRARY_DIRS} ${LIBEVENT_OPENSSL_LIBRARY_DIRS} ${OPENSSL_LIBRARY_DIRS})

# Executáveis
add_executable(proxy main_http.c)
add_executable(sslproxy main_https.c)

# Linkagem
target_link_libraries(proxy ${LIBEVENT_LIBRARIES})
target_link_libraries(sslproxy ${LIBEVENT_LIBRARIES} ${LIBEVENT_OPENSSL_LIBRARIES} ${OPENSSL_LIBRARIES})
