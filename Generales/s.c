#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>  // inet_ntoa()

#define MAX 80
#define CONFIG_PATH "/etc/server/config.conf"
#define SA struct sockaddr

// Variables globales para config
int PORT = 8080;                // valor por defecto
char LOGFILE[256] = "/var/log/servidor.log";

// Función para leer configuración simple
void read_config() {
    FILE *config = fopen(CONFIG_PATH, "r");
    if (config == NULL) {
        perror("No se pudo abrir el archivo de configuración, usando valores por defecto");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), config)) {
        if (strncmp(line, "PORT=", 5) == 0) {
            PORT = atoi(line + 5);
        } else if (strncmp(line, "LOGFILE=", 8) == 0) {
            strncpy(LOGFILE, line + 8, sizeof(LOGFILE)-1);
            LOGFILE[strcspn(LOGFILE, "\n")] = 0; // quitar salto de línea
        }
    }
    fclose(config);
}

// Función para escribir en el log
void write_log(const char *client_ip, const char *msg, const char *status) {
    FILE *log = fopen(LOGFILE, "a");
    if (!log) {
        perror("No se pudo abrir el log");
        return;
    }
    time_t now = time(NULL);
    char *timestr = ctime(&now);
    timestr[strcspn(timestr, "\n")] = 0; // quitar salto de línea

    fprintf(log, "[%s] Cliente: %s | Mensaje: %s | Estado: %s\n",
            timestr, client_ip, msg, status);
    fclose(log);
}

// Función para comunicación
void func(int connfd, char *client_ip) {
    char buff[MAX];
    int n;
    for (;;) {
        bzero(buff, MAX);
        read(connfd, buff, sizeof(buff));

        printf("From client: %s\t To client : ", buff);
        write_log(client_ip, buff, "RECIBIDO");

        bzero(buff, MAX);
        n = 0;
        while ((buff[n++] = getchar()) != '\n')
            ;

        write(connfd, buff, sizeof(buff));
        write_log(client_ip, buff, "RESPONDIDO");

        if (strncmp("exit", buff, 4) == 0) {
            printf("Server Exit...\n");
            write_log(client_ip, "exit", "DESCONECTADO");
            break;
        }
    }
}

int main() {
    int sockfd, connfd;
    socklen_t len;
    struct sockaddr_in servaddr, cli;

    read_config();
    printf("Puerto configurado: %d\n", PORT);
    printf("Log en: %s\n", LOGFILE);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        perror("socket bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully binded..\n");

    if ((listen(sockfd, 5)) != 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server listening..\n");

    len = sizeof(cli);
    connfd = accept(sockfd, (SA*)&cli, &len);
    if (connfd < 0) {
        perror("server accept failed");
        exit(EXIT_FAILURE);
    }
    char *client_ip = inet_ntoa(cli.sin_addr);
    printf("server accept the client (%s)...\n", client_ip);
    write_log(client_ip, "Conexión establecida", "OK");

    func(connfd, client_ip);

    close(connfd);
    close(sockfd);
}
