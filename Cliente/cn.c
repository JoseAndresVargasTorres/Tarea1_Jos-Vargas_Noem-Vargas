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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>

// Estructura para enviar la información de la imagen
typedef struct {
    int ancho;
    int alto;
    int size; // tamaño total en bytes (ancho * alto)
} ImageHeader;

void enviar_imagen(int sockfd, unsigned char *pixels, int ancho, int alto)
{
    ImageHeader header;
    header.ancho = ancho;
    header.alto = alto;
    header.size = ancho * alto;
    
    printf("Enviando imagen: %dx%d (%d bytes)...\n", ancho, alto, header.size);
    
    // Primero enviar el header con la información de la imagen
    if (write(sockfd, &header, sizeof(ImageHeader)) < 0) {
        printf("Error enviando header de imagen\n");
        return;
    }
    
    // Luego enviar los datos de la imagen
    int bytes_enviados = 0;
    int total_bytes = header.size;
    
    while (bytes_enviados < total_bytes) {
        int bytes_restantes = total_bytes - bytes_enviados;
        int chunk_size = (bytes_restantes > 4096) ? 4096 : bytes_restantes; // Enviar en chunks de máximo 4KB
        
        int resultado = write(sockfd, pixels + bytes_enviados, chunk_size);
        if (resultado < 0) {
            printf("Error enviando datos de imagen\n");
            return;
        }
        
        bytes_enviados += resultado;
        printf("Enviados %d/%d bytes\r", bytes_enviados, total_bytes);
        fflush(stdout);
    }
    
    printf("\nImagen enviada completamente!\n");
    
    // Esperar confirmación del servidor
    char respuesta[MAX];
    bzero(respuesta, sizeof(respuesta));
    read(sockfd, respuesta, sizeof(respuesta));
    printf("Respuesta del servidor: %s\n", respuesta);
}

void func(int sockfd, unsigned char *pixels, int ancho, int alto)
{
    char buff[MAX];
    int n;
    int imagen_enviada = 0; // Flag para controlar si ya se envió la imagen
    
    for (;;) {
        bzero(buff, sizeof(buff));
        printf("Enter command (send/exit): ");
        n = 0;
        // leer del teclado hasta '\n'
        while ((buff[n++] = getchar()) != '\n') ;
        buff[n-1] = '\0'; // reemplaza '\n' por '\0'
        
        if (strncmp(buff, "send", 4) == 0) {
            if (!imagen_enviada) {
                // Enviar comando al servidor para indicar que viene una imagen
                write(sockfd, "IMAGE", 6);
                
                // Enviar la imagen
                enviar_imagen(sockfd, pixels, ancho, alto);
                imagen_enviada = 1;
            } else {
                printf("La imagen ya fue enviada. Use 'exit' para terminar.\n");
            }
        }
        else if (strncmp(buff, "exit", 4) == 0) {
            // enviar comando exit al servidor
            write(sockfd, buff, strlen(buff) + 1);
            printf("Client Exit...\n");
            break;
        }
        else {
            // Para otros comandos, enviar como texto normal
            write(sockfd, buff, strlen(buff) + 1);
            
            // leer respuesta del servidor
            bzero(buff, sizeof(buff));
            read(sockfd, buff, sizeof(buff));
            printf("From Server : %s\n", buff);
        }
    }
}

// Función que recibe una imagen en cualquier formato y la pasa a array de bytes
unsigned char* Convertir_PGM(const char *NombreImagen, int *ancho, int *alto) {
    int canales = 1;

    // Cargar imagen, solo un canal, como es escala de grises R = G = B
    unsigned char *pixel_array = stbi_load(NombreImagen, ancho, alto, &canales, 1);
    if (!pixel_array) {
        printf("Error al abrir la imagen: %s\n", NombreImagen);
        return NULL;
    }

    printf("Imagen cargada: %s (%dx%d)\n", NombreImagen, *ancho, *alto);
    
    // Retornar el array con los píxeles
    return pixel_array;
}

int main()
{
    const char *image_original = "faro.jpg"; // Imagen a enviar
    int ancho, alto; // Variables para guardar ancho y alto

    // Llama a la función para convertir la imagen
    unsigned char *pixels = Convertir_PGM(image_original, &ancho, &alto);
    if (!pixels) return 1;
    
    // Código de client.c
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
    
    // función para manejar comandos y envío de imagen
    func(sockfd, pixels, ancho, alto);
    
    // cerrar socket
    close(sockfd);
    stbi_image_free(pixels); // liberar memoria
    return 0;
}

// docker run --rm --network=host -it client
// localhost / 127.0.0.1
// ip a