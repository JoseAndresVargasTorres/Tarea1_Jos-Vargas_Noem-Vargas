#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
    char filename[64];
    ImageTask *next;
};

// Estructura para cliente con su cola de imágenes
typedef struct ClientSession ClientSession;
struct ClientSession {
    int sockfd;
    char cliente_ip[32];
    ImageTask *imagenes_cliente;  // Cola de imágenes del cliente
    int num_imagenes;            // Contador de imágenes del cliente
    int procesamiento_iniciado;  // Flag para saber si ya inició el procesamiento
    ClientSession *next;
};

// Variables globales
ImageTask *cola_imagenes = NULL;
ClientSession *clientes_activos = NULL;  // Lista de clientes activos
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientes_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_condition = PTHREAD_COND_INITIALIZER;
int servidor_activo = 1;
int imagenes_procesadas = 0;
int clientes_conectados = 0;

// Declaraciones de funciones
int Histograma_Ecualizacion(unsigned char *pixels, int ancho, int alto);
void insertar_en_cola_cliente(int sockfd, ImageHeader header, unsigned char *data, const char *cliente_ip, const char *filename);
void iniciar_procesamiento_cliente(int sockfd);
void* procesador_imagenes(void* arg);
void* manejar_cliente(void* arg);
void recibir_imagen(int connfd, const char *cliente_ip);
void mostrar_estadisticas();
ClientSession* encontrar_cliente(int sockfd);
void crear_cliente(int sockfd, const char *cliente_ip);
void remover_cliente(int sockfd);

// Función para crear nuevo cliente
void crear_cliente(int sockfd, const char *cliente_ip) {
    pthread_mutex_lock(&clientes_mutex);
    
    ClientSession *nuevo_cliente = malloc(sizeof(ClientSession));
    nuevo_cliente->sockfd = sockfd;
    strcpy(nuevo_cliente->cliente_ip, cliente_ip);
    nuevo_cliente->imagenes_cliente = NULL;
    nuevo_cliente->num_imagenes = 0;
    nuevo_cliente->procesamiento_iniciado = 0;
    nuevo_cliente->next = clientes_activos;
    clientes_activos = nuevo_cliente;
    
    printf("[CLIENTE] %s registrado para sesión de imágenes\n", cliente_ip);
    
    pthread_mutex_unlock(&clientes_mutex);
}

// Función para encontrar cliente por sockfd
ClientSession* encontrar_cliente(int sockfd) {
    pthread_mutex_lock(&clientes_mutex);
    
    ClientSession *cliente = clientes_activos;
    while (cliente != NULL) {
        if (cliente->sockfd == sockfd) {
            pthread_mutex_unlock(&clientes_mutex);
            return cliente;
        }
        cliente = cliente->next;
    }
    
    pthread_mutex_unlock(&clientes_mutex);
    return NULL;
}

// Función para remover cliente de la lista
void remover_cliente(int sockfd) {
    pthread_mutex_lock(&clientes_mutex);
    
    ClientSession *actual = clientes_activos;
    ClientSession *anterior = NULL;
    
    while (actual != NULL) {
        if (actual->sockfd == sockfd) {
            if (anterior == NULL) {
                clientes_activos = actual->next;
            } else {
                anterior->next = actual->next;
            }
            
            // Liberar imágenes pendientes del cliente
            ImageTask *img = actual->imagenes_cliente;
            while (img != NULL) {
                ImageTask *temp = img;
                img = img->next;
                free(temp->data);
                free(temp);
            }
            
            printf("[CLIENTE] %s removido de sesión\n", actual->cliente_ip);
            free(actual);
            break;
        }
        anterior = actual;
        actual = actual->next;
    }
    
    pthread_mutex_unlock(&clientes_mutex);
}

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

