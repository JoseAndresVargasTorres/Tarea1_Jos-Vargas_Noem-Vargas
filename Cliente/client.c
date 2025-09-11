#include <arpa/inet.h> // inet_addr()
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // bzero()
#include <sys/socket.h>
#include <unistd.h> // read(), write(), close()
#define MAX 80
#define SA struct sockaddr

void func(int sockfd)
{
    char buff[MAX];
    int n;
    for (;;) {
        bzero(buff, sizeof(buff));
        printf("Enter the string : ");
        n = 0;
        // leer del teclado hasta '\n'
        while ((buff[n++] = getchar()) != '\n') ;
        buff[n-1] = '\0'; // reemplaza '\n' por '\0' para enviar solo el string
        
        // enviar al servidor
        write(sockfd, buff, n);
        
        // limpiar buffer y leer respuesta del servidor
        bzero(buff, sizeof(buff));
        read(sockfd, buff, sizeof(buff));
        printf("From Server : %s\n", buff);
        
        // si el servidor envía "exit", termina
        if (strncmp(buff, "exit", 4) == 0) {
            printf("Client Exit...\n");
            break;
        }
    }
}

int main()
{
    int sockfd, port;
    char ip_address[16]; // Para almacenar la dirección IP (formato 127.0.0.1)
    struct sockaddr_in servaddr;
    
    // Solicitar IP del servidor
    printf("Enter server IP address: ");
    if (fgets(ip_address, sizeof(ip_address), stdin) == NULL) {
        printf("Error reading IP address...\n");
        exit(1);
    }
    
    // Remover el '\n' del final si existe
    ip_address[strcspn(ip_address, "\n")] = '\0';
    
    // Solicitar puerto del servidor
    printf("Enter server port: ");
    if (scanf("%d", &port) != 1) {
        printf("Error reading port number...\n");
        exit(1);
    }
    
    // Limpiar el buffer de entrada después de scanf
    while (getchar() != '\n');
    
    // Validar puerto
    if (port < 1 || port > 65535) {
        printf("Invalid port number. Port must be between 1 and 65535...\n");
        exit(1);
    }
    
    // crear socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    } else
        printf("Socket successfully created..\n");
    
    bzero(&servaddr, sizeof(servaddr));
    
    // asignar IP y puerto ingresados por el usuario
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip_address);
    servaddr.sin_port = htons(port);
    
    // Verificar si la IP es válida
    if (servaddr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address...\n");
        exit(1);
    }
    
    printf("Attempting to connect to %s:%d...\n", ip_address, port);
    
    // conectar al servidor
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        exit(0);
    } else
        printf("connected to the server..\n");
    
    // función para chattear
    func(sockfd);
    
    // cerrar socket
    close(sockfd);
    return 0;
}

// docker run --rm --network=host -it client
// localhost / 127.0.0.1
// ip a