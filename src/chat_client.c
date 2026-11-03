
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* macOS nao possui MSG_NOSIGNAL; SIGPIPE ja e ignorado em main(). */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define BUFFER_SIZE 1024
#define DEFAULT_BLOCK 65536

int sockfd;
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent,
                         MSG_NOSIGNAL);

        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

/* ---- Modo benchmark ----------------------------------------------
 * Uso: chat_client <IP> <porta> --bench <nome> [bloco_bytes]
 * Envia "/bench <nome>", espera "GO <segundos>" do servidor (barreira
 * que sincroniza todos os clientes) e transmite blocos de dados sem
 * parar durante o tempo informado, sem digitacao manual. */
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int read_go(int fd, double *duration) {
    char line[64];
    size_t pos = 0;

    while (pos < sizeof(line) - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);

        if (n == -1 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        if (c == '\n')
            break;

        line[pos++] = c;
    }

    line[pos] = '\0';
    return sscanf(line, "GO %lf", duration) == 1 ? 0 : -1;
}

static int run_bench(int fd, const char *name, size_t block) {
    char hello[96];
    double duration;
    char *buf = malloc(block);

    if (buf == NULL)
        return -1;

    memset(buf, 'x', block);

    snprintf(hello, sizeof(hello), "/bench %s\n", name);

    if (send_all(fd, hello, strlen(hello)) == -1 ||
        read_go(fd, &duration) == -1) {
        fprintf(stderr, "Falha no handshake do benchmark.\n");
        free(buf);
        return -1;
    }

    uint64_t total = 0;
    double start = now_s();
    double end = start + duration;

    while (now_s() < end) {
        if (send_all(fd, buf, block) == -1)
            break;
        total += block;
    }

    double elapsed = now_s() - start;

    shutdown(fd, SHUT_WR);

    printf("CLIENT %s bytes=%llu seconds=%.3f mbps=%.2f\n", name,
           (unsigned long long)total, elapsed,
           (double)total * 8.0 / elapsed / 1e6);

    free(buf);
    return 0;
}

void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    size_t pos = 0;

    (void)arg;

    while (1) {
        char c;
        ssize_t n = recv(sockfd, &c, 1, 0);

        if (n == 0) {
            pthread_mutex_lock(&output_mutex);
            printf("\nConexao encerrada pelo servidor.\n");
            fflush(stdout);
            pthread_mutex_unlock(&output_mutex);
            break;
        }

        if (n == -1) {
            if (errno == EINTR)
                continue;

            pthread_mutex_lock(&output_mutex);
            perror("recv");
            pthread_mutex_unlock(&output_mutex);
            break;
        }

        if (c == '\n') {
            buffer[pos] = '\0';

            pthread_mutex_lock(&output_mutex);
            printf("\r%s\n", buffer);
            fflush(stdout);
            pthread_mutex_unlock(&output_mutex);

            pos = 0;
        } else if (c != '\r') {
            if (pos < sizeof(buffer) - 1)
                buffer[pos++] = c;
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    char name[32];
    char buffer[BUFFER_SIZE];
    pthread_t receiver;

    int bench = (argc >= 5 && strcmp(argv[3], "--bench") == 0);

    if (argc != 3 && !bench) {
        fprintf(stderr,
                "Uso: %s <IP> <porta> [--bench <nome> [bloco_bytes]]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, argv[1],
                  &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Endereco IPv4 invalido.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (bench) {
        size_t block = argc >= 6 ? (size_t)atol(argv[5])
                                 : DEFAULT_BLOCK;
        int rc = block > 0 ? run_bench(sockfd, argv[4], block) : -1;

        close(sockfd);
        return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    printf("Seu nome: ");

    if (fgets(name, sizeof(name), stdin) == NULL) {
        close(sockfd);
        return EXIT_FAILURE;
    }

    name[strcspn(name, "\r\n")] = '\0';

    if (name[0] == '\0' ||
        strchr(name, '\n') != NULL) {
        fprintf(stderr, "Nome invalido.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (send_all(sockfd, name, strlen(name)) == -1 ||
        send_all(sockfd, "\n", 1) == -1) {
        perror("send");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (pthread_create(&receiver, NULL,
                       receive_messages, NULL) != 0) {
        fprintf(stderr, "Erro ao criar thread receptora.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Conectado! Digite /quit para sair.\n");

    while (1) {
        pthread_mutex_lock(&output_mutex);
        printf("> ");
        fflush(stdout);
        pthread_mutex_unlock(&output_mutex);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
            break;

        if (strcmp(buffer, "/quit\n") == 0 ||
            strcmp(buffer, "/quit") == 0) {
            send_all(sockfd, "/quit\n", 6);
            break;
        }

        size_t len = strlen(buffer);

        if (len > 0 && buffer[len - 1] != '\n') {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            fprintf(stderr, "Mensagem muito longa.\n");
            continue;
        }

        if (send_all(sockfd, buffer, len) == -1) {
            perror("send");
            break;
        }
    }

    shutdown(sockfd, SHUT_RDWR);
    pthread_join(receiver, NULL);
    close(sockfd);

    pthread_mutex_destroy(&output_mutex);

    printf("Cliente encerrado.\n");

    return EXIT_SUCCESS;
}