// Función para almacenar imagen en la cola del cliente
void insertar_en_cola_cliente(int sockfd, ImageHeader header, unsigned char *data, const char *cliente_ip, const char *filename) {
    ClientSession *cliente = encontrar_cliente(sockfd);
    if (!cliente) {
        printf("[ERROR] Cliente no encontrado para almacenar imagen\n");
        return;
    }
    
    ImageTask *nueva_imagen = malloc(sizeof(ImageTask));
    nueva_imagen->sockfd = sockfd;
    nueva_imagen->header = header;
    nueva_imagen->data = malloc(header.size);
    memcpy(nueva_imagen->data, data, header.size);
    strcpy(nueva_imagen->cliente_ip, cliente_ip);
    strcpy(nueva_imagen->filename, filename);
    nueva_imagen->next = NULL;
    
    // Agregar al final de la cola del cliente
    if (cliente->imagenes_cliente == NULL) {
        cliente->imagenes_cliente = nueva_imagen;
    } else {
        ImageTask *temp = cliente->imagenes_cliente;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = nueva_imagen;
    }
    
    cliente->num_imagenes++;
    printf("[ALMACENADO] Imagen %s de %s guardada en memoria (%d/%d en cola)\n", 
           filename, cliente_ip, cliente->num_imagenes, cliente->num_imagenes);
}

// Función para iniciar procesamiento cuando cliente envía EXIT
void iniciar_procesamiento_cliente(int sockfd) {
    ClientSession *cliente = encontrar_cliente(sockfd);
    if (!cliente || cliente->procesamiento_iniciado) {
        return;
    }
    
    cliente->procesamiento_iniciado = 1;
    printf("[INICIANDO] Procesamiento de %d imágenes de %s\n", 
           cliente->num_imagenes, cliente->cliente_ip);
    
    // Transferir imágenes del cliente a la cola principal ordenada por tamaño
    pthread_mutex_lock(&cola_mutex);
    
    ImageTask *img_cliente = cliente->imagenes_cliente;
    while (img_cliente != NULL) {
        ImageTask *siguiente = img_cliente->next;
        img_cliente->next = NULL;
        
        // Insertar en cola principal ordenada por tamaño (menor primero)
        if (cola_imagenes == NULL || img_cliente->header.size < cola_imagenes->header.size) {
            img_cliente->next = cola_imagenes;
            cola_imagenes = img_cliente;
        } else {
            ImageTask *actual = cola_imagenes;
            while (actual->next != NULL && actual->next->header.size < img_cliente->header.size) {
                actual = actual->next;
            }
            img_cliente->next = actual->next;
            actual->next = img_cliente;
        }
        
        img_cliente = siguiente;
    }
    
    cliente->imagenes_cliente = NULL;  // Ya no necesitamos la cola del cliente
    
    // Mostrar estado de la cola de procesamiento
    printf("[COLA PROCESAMIENTO] Estado actual (menor a mayor): ");
    ImageTask *temp = cola_imagenes;
    int posicion = 1;
    while (temp != NULL) {
        printf("%d°(%s:%d bytes) ", posicion++, temp->filename, temp->header.size);
        temp = temp->next;
    }
    printf("\n");
    
    pthread_mutex_unlock(&cola_mutex);
    pthread_cond_broadcast(&cola_condition); // Despertar procesadores
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
        
        // Tomar la primera tarea (mayor prioridad - menor tamaño)
        ImageTask *tarea = cola_imagenes;
        cola_imagenes = cola_imagenes->next;
        
        pthread_mutex_unlock(&cola_mutex);
        
        // Procesar imagen
        printf("[PROCESANDO] Imagen %s de %s (%dx%d, %d bytes)\n", 
               tarea->filename, tarea->cliente_ip, 
               tarea->header.ancho, tarea->header.alto, tarea->header.size);
        
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
        char *punto = strrchr(tarea->filename, '.');
        if (punto) {
            *punto = '\0';
            snprintf(nombre_archivo, sizeof(nombre_archivo), 
                    "%s_%s_processed_%ld.jpg", color_dir, tarea->filename, time(NULL));
            *punto = '.';
        } else {
            snprintf(nombre_archivo, sizeof(nombre_archivo), 
                    "%s_%s_processed_%ld.jpg", color_dir, tarea->filename, time(NULL));
        }
        
        stbi_write_jpg(nombre_archivo, tarea->header.ancho, tarea->header.alto, 1, tarea->data, 90);
        printf("[GUARDADO] %s procesada y clasificada como '%s'\n", tarea->filename, color_dir);
        
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
            printf("[ENVIADO] Imagen %s procesada devuelta al cliente\n", tarea->filename);
        }
        
        imagenes_procesadas++;
        
        // Liberar memoria
        free(tarea->data);
        free(tarea);
    }
    return NULL;
}

