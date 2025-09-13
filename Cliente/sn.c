#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#define MAX 80
#define PORT 1717
#define SA struct sockaddr

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Estructura para información de imagen
typedef struct {
    int ancho;
    int alto;
    int size;
} ImageHeader;

// Estructura para cola de prioridad
typedef struct ImageTask ImageTask;
struct ImageTask {
    int sockfd;
    ImageHeader header;
    unsigned char *data;
    char cliente_ip[32];
    ImageTask *next;
};

// Variables globales
ImageTask *cola_imagenes = NULL;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_condition = PTHREAD_COND_INITIALIZER;
int servidor_activo = 1;
int imagenes_procesadas = 0;
int clientes_conectados = 0;

// Declaraciones de funciones
int Histograma_Ecualizacion(unsigned char *pixels, int ancho, int alto);
void insertar_en_cola(int sockfd, ImageHeader header, unsigned char *data, const char *cliente_ip);
void* procesador_imagenes(void* arg);
void* manejar_cliente(void* arg);
void recibir_imagen(int connfd, const char *cliente_ip);
void mostrar_estadisticas();

// Función de histograma de ecualización
int Histograma_Ecualizacion(unsigned char *pixels, int ancho, int alto) {
    int frecuencia[256] = {0};
    int frecuencia_acumulada[256] = {0};
    int total_pixels = ancho * alto;

    // Contar pixeles de cada intensidad 
    for(int i = 0; i < total_pixels; i++){
        unsigned char valor = pixels[i];
        frecuencia[valor]++;
    }

    // Calcular frecuencia acumulada
    frecuencia_acumulada[0] = frecuencia[0];
    for(int i = 1; i < 256; i++){
        frecuencia_acumulada[i] = frecuencia_acumulada[i-1] + frecuencia[i];
    }

    // Aplicar ecualización
    for(int i = 0; i < total_pixels; i++){
        unsigned char nuevo_pixel = (frecuencia_acumulada[pixels[i]] * 255) / total_pixels;
        pixels[i] = nuevo_pixel;
    }
    
    return 0;
}

// Función para insertar imagen en cola por prioridad (menor tamaño = mayor prioridad)
void insertar_en_cola(int sockfd, ImageHeader header, unsigned char *data, const char *cliente_ip) {
    ImageTask *nueva_tarea = malloc(sizeof(ImageTask));
    nueva_tarea->sockfd = sockfd;
    nueva_tarea->header = header;
    nueva_tarea->data = malloc(header.size);
    memcpy(nueva_tarea->data, data, header.size);
    strcpy(nueva_tarea->cliente_ip, cliente_ip);
    nueva_tarea->next = NULL;
    
    pthread_mutex_lock(&cola_mutex);
    
    // Insertar ordenado por tamaño (menor a mayor = mayor prioridad)
    if (cola_imagenes == NULL || header.size < cola_imagenes->header.size) {
        // Insertar al principio (mayor prioridad)
        nueva_tarea->next = cola_imagenes;
        cola_imagenes = nueva_tarea;
        printf("[COLA] Imagen de %s añadida con ALTA prioridad (tamaño: %d bytes)\n", 
               cliente_ip, header.size);
    } else {
        // Buscar posición correcta
        ImageTask *actual = cola_imagenes;
        while (actual->next != NULL && actual->next->header.size <= header.size) {
            actual = actual->next;
        }
        nueva_tarea->next = actual->next;
        actual->next = nueva_tarea;
        printf("[COLA] Imagen de %s añadida en cola (tamaño: %d bytes)\n", 
               cliente_ip, header.size);
    }
    
    // Mostrar estado de la cola
    printf("[COLA] Estado actual: ");
    ImageTask *temp = cola_imagenes;
    int posicion = 1;
    while (temp != NULL) {
        printf("%d°(%d bytes) ", posicion++, temp->header.size);
        temp = temp->next;
    }
    printf("\n");
    
    pthread_mutex_unlock(&cola_mutex);
    pthread_cond_signal(&cola_condition); // Despertar al procesador
}

