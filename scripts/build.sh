#!/bin/sh
# Compila servidor e cliente em bin/. Execute a partir de qualquer pasta.
cd "$(dirname "$0")/.." || exit 1
mkdir -p bin
CC=${CC:-cc}
$CC -O2 -Wall -Wextra src/chat_server.c -o bin/chat_server -pthread &&
$CC -O2 -Wall -Wextra src/chat_client.c -o bin/chat_client -pthread &&
echo "OK: bin/chat_server bin/chat_client"