// Función para recibir imagen y almacenarla (no procesarla aún)
void recibir_imagen(int connfd, const char *cliente_ip) {
    ImageHeader header;
    
    // *** MEJORA: LIMPIAR EL HEADER ANTES DE LEER ***
    memset(&header, 0, sizeof(ImageHeader));
    
    int header_bytes = read(connfd, &header, sizeof(ImageHeader));
    if (header_bytes <= 0) {
        printf("[ERROR] Error recibiendo header de %s\n", cliente_ip);
        return;
    }
    
    // *** MEJORA: VALIDACIÓN MEJORADA CON MENSAJES DE DEBUG ***
    printf("[DEBUG] Header recibido de %s: %dx%d (%d bytes)\n", 
           cliente_ip, header.ancho, header.alto, header.size);
    
    // VALIDACIÓN CRÍTICA: Verificar que el header sea válido
    if (header.ancho <= 0 || header.alto <= 0 || header.size <= 0 || 
        header.ancho > 10000 || header.alto > 10000 || header.size > 50000000 ||
        header.size != (header.ancho * header.alto)) {  // *** NUEVA VALIDACIÓN ***
        
        printf("[ERROR] Header inválido de %s: %dx%d (%d bytes) - RECHAZANDO\n", 
               cliente_ip, header.ancho, header.alto, header.size);
        printf("[ERROR] Tamaño calculado esperado: %d bytes\n", header.ancho * header.alto);
        
        // Enviar error al cliente
        const char *error_msg = "ERROR_HEADER";
        write(connfd, error_msg, strlen(error_msg));
        return;
    }
    
    printf("[RECIBIENDO] De %s: %dx%d (%d bytes) - HEADER VÁLIDO\n", 
           cliente_ip, header.ancho, header.alto, header.size);
    
    unsigned char *imagen_buffer = malloc(header.size);
    if (!imagen_buffer) {
        printf("[ERROR] Memoria insuficiente para imagen de %s\n", cliente_ip);
        const char *error_msg = "ERROR_MEMORIA";
        write(connfd, error_msg, strlen(error_msg));
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
            const char *error_msg = "ERROR_DATOS";
            write(connfd, error_msg, strlen(error_msg));
            return;
        }
        
        bytes_recibidos += resultado;
    }
    
    printf("[RECIBIDO] Imagen completa de %s - ALMACENADA para procesamiento posterior\n", cliente_ip);
    
    // Almacenar en cola del cliente (no procesar aún)
    char filename[64];
    snprintf(filename, sizeof(filename), "image_%ld.jpg", time(NULL));
    insertar_en_cola_cliente(connfd, header, imagen_buffer, cliente_ip, filename);
    
    free(imagen_buffer);
    
    // Enviar confirmación de recepción al cliente
    const char *confirmacion = "OK";
    write(connfd, confirmacion, strlen(confirmacion) + 1);
}

