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

// Función que recibe una imagen en cualquier formato y la pasa a .pgm (para escala de grises)
// Esto para facilidad, ya que .ppm es un formato de C

//unsigned char* Convertir_PGM(const char *NombreImagen, const char *imagen_ppm, int *ancho, int *alto) {
unsigned char* Convertir_PGM(const char *NombreImagen, int *ancho, int *alto) {
    int canales = 1;

    // Cargar imagen, solo un canal, como es escala de grises R = G = B
    unsigned char *pixel_array = stbi_load(NombreImagen, ancho, alto, &canales, 1);
    if (!pixel_array) {
        printf("Error al abrir la imagen: %s\n", NombreImagen);
        return NULL;
    }

    // Guardar en PPM
   /*FILE *fout = fopen(imagen_ppm, "wb");
    if (!fout) {
        printf("No se pudo crear el archivo PPM: %s\n", imagen_ppm);
        stbi_image_free(pixel_array);
        return NULL;
    }*/ 

    //fprintf(fout, "P5\n%d %d\n255\n", *ancho, *alto); // Cabecera para que sea un archivo válido
    //fwrite(pixel_array, 1, (*ancho) * (*alto), fout); // 1 byte por píxel - Escribir los pixeles o el contenido de la imagen
    //fclose(fout); // Cerrar el archivo

    //printf("Imagen convertida a PPM: %s (%dx%d)\n", imagen_ppm, *ancho, *alto);

    // Retornar el array con los píxeles
    return pixel_array;
}

int main()
{


    const char *image_original = "img/girl.gif"; // Imagen a enviar
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
    
    // función para chattear
    func(sockfd);
    
    // cerrar socket
    close(sockfd);
    stbi_image_free(pixels); // liberar memoria
    return 0;
}

// docker run --rm --network=host -it client
// localhost / 127.0.0.1
// ip a