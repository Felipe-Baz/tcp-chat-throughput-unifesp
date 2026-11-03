
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

/* macOS nao possui MSG_NOSIGNAL; SIGPIPE ja e ignorado em main(). */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100
#define BENCH_BUF_SIZE (256 * 1024)

typedef struct Client {
    int fd;
    char name[32];
    struct Client *next;
} Client;

Client *clients = NULL;
int num_clients = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Envia todos os bytes, mesmo se send() enviar parcialmente. */
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

/* Le uma linha terminada por '\n'. */
ssize_t read_line(int fd, char *buffer, size_t capacity) {
    size_t pos = 0;

    while (pos < capacity - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);

        if (n == 0)
            return pos == 0 ? 0 : (ssize_t)pos;

        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (c == '\n')
            break;

        if (c != '\r')
            buffer[pos++] = c;
    }

    buffer[pos] = '\0';
    return (ssize_t)pos;
}


/* ---- Modo benchmark ----------------------------------------------
 * Uso: chat_server <porta> --bench <N> [aquecimento_s] [medicao_s]
 * Aguarda N clientes (que enviam "/bench <nome>"), libera todos ao
 * mesmo tempo com "GO <duracao>
" e mede os bytes recebidos de todas
 * as threads apenas na janela [aquecimento, aquecimento+medicao].
 * Cada thread tem seu proprio contador (sem lock no caminho quente). */
typedef struct {
    _Atomic uint64_t bytes;
    char pad[56];                 /* evita false sharing (linha de 64 B) */
} BenchCounter;

static BenchCounter bench_counters[MAX_CLIENTS];
static int bench_expected = 0;
static int bench_next = 0;
static int bench_arrived = 0;
static int bench_released = 0;
static double bench_warmup = 2.0, bench_measure = 10.0;
static pthread_mutex_t bench_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bench_cond = PTHREAD_COND_INITIALIZER;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint64_t bench_total(void) {
    uint64_t t = 0;
    for (int i = 0; i < bench_expected; i++)
        t += atomic_load_explicit(&bench_counters[i].bytes,
                                  memory_order_relaxed);
    return t;
}

static void *bench_sampler(void *arg) {
    (void)arg;
    struct timespec w = { (time_t)bench_warmup,
        (long)((bench_warmup - (time_t)bench_warmup) * 1e9) };
    nanosleep(&w, NULL);

    uint64_t b0 = bench_total();
    double t0 = now_s();

    struct timespec m = { (time_t)bench_measure,
        (long)((bench_measure - (time_t)bench_measure) * 1e9) };
    nanosleep(&m, NULL);

    uint64_t b1 = bench_total();
    double t1 = now_s();

    double secs = t1 - t0;
    double mbps = (double)(b1 - b0) * 8.0 / secs / 1e6;

    printf("RESULT clients=%d bytes=%llu seconds=%.4f mbps=%.2f\n",
           bench_expected, (unsigned long long)(b1 - b0), secs, mbps);
    fflush(stdout);
    return NULL;
}