// Hilo para manejar cada cliente
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
    
    // Crear entrada para el cliente
    crear_cliente(sockfd, cliente_ip);
    
    char buffer[MAX];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        
        int bytes_leidos = read(sockfd, buffer, sizeof(buffer) - 1);
        if (bytes_leidos <= 0) {
            printf("[DESCONEXION] Cliente %s se desconectó\n", cliente_ip);
            break;
        }
        
        buffer[bytes_leidos] = '\0';
        
        printf("[COMANDO] Recibido de %s (%d bytes): '%.50s%s'\n", 
               cliente_ip, bytes_leidos, buffer, bytes_leidos > 50 ? "..." : "");
        
        if (strncmp(buffer, "IMAGE", 5) == 0) {
            printf("[PROCESANDO] Comando IMAGE de %s\n", cliente_ip);
            // INMEDIATAMENTE después de leer "IMAGE", leer la imagen
            recibir_imagen(sockfd, cliente_ip);
            
        } else if (strncmp(buffer, "exit", 4) == 0 || strncmp(buffer, "EXIT", 4) == 0) {
            printf("[CLIENTE] %s envió EXIT - INICIANDO PROCESAMIENTO\n", cliente_ip);
            iniciar_procesamiento_cliente(sockfd);
            break;
            
        } else {
            // Si no es un comando válido, verificar si son datos binarios
            int datos_binarios = 0;
            for (int i = 0; i < bytes_leidos && i < 50; i++) {
                if (buffer[i] < 32 && buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] != '\t') {
                    datos_binarios = 1;
                    break;
                }
            }
            
            if (datos_binarios) {
                printf("[ERROR] Datos binarios recibidos como comando de %s - DESCARTANDO\n", cliente_ip);
                // Descartar datos hasta encontrar próximo comando válido
                continue;
            } else {
                printf("[ADVERTENCIA] Comando no reconocido de %s: '%.30s%s'\n", 
                       cliente_ip, buffer, bytes_leidos > 30 ? "..." : "");
            }
        }
    }
    
    // Mantener conexión para enviar imágenes procesadas
    printf("[CLIENTE] %s terminó envío - esperando imágenes procesadas\n", cliente_ip);
    
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
    printf("Imágenes en cola de procesamiento: %d\n", en_cola);
    pthread_mutex_unlock(&cola_mutex);
    
    // Mostrar clientes activos y sus imágenes pendientes
    pthread_mutex_lock(&clientes_mutex);
    ClientSession *cliente = clientes_activos;
    while (cliente != NULL) {
        printf("Cliente %s: %d imágenes almacenadas, procesamiento %s\n", 
               cliente->cliente_ip, cliente->num_imagenes,
               cliente->procesamiento_iniciado ? "INICIADO" : "PENDIENTE");
        cliente = cliente->next;
    }
    pthread_mutex_unlock(&clientes_mutex);
    
    printf("Estado: ACTIVO\n");
    printf("===============================\n\n");
}

int main() {
    int sockfd, len;
    struct sockaddr_in servaddr, cli;
    
    printf("=== IMAGESERVER - Procesamiento con Cola de Prioridad (MODO SECUENCIAL) ===\n");
    printf("Las imágenes se almacenan hasta recibir EXIT, luego se procesan por tamaño\n");
    printf("*** VERSIÓN MEJORADA - Buffer limpio y validación robusta ***\n");
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
    printf("Modo: Almacenar imágenes → Esperar EXIT → Procesar por prioridad\n");
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
        
        // Mostrar estadísticas cada 3 clientes nuevos
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

/*
Compilación:
gcc sn.c -o servidor -lm -lpthread

CAMBIOS IMPLEMENTADOS EN ESTA VERSIÓN:
1. Limpieza del buffer antes de leer headers
2. Validación mejorada con mensajes de debug
3. Validación adicional: header.size == (header.ancho * header.alto)
4. Limpieza de buffer en el manejo de clientes
5. Mensajes de debug para rastrear el problema
6. Pequeña pausa para estabilizar conexiones nuevas

Funcionamiento modificado:
1. Cliente envía imágenes una por una
2. Servidor almacena cada imagen en memoria (no procesa)
3. Cuando cliente envía "EXIT", servidor inicia procesamiento por prioridad
4. Servidor procesa y devuelve imágenes al cliente en orden de prioridad
*/