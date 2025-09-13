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
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>

// Estructura para enviar la información de la imagen

typedef struct ImageTask
{
    int sockfd;
    ImageHeader header;
    unsigned char *data;
    struct ImageTask *next;
} ImageTask;

typedef struct {
    int ancho;
    int alto;
    int size; // tamaño total en bytes (ancho * alto)
} ImageHeader;

#include <pthread.h>
// Variables globales para cola de prioridad
ImageTask *cola_imagenes = NULL;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_condition = PTHREAD_COND_INITIALIZER;
int servidor_activo = 1;

// AÑADIR ESTAS DECLARACIONES AQUÍ:
void enviar_imagen(int sockfd, unsigned char *pixels, int ancho, int alto);
void recibir_imagen_procesada(int sockfd, const char *nombre_original);
void func(int sockfd, unsigned char *pixels, int ancho, int alto);
unsigned char* Convertir_PGM(const char *NombreImagen, int *ancho, int *alto);





void enviar_imagen(int sockfd, unsigned char *pixels, int ancho, int alto)
{
    ImageHeader header;
    header.ancho = ancho;
    header.alto = alto;
    header.size = ancho * alto;
    
    // AÑADIR AQUÍ EL DEBUG
    printf("DEBUG: Header enviado - ancho:%d, alto:%d, size:%d\n", 
           header.ancho, header.alto, header.size);
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
    printf("\nImagen procesada enviada al cliente!\n");


    // Recibir imagen procesada
    recibir_imagen_procesada(sockfd, "faro.jpg");
}


void recibir_imagen_procesada(int sockfd, const char *nombre_original){

    ImageHeader header;

    printf("ESperando imagen procesada del servidor...\n");

    //Recibir header
    if(read(sockfd, &header, sizeof(ImageHeader)) <= 0){
        printf("Error recibiendo header de imagen procesada\n");
        return;
    }

    printf("Recibiendo imagen procesada: %dx%d (%d bytes)..\n",
    header.ancho,header.alto,header.size);

    //Alocar memoria
    unsigned char *imagen_procesada = malloc(header.size);
    if(!imagen_procesada){
        printf("Error: No se pudo alocar memoria\n");
        return;
    }

    //Recibir datos
    int bytes_recibidos = 0;
    while(bytes_recibidos < header.size){
        int bytes_restantes = header.size - bytes_recibidos;
        int resultado = read(sockfd,imagen_procesada+bytes_recibidos, bytes_restantes);

        if (resultado <=0){
            printf("ERROR  RECIBIENDO IMAGEN PROCESADA\n");
            free(imagen_procesada);
            return ;
        }

        bytes_recibidos += resultado;
        printf("Descargados %d/%d bytes\r", bytes_recibidos, header.size);
        fflush(stdout);

    }

    // Guardar imagen procesada
    char nombre_salida[256];
    snprintf(nombre_salida, sizeof(nombre_salida), "processed_%s", nombre_original);
    stbi_write_jpg(nombre_salida, header.ancho, header.alto, 1, imagen_procesada, 90);
    
    printf("\nImagen procesada guardada como: %s\n", nombre_salida);
    
    free(imagen_procesada);




}


void insertar_en_cola(int sockfd, ImageHeader header, unsigned char *data) {
    ImageTask *nueva_tarea = malloc(sizeof(ImageTask));
    nueva_tarea->sockfd = sockfd;
    nueva_tarea->header = header;
    nueva_tarea->data = malloc(header.size);
    memcpy(nueva_tarea->data, data, header.size);
    nueva_tarea->next = NULL;
    
    pthread_mutex_lock(&cola_mutex);
    
    // Insertar ordenado por tamaño (menor a mayor)
    if (cola_imagenes == NULL || header.size < cola_imagenes->header.size) {
        // Insertar al principio
        nueva_tarea->next = cola_imagenes;
        cola_imagenes = nueva_tarea;
    } else {
        // Buscar posición correcta
        ImageTask *actual = cola_imagenes;
        while (actual->next != NULL && actual->next->header.size <= header.size) {
            actual = actual->next;
        }
        nueva_tarea->next = actual->next;
        actual->next = nueva_tarea;
    }
    
    pthread_mutex_unlock(&cola_mutex);
    pthread_cond_signal(&cola_condition); // Avisar al procesador
    
    printf("Imagen añadida a cola de prioridad (tamaño: %d bytes)\n", header.size);
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
                printf("¿Desea enviar otra imagen? (send/exit): ");
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
        else if (strncmp(buff, "help", 4) == 0) {
            printf("\nComandos disponibles:\n");
            printf("  send - Enviar imagen para procesar\n");
            printf("  exit - Salir del cliente\n");
            printf("  help - Mostrar esta ayuda\n\n");
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


// docker build -t client .
// docker run --rm --network=host -it client
// localhost / 127.0.0.1
// ip a