// Hilo dedicado para procesar imágenes por prioridad
void* procesador_imagenes(void* arg) {
    while (servidor_activo) {
        pthread_mutex_lock(&cola_mutex);
        
        // Esperar hasta que haya tareas en la cola
        while (cola_imagenes == NULL && servidor_activo) {
            pthread_cond_wait(&cola_condition, &cola_mutex);
        }
        
        if (!servidor_activo) {
            pthread_mutex_unlock(&cola_mutex);
            break;
        }
        
        // Tomar la primera tarea (mayor prioridad)
        ImageTask *tarea = cola_imagenes;
        cola_imagenes = cola_imagenes->next;
        
        pthread_mutex_unlock(&cola_mutex);
        
        // Procesar imagen
        printf("[PROCESANDO] Imagen de %s (%dx%d, %d bytes)\n", 
               tarea->cliente_ip, tarea->header.ancho, tarea->header.alto, tarea->header.size);
        
        // Aplicar histograma de ecualización
        Histograma_Ecualizacion(tarea->data, tarea->header.ancho, tarea->header.alto);
        
        // Determinar color predominante para directorio
        long suma = 0;
        for (int i = 0; i < tarea->header.size; i++) {
            suma += tarea->data[i];
        }
        int intensidad_promedio = suma / tarea->header.size;
        
        const char *color_dir;
        if (intensidad_promedio < 85) {
            color_dir = "azules";
        } else if (intensidad_promedio < 170) {
            color_dir = "rojas";
        } else {
            color_dir = "verdes";
        }
        
        // Guardar imagen procesada
        char nombre_archivo[256];
        snprintf(nombre_archivo, sizeof(nombre_archivo), 
                "%s_processed_%ld.jpg", color_dir, time(NULL));
        
        stbi_write_jpg(nombre_archivo, tarea->header.ancho, tarea->header.alto, 1, tarea->data, 90);
        printf("[GUARDADO] %s clasificada como '%s'\n", nombre_archivo, color_dir);
        
        // Enviar imagen procesada de vuelta al cliente
        if (write(tarea->sockfd, &tarea->header, sizeof(ImageHeader)) > 0) {
            int bytes_enviados = 0;
            while (bytes_enviados < tarea->header.size) {
                int bytes_restantes = tarea->header.size - bytes_enviados;
                int chunk_size = (bytes_restantes > 4096) ? 4096 : bytes_restantes;
                
                int resultado = write(tarea->sockfd, tarea->data + bytes_enviados, chunk_size);
                if (resultado <= 0) break;
                bytes_enviados += resultado;
            }
            printf("[ENVIADO] Imagen procesada devuelta a %s\n", tarea->cliente_ip);
        }
        
        imagenes_procesadas++;
        
        // Liberar memoria
        free(tarea->data);
        free(tarea);
    }
    return NULL;
}

// Función para recibir imagen y añadirla a cola
void recibir_imagen(int connfd, const char *cliente_ip) {
    ImageHeader header;
    
    if (read(connfd, &header, sizeof(ImageHeader)) <= 0) {
        printf("[ERROR] Error recibiendo header de %s\n", cliente_ip);
        return;
    }
    
    printf("[RECIBIENDO] De %s: %dx%d (%d bytes)\n", 
           cliente_ip, header.ancho, header.alto, header.size);
    
    unsigned char *imagen_buffer = malloc(header.size);
    if (!imagen_buffer) {
        printf("[ERROR] Memoria insuficiente para imagen de %s\n", cliente_ip);
        return;
    }
    
    // Recibir datos de imagen
    int bytes_recibidos = 0;
    while (bytes_recibidos < header.size) {
        int bytes_restantes = header.size - bytes_recibidos;
        int resultado = read(connfd, imagen_buffer + bytes_recibidos, bytes_restantes);
        
        if (resultado <= 0) {
            printf("[ERROR] Fallo recibiendo datos de %s\n", cliente_ip);
            free(imagen_buffer);
            return;
        }
        
        bytes_recibidos += resultado;
    }
    
    printf("[RECIBIDO] Imagen completa de %s\n", cliente_ip);
    
    // Añadir a cola de prioridad
    insertar_en_cola(connfd, header, imagen_buffer, cliente_ip);
    
    free(imagen_buffer); // La cola hace su propia copia
}

