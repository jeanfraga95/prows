cmake_minimum_required(VERSION 3.10)
project(HSProxy C)

set(CMAKE_C_STANDARD 11)

# Encontrando o pkg-config e verificando dependências
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(LIBEVENT_OPENSSL REQUIRED libevent_openssl)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(CURL REQUIRED libcurl)

# Diretórios de headers e libs
include_directories(
    ${LIBEVENT_INCLUDE_DIRS} 
    ${LIBEVENT_OPENSSL_INCLUDE_DIRS} 
    ${OPENSSL_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

link_directories(
    ${LIBEVENT_LIBRARY_DIRS} 
    ${LIBEVENT_OPENSSL_LIBRARY_DIRS} 
    ${OPENSSL_LIBRARY_DIRS}
    ${CURL_LIBRARY_DIRS}
)

# Executáveis
add_executable(proxy proxy.c)
add_executable(sslproxy sslproxy.c)

# Linkagem
target_link_libraries(proxy 
    ${LIBEVENT_LIBRARIES} 
    ${CURL_LIBRARIES}
)

target_link_libraries(sslproxy 
    ${LIBEVENT_LIBRARIES} 
    ${LIBEVENT_OPENSSL_LIBRARIES} 
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
)

# Adiciona flags de compilação para ambos
target_compile_options(proxy PRIVATE -Wall -O2)
target_compile_options(sslproxy PRIVATE -Wall -O2)