/* Trata um cliente em modo benchmark; idx = indice do contador. */
static void bench_client(Client *client, int idx) {
    char *buf = malloc(BENCH_BUF_SIZE);
    char go[64];

    if (buf == NULL)
        return;

    /* Barreira: o ultimo a chegar libera todos e dispara o medidor. */
    pthread_mutex_lock(&bench_mutex);
    if (++bench_arrived == bench_expected) {
        pthread_t st;
        bench_released = 1;
        pthread_create(&st, NULL, bench_sampler, NULL);
        pthread_detach(st);
        pthread_cond_broadcast(&bench_cond);
    }
    while (!bench_released)
        pthread_cond_wait(&bench_cond, &bench_mutex);
    pthread_mutex_unlock(&bench_mutex);

    /* Duracao do envio = aquecimento + medicao + 1 s de folga. */
    snprintf(go, sizeof(go), "GO %.3f\n",
             bench_warmup + bench_measure + 1.0);
    send_all(client->fd, go, strlen(go));

    for (;;) {
        ssize_t n = recv(client->fd, buf, BENCH_BUF_SIZE, 0);
        if (n > 0) {
            atomic_fetch_add_explicit(&bench_counters[idx].bytes,
                                      (uint64_t)n,
                                      memory_order_relaxed);
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }

    free(buf);
}


/* Distribui uma mensagem aos demais clientes. */
void broadcast(const char *message, Client *sender) {
    pthread_mutex_lock(&clients_mutex);

    Client *current = clients;

    while (current != NULL) {
        if (current != sender) {
            if (send_all(current->fd, message,
                         strlen(message)) == -1) {
                fprintf(stderr,
                        "Falha ao enviar para %s.\n",
                        current->name);
            }
        }

        current = current->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    Client *client = arg;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE + 64];
    int registered = 0;

    /* A primeira linha recebida e o nome do usuario. */
    ssize_t n = read_line(client->fd, buffer, sizeof(buffer));

    if (n <= 0 || buffer[0] == '\0') {
        close(client->fd);
        free(client);
        return NULL;
    }

    if (bench_expected > 0 && strncmp(buffer, "/bench", 6) == 0) {
        pthread_mutex_lock(&bench_mutex);
        int idx = bench_next++;       /* slot unico por cliente */
        pthread_mutex_unlock(&bench_mutex);
        if (idx >= bench_expected) {  /* cliente excedente */
            close(client->fd);
            free(client);
            return NULL;
        }
        bench_client(client, idx);
        close(client->fd);
        free(client);
        return NULL;
    }

    snprintf(client->name, sizeof(client->name),
             "%s", buffer);

    pthread_mutex_lock(&clients_mutex);

    if (num_clients >= MAX_CLIENTS) {
        pthread_mutex_unlock(&clients_mutex);

        const char *error = "Servidor cheio.\n";
        send_all(client->fd, error, strlen(error));
        close(client->fd);
        free(client);
        return NULL;
    }

    client->next = clients;
    clients = client;
    num_clients++;
    registered = 1;

    pthread_mutex_unlock(&clients_mutex);

    snprintf(message, sizeof(message),
             "*** %s entrou no chat. ***\n",
             client->name);
    broadcast(message, client);

    printf("Usuario conectado: %s\n", client->name);

    while ((n = read_line(client->fd, buffer,
                          sizeof(buffer))) > 0) {
        if (strcmp(buffer, "/quit") == 0)
            break;

        snprintf(message, sizeof(message),
                 "%s: %s\n", client->name, buffer);

        broadcast(message, client);
    }

    if (n == -1)
        perror("recv");

    if (registered) {
        pthread_mutex_lock(&clients_mutex);

        Client **current = &clients;

        while (*current != NULL && *current != client)
            current = &(*current)->next;

        if (*current == client) {
            *current = client->next;
            num_clients--;
        }

        pthread_mutex_unlock(&clients_mutex);

        snprintf(message, sizeof(message),
                 "*** %s saiu do chat. ***\n",
                 client->name);
        broadcast(message, NULL);
    }

    printf("Usuario desconectado: %s\n", client->name);

    close(client->fd);
    free(client);

    return NULL;
}

int main(int argc, char *argv[]) {
    int server_fd;
    struct sockaddr_in server_addr;

    if (argc != 2 && argc < 4) {
        fprintf(stderr,
                "Uso: %s <porta> [--bench <N> [aquec_s] [medicao_s]]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 4 && strcmp(argv[2], "--bench") == 0) {
        bench_expected = atoi(argv[3]);
        if (argc >= 5) bench_warmup = atof(argv[4]);
        if (argc >= 6) bench_measure = atof(argv[5]);
        if (bench_expected < 1 || bench_expected > MAX_CLIENTS) {
            fprintf(stderr, "N invalido.\n");
            return EXIT_FAILURE;
        }
    }

    int port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 128) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Chat TCP ativo na porta %d.\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &addr_len
        );

        if (client_fd == -1) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        Client *client = malloc(sizeof(Client));

        if (client == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        client->fd = client_fd;
        client->name[0] = '\0';
        client->next = NULL;

        pthread_t tid;

        if (pthread_create(&tid, NULL,
                           handle_client, client) != 0) {
            fprintf(stderr, "Erro ao criar thread.\n");
            close(client_fd);
            free(client);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    pthread_mutex_destroy(&clients_mutex);

    return EXIT_SUCCESS;
}
