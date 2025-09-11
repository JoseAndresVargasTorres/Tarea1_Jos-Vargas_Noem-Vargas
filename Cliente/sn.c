#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h> // read(), write(), close()
#define MAX 80 // Máximo tamaño del buffer 80 bytes
#define PORT 1717 // Puerto por defecto
#define SA struct sockaddr

// Estructura para recibir la información de la imagen (debe ser igual a la del cliente)
typedef struct {
    int ancho;
    int alto;
    int size; // tamaño total en bytes (ancho * alto)
} ImageHeader;

// Declaración de funciones
void guardar_imagen_pgm(unsigned char *pixels, int ancho, int alto, const char *nombre_archivo);
void recibir_imagen(int connfd);
void func(int connfd);

// Función para recibir imagen completa
void recibir_imagen(int connfd) {
    ImageHeader header;
    
    // Primero recibir el header
    if (read(connfd, &header, sizeof(ImageHeader)) <= 0) {
        printf("Error recibiendo header de imagen\n");
        return;
    }
    
    printf("Recibiendo imagen: %dx%d (%d bytes)...\n", header.ancho, header.alto, header.size);
    
    // Alocar memoria para la imagen
    unsigned char *imagen_buffer = malloc(header.size);
    if (!imagen_buffer) {
        printf("Error: No se pudo alocar memoria para la imagen\n");
        write(connfd, "ERROR: Memoria insuficiente", 28);
        return;
    }
    
    // Recibir los datos de la imagen
    int bytes_recibidos = 0;
    int total_bytes = header.size;
    
    while (bytes_recibidos < total_bytes) {
        int bytes_restantes = total_bytes - bytes_recibidos;
        int resultado = read(connfd, imagen_buffer + bytes_recibidos, bytes_restantes);
        
        if (resultado <= 0) {
            printf("Error recibiendo datos de imagen\n");
            free(imagen_buffer);
            write(connfd, "ERROR: Fallo en transmisión", 28);
            return;
        }
        
        bytes_recibidos += resultado;
        printf("Recibidos %d/%d bytes\r", bytes_recibidos, total_bytes);
        fflush(stdout);
    }
    
    printf("\nImagen recibida completamente!\n");
    
    // Aquí puedes procesar la imagen como necesites
    // Por ejemplo, guardarla en un archivo .pgm
    guardar_imagen_pgm(imagen_buffer, header.ancho, header.alto, "imagen_recibida.pgm");
    
    // Enviar confirmación al cliente
    write(connfd, "Imagen recibida correctamente", 30);
    
    // Liberar memoria
    free(imagen_buffer);
}

// Función para guardar la imagen en formato PGM
void guardar_imagen_pgm(unsigned char *pixels, int ancho, int alto, const char *nombre_archivo) {
    FILE *archivo = fopen(nombre_archivo, "wb");
    if (!archivo) {
        printf("No se pudo crear el archivo: %s\n", nombre_archivo);
        return;
    }
    
    // Escribir cabecera PGM
    fprintf(archivo, "P5\n%d %d\n255\n", ancho, alto);
    
    // Escribir datos de imagen
    fwrite(pixels, 1, ancho * alto, archivo);
    
    fclose(archivo);
    printf("Imagen guardada como: %s\n", nombre_archivo);
}

// Function designed for chat between client and server.
// Modificada para manejar imágenes
void func(int connfd)
{
    char buff[MAX];
    int n;
    
    // infinite loop for chat
    for (;;) {
        bzero(buff, MAX); // Pone todo el buffer a ceros
        
        // read the message from client and copy it in buffer
        int bytes_leidos = read(connfd, buff, sizeof(buff));
        if (bytes_leidos <= 0) {
            printf("Cliente desconectado\n");
            break;
        }
        
        // VERIFICAR SI EL CLIENTE ENVIÓ "EXIT"
        if (strncmp("exit", buff, 4) == 0) {
            printf("Client sent exit command. Server shutting down...\n");
            break;
        }
        
        // VERIFICAR SI EL CLIENTE QUIERE ENVIAR UNA IMAGEN
        if (strncmp("IMAGE", buff, 5) == 0) {
            printf("Cliente quiere enviar una imagen...\n");
            recibir_imagen(connfd);
            continue; // Continuar el loop sin pedir input del servidor
        }
        
        // print buffer which contains the client contents
        printf("From client: %s\t To client : ", buff);
        bzero(buff, MAX);
        n = 0;
        
        // copy server message in the buffer
        while ((buff[n++] = getchar()) != '\n')
            ;
        
        // and send that buffer to client
        write(connfd, buff, sizeof(buff));
        
        // if msg contains "Exit" then server exit and chat ended.
        if (strncmp("exit", buff, 4) == 0) {
            printf("Server Exit...\n");
            break;
        }
    }
}

// Driver function
int main()
{
    int sockfd, connfd, len;
    struct sockaddr_in servaddr, cli;
    
    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // Creación de socket TCP IPv4
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully created..\n");
    
    bzero(&servaddr, sizeof(servaddr)); // Limpia sockaddr_in
    
    // assign IP, PORT
    servaddr.sin_family = AF_INET; // Indica IPv4
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Acepta conexiones por todas las interfaces locales
    servaddr.sin_port = htons(PORT); // Convierte el puerto a orden de bytes de red
    
    // Binding newly created socket to given IP and verification
    // Enlaza el socket al puerto IP
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed...\n");
        exit(0);
    }
    else
        printf("Socket successfully binded..\n");
    
    // Now server is ready to listen and verification
    // Pone el socket en modo escucha, el 5 representa la cantidad de conexiones que espera
    if ((listen(sockfd, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    }
    else
        printf("Server listening..\n");
    
    len = sizeof(cli);
    
    // Accept the data packet from client and verification
    connfd = accept(sockfd, (SA*)&cli, &len);
    if (connfd < 0) {
        printf("server accept failed...\n");
        exit(0);
    }
    else
        printf("server accept the client...\n");
    
    // Function for chatting between client and server
    func(connfd); // Realiza el chat hasta que salga
    
    // After chatting close the socket
    close(sockfd);
    
    return 0;
}