// Hilo para manejar cada cliente
void* manejar_cliente(void* arg) {
    int sockfd = *(int*)arg;
    free(arg);
    
    clientes_conectados++;
    
    struct sockaddr_in cliente_addr;
    socklen_t len = sizeof(cliente_addr);
    getpeername(sockfd, (struct sockaddr*)&cliente_addr, &len);
    
    char cliente_ip[32];
    snprintf(cliente_ip, sizeof(cliente_ip), "%s", inet_ntoa(cliente_addr.sin_addr));
    
    printf("[CLIENTE] %s conectado (Total: %d)\n", cliente_ip, clientes_conectados);
    
    char buffer[MAX];
    while (1) {
        int bytes_leidos = read(sockfd, buffer, sizeof(buffer) - 1);
        if (bytes_leidos <= 0) {
            break; // Cliente desconectado
        }
        
        buffer[bytes_leidos] = '\0';
        
        if (strncmp(buffer, "IMAGE", 5) == 0) {
            recibir_imagen(sockfd, cliente_ip);
        } else if (strncmp(buffer, "exit", 4) == 0) {
            printf("[CLIENTE] %s solicitó desconexión\n", cliente_ip);
            break;
        }
    }
    
    close(sockfd);
    clientes_conectados--;
    printf("[CLIENTE] %s desconectado (Total: %d)\n", cliente_ip, clientes_conectados);
    return NULL;
}

// Función para mostrar estadísticas del servidor
void mostrar_estadisticas() {
    printf("\n=== ESTADÍSTICAS DEL SERVIDOR ===\n");
    printf("Puerto: %d\n", PORT);
    printf("Clientes conectados: %d\n", clientes_conectados);
    printf("Imágenes procesadas: %d\n", imagenes_procesadas);
    
    pthread_mutex_lock(&cola_mutex);
    int en_cola = 0;
    ImageTask *temp = cola_imagenes;
    while (temp != NULL) {
        en_cola++;
        temp = temp->next;
    }
    printf("Imágenes en cola: %d\n", en_cola);
    pthread_mutex_unlock(&cola_mutex);
    
    printf("Estado: ACTIVO\n");
    printf("===============================\n\n");
}

int main() {
    int sockfd, len;
    struct sockaddr_in servaddr, cli;
    
    printf("=== IMAGESERVER - Procesamiento con Cola de Prioridad ===\n");
    printf("Iniciando servidor en puerto %d...\n", PORT);
    
    // Crear hilo procesador de imágenes
    pthread_t procesador_thread;
    pthread_create(&procesador_thread, NULL, procesador_imagenes, NULL);
    
    // Crear socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Error creando socket\n");
        exit(0);
    }
    
    // Permitir reutilizar dirección
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    
    if (bind(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("Error en bind\n");
        exit(0);
    }
    
    if (listen(sockfd, 5) != 0) {
        printf("Error en listen\n");
        exit(0);
    }
    
    printf("Servidor escuchando...\n");
    printf("Presiona Ctrl+C para mostrar estadísticas\n\n");
    
    // Loop principal para aceptar clientes
    while (1) {
        len = sizeof(cli);
        int *connfd = malloc(sizeof(int));
        *connfd = accept(sockfd, (SA*)&cli, &len);
        
        if (*connfd < 0) {
            free(connfd);
            continue;
        }
        
        // Crear hilo para manejar cliente
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, manejar_cliente, connfd);
        pthread_detach(client_thread);
        
        // Mostrar estadísticas cada 5 clientes nuevos
        if (clientes_conectados % 3 == 0) {
            mostrar_estadisticas();
        }
    }
    
    // Limpiar al salir
    servidor_activo = 0;
    pthread_cond_broadcast(&cola_condition);
    pthread_join(procesador_thread, NULL);
    close(sockfd);
    
    return 0;
}