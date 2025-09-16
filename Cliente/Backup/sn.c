#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#define MAX 80
#define PORT 1717
#define SA struct sockaddr

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Estructura para información de imagen RGB
typedef struct {
    int ancho;
    int alto;
    int size;        // ancho * alto * canales
    int canales;     // 3 para RGB del cliente, 1 para procesamiento
} ImageHeader;

// Estructura para cola de prioridad
typedef struct ImageTask ImageTask;
struct ImageTask {
    int sockfd;
    ImageHeader header;
    unsigned char *data;          // Datos en escala de grises para procesamiento
    unsigned char *rgb_data;      // Datos RGB para clasificación (EN MEMORIA)
    char cliente_ip[32];
    char filename[64];
    ImageTask *next;
};

// Estructura para cliente con su cola de imágenes
typedef struct ClientSession ClientSession;
struct ClientSession {
    int sockfd;
    char cliente_ip[32];
    ImageTask *imagenes_cliente;
    int num_imagenes;
    int procesamiento_iniciado;
    ClientSession *next;
};

// Variables globales
ImageTask *cola_imagenes = NULL;
ClientSession *clientes_activos = NULL;
pthread_mutex_t cola_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientes_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cola_condition = PTHREAD_COND_INITIALIZER;
int servidor_activo = 1;
int imagenes_procesadas = 0;
int clientes_conectados = 0;

// Declaraciones de funciones
int Histograma_Ecualizacion(unsigned char *pixels, int ancho, int alto);
const char* clasificar_color_predominante_rgb(unsigned char *imagen_rgb, int ancho, int alto);
void crear_directorios_colores();
void insertar_en_cola_cliente(int sockfd, ImageHeader header, unsigned char *data, unsigned char *rgb_data, const char *cliente_ip, const char *filename);
void iniciar_procesamiento_cliente(int sockfd);
void* procesador_imagenes(void* arg);
void* manejar_cliente(void* arg);
void recibir_imagen_rgb(int connfd, const char *cliente_ip);
void mostrar_estadisticas();
ClientSession* encontrar_cliente(int sockfd);
void crear_cliente(int sockfd, const char *cliente_ip);
void remover_cliente(int sockfd);

// FUNCIÓN: Crear directorios incluyendo img_ecualizadas
void crear_directorios_colores() {
    mkdir("verde", 0755);
    mkdir("azul", 0755);
    mkdir("rojo", 0755);
    mkdir("img_ecualizadas", 0755);
    printf("[SETUP] Directorios creados: verde/, azul/, rojo/, img_ecualizadas/\n");
}

// FUNCIÓN MEJORADA: Clasificar color usando datos RGB en memoria (SIN archivos temporales)
const char* clasificar_color_predominante_rgb(unsigned char *imagen_rgb, int ancho, int alto) {
    if (!imagen_rgb) {
        printf("[ERROR] Datos RGB nulos para clasificación\n");
        return "rojo"; // Default
    }
    
    printf("[CLASIFICACION] Analizando datos RGB en memoria (%dx%d)\n", ancho, alto);
    
    long suma_rojo = 0, suma_verde = 0, suma_azul = 0;
    int total_pixels = ancho * alto;
    
    // Sumar valores RGB reales desde memoria
    for (int i = 0; i < total_pixels * 3; i += 3) {
        suma_rojo += imagen_rgb[i];     // R
        suma_verde += imagen_rgb[i + 1]; // G  
        suma_azul += imagen_rgb[i + 2];  // B
    }
    
    // Calcular promedios
    double promedio_rojo = (double)suma_rojo / total_pixels;
    double promedio_verde = (double)suma_verde / total_pixels;
    double promedio_azul = (double)suma_azul / total_pixels;
    
    printf("[CLASIFICACION] Promedios RGB: R=%.2f, G=%.2f, B=%.2f\n", 
           promedio_rojo, promedio_verde, promedio_azul);
    
    // Determinar color predominante
    const char *color_dir;
    double umbral = 3.0;
    
    double diff_rojo_verde = fabs(promedio_rojo - promedio_verde);
    double diff_rojo_azul = fabs(promedio_rojo - promedio_azul);
    double diff_verde_azul = fabs(promedio_verde - promedio_azul);
    
    if (promedio_rojo > promedio_verde && promedio_rojo > promedio_azul && 
        (diff_rojo_verde > umbral || diff_rojo_azul > umbral)) {
        color_dir = "rojo";
    } else if (promedio_verde > promedio_rojo && promedio_verde > promedio_azul && 
               (diff_verde_azul > umbral || diff_rojo_verde > umbral)) {
        color_dir = "verde";
    } else if (promedio_azul > promedio_rojo && promedio_azul > promedio_verde && 
               (diff_verde_azul > umbral || diff_rojo_azul > umbral)) {
        color_dir = "azul";
    } else {
        // Empate - usar el mayor
        if (promedio_rojo >= promedio_verde && promedio_rojo >= promedio_azul) {
            color_dir = "rojo";
        } else if (promedio_verde >= promedio_azul) {
            color_dir = "verde";
        } else {
            color_dir = "azul";
        }
        printf("[CLASIFICACION] Valores cercanos, usando color máximo\n");
    }
    
    printf("[CLASIFICACION] Color predominante detectado: %s\n", color_dir);
    return color_dir;
}

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
    
    printf("[CLIENTE] %s registrado para sesión RGB\n", cliente_ip);
    
    pthread_mutex_unlock(&clientes_mutex);
}

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
            
            ImageTask *img = actual->imagenes_cliente;
            while (img != NULL) {
                ImageTask *temp = img;
                img = img->next;
                free(temp->data);
                free(temp->rgb_data); // Liberar datos RGB
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

// FUNCIÓN: Histograma de ecualización mejorada
int Histograma_Ecualizacion(unsigned char *pixels, int ancho, int alto) {
    int frecuencia[256] = {0};
    int frecuencia_acumulada[256] = {0};
    int total_pixels = ancho * alto;

    printf("[HISTOGRAMA] Aplicando ecualización a imagen %dx%d (%d pixels)\n", 
           ancho, alto, total_pixels);

    // Contar frecuencias
    for(int i = 0; i < total_pixels; i++){
        unsigned char valor = pixels[i];
        frecuencia[valor]++;
    }

    // Calcular frecuencia acumulada (CDF)
    frecuencia_acumulada[0] = frecuencia[0];
    for(int i = 1; i < 256; i++){
        frecuencia_acumulada[i] = frecuencia_acumulada[i-1] + frecuencia[i];
    }

    // Aplicar ecualización usando la fórmula estándar
    for(int i = 0; i < total_pixels; i++){
        unsigned char nuevo_pixel = (frecuencia_acumulada[pixels[i]] * 255) / total_pixels;
        pixels[i] = nuevo_pixel;
    }
    
    printf("[HISTOGRAMA] Ecualización aplicada exitosamente\n");
    return 0;
}

void insertar_en_cola_cliente(int sockfd, ImageHeader header, unsigned char *data, unsigned char *rgb_data, const char *cliente_ip, const char *filename) {
    ClientSession *cliente = encontrar_cliente(sockfd);
    if (!cliente) {
        printf("[ERROR] Cliente no encontrado para almacenar imagen\n");
        return;
    }
    
    ImageTask *nueva_imagen = malloc(sizeof(ImageTask));
    nueva_imagen->sockfd = sockfd;
    nueva_imagen->header = header;
    
    // Copiar datos en escala de grises
    nueva_imagen->data = malloc(header.size);
    memcpy(nueva_imagen->data, data, header.size);
    
    // Copiar datos RGB para clasificación
    int rgb_size = header.ancho * header.alto * 3;
    nueva_imagen->rgb_data = malloc(rgb_size);
    memcpy(nueva_imagen->rgb_data, rgb_data, rgb_size);
    
    strcpy(nueva_imagen->cliente_ip, cliente_ip);
    strcpy(nueva_imagen->filename, filename);
    nueva_imagen->next = NULL;
    
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
    printf("[ALMACENADO] Imagen %s de %s en memoria (RGB y escala de grises)\n", 
           filename, cliente_ip);
}

void iniciar_procesamiento_cliente(int sockfd) {
    ClientSession *cliente = encontrar_cliente(sockfd);
    if (!cliente || cliente->procesamiento_iniciado) {
        return;
    }
    
    cliente->procesamiento_iniciado = 1;
    printf("[INICIANDO] Procesamiento RGB de %d imágenes de %s\n", 
           cliente->num_imagenes, cliente->cliente_ip);
    
    pthread_mutex_lock(&cola_mutex);
    
    ImageTask *img_cliente = cliente->imagenes_cliente;
    while (img_cliente != NULL) {
        ImageTask *siguiente = img_cliente->next;
        img_cliente->next = NULL;
        
        // Insertar ordenado por tamaño (menor primero)
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
    
    cliente->imagenes_cliente = NULL;
    
    printf("[COLA] Imágenes RGB ordenadas por prioridad de tamaño\n");
    
    pthread_mutex_unlock(&cola_mutex);
    pthread_cond_broadcast(&cola_condition);
}

// FUNCIÓN CORREGIDA: Procesador SOLO guarda en los directorios correctos
void* procesador_imagenes(void* arg) {
    while (servidor_activo) {
        pthread_mutex_lock(&cola_mutex);
        
        while (cola_imagenes == NULL && servidor_activo) {
            pthread_cond_wait(&cola_condition, &cola_mutex);
        }
        
        if (!servidor_activo) {
            pthread_mutex_unlock(&cola_mutex);
            break;
        }
        
        ImageTask *tarea = cola_imagenes;
        cola_imagenes = cola_imagenes->next;
        
        pthread_mutex_unlock(&cola_mutex);
        
        printf("\n[PROCESANDO] Imagen %s de %s (%dx%d, %d bytes gris)\n", 
               tarea->filename, tarea->cliente_ip, 
               tarea->header.ancho, tarea->header.alto, tarea->header.size);
        
        // PASO 1: Clasificar color usando datos RGB en memoria (SIN archivos temporales)
        printf("[CLASIFICACION] Usando datos RGB en memoria\n");
        const char *color_dir = clasificar_color_predominante_rgb(tarea->rgb_data, 
                                                                 tarea->header.ancho, tarea->header.alto);
        
        // PASO 2: Aplicar histograma de ecualización a datos en escala de grises
        Histograma_Ecualizacion(tarea->data, tarea->header.ancho, tarea->header.alto);
        
        // PASO 3: Generar nombre base para archivos
        char nombre_base[256];
        char *punto = strrchr(tarea->filename, '.');
        if (punto) {
            *punto = '\0';
            snprintf(nombre_base, sizeof(nombre_base), "%s_processed_%ld", tarea->filename, time(NULL));
            *punto = '.';
        } else {
            snprintf(nombre_base, sizeof(nombre_base), "%s_processed_%ld", tarea->filename, time(NULL));
        }
        
        // PASO 4: Guardar SOLO en directorio por color (clasificación)
        char ruta_color[512];
        snprintf(ruta_color, sizeof(ruta_color), "%s/%s.jpg", color_dir, nombre_base);
        
        int guardado_color = stbi_write_jpg(ruta_color, tarea->header.ancho, tarea->header.alto, 1, tarea->data, 90);
        if (guardado_color) {
            printf("[GUARDADO COLOR] %s → %s\n", tarea->filename, ruta_color);
        } else {
            printf("[ERROR COLOR] No se pudo guardar: %s\n", ruta_color);
        }
        
        // PASO 5: Guardar SOLO en img_ecualizadas (TODAS las imágenes procesadas)
        char ruta_general[512];
        snprintf(ruta_general, sizeof(ruta_general), "img_ecualizadas/%s.jpg", nombre_base);
        
        int guardado_general = stbi_write_jpg(ruta_general, tarea->header.ancho, tarea->header.alto, 1, tarea->data, 90);
        if (guardado_general) {
            printf("[GUARDADO GENERAL] %s → %s\n", tarea->filename, ruta_general);
        } else {
            printf("[ERROR GENERAL] No se pudo guardar: %s\n", ruta_general);
        }
        
        // VERIFICACIÓN: Confirmar guardado SOLO en directorios correctos
        if (guardado_color && guardado_general) {
            printf("[GUARDADO CORRECTO] ✓ Color: %s | ✓ General: %s\n", ruta_color, ruta_general);
            printf("[CONFIRMADO] Imagen guardada ÚNICAMENTE en directorios designados\n");
        } else {
            printf("[ADVERTENCIA] Guardado parcial - Color: %s | General: %s\n", 
                   guardado_color ? "OK" : "FALLO", guardado_general ? "OK" : "FALLO");
        }
        
        // PASO 6: Enviar imagen procesada al cliente
        if (write(tarea->sockfd, &tarea->header, sizeof(ImageHeader)) > 0) {
            int bytes_enviados = 0;
            while (bytes_enviados < tarea->header.size) {
                int bytes_restantes = tarea->header.size - bytes_enviados;
                int chunk_size = (bytes_restantes > 4096) ? 4096 : bytes_restantes;
                
                int resultado = write(tarea->sockfd, tarea->data + bytes_enviados, chunk_size);
                if (resultado <= 0) break;
                bytes_enviados += resultado;
            }
            printf("[ENVIADO] Imagen procesada devuelta al cliente\n");
        }
        
        imagenes_procesadas++;
        
        // PASO 7: Liberar memoria (NO hay archivos temporales que eliminar)
        free(tarea->data);
        free(tarea->rgb_data);
        free(tarea);
        
        printf("[COMPLETADO] Imagen procesada sin archivos temporales\n");
    }
    return NULL;
}

// FUNCIÓN CORREGIDA: Recibir imagen RGB del cliente (SIN crear archivos temporales)
void recibir_imagen_rgb(int connfd, const char *cliente_ip) {
    ImageHeader header;
    memset(&header, 0, sizeof(ImageHeader));
    
    int header_bytes = read(connfd, &header, sizeof(ImageHeader));
    if (header_bytes <= 0) {
        printf("[ERROR] Error recibiendo header RGB de %s\n", cliente_ip);
        return;
    }
    
    printf("[DEBUG] Header RGB de %s: %dx%d, %d canales (%d bytes)\n", 
           cliente_ip, header.ancho, header.alto, header.canales, header.size);
    
    // Validar header RGB
    if (header.ancho <= 0 || header.alto <= 0 || header.size <= 0 || header.canales != 3 ||
        header.ancho > 10000 || header.alto > 10000 || header.size > 50000000 ||
        header.size != (header.ancho * header.alto * header.canales)) {
        
        printf("[ERROR] Header RGB inválido de %s: %dx%d, %d canales (%d bytes)\n", 
               cliente_ip, header.ancho, header.alto, header.canales, header.size);
        
        const char *error_msg = "ERROR_HEADER";
        write(connfd, error_msg, strlen(error_msg));
        return;
    }
    
    printf("[RECIBIENDO] RGB de %s: %dx%d, %d canales (%d bytes) - VÁLIDO\n", 
           cliente_ip, header.ancho, header.alto, header.canales, header.size);
    
    // Alocar memoria para datos RGB
    unsigned char *imagen_rgb_buffer = malloc(header.size);
    if (!imagen_rgb_buffer) {
        printf("[ERROR] Memoria insuficiente para RGB de %s\n", cliente_ip);
        const char *error_msg = "ERROR_MEMORIA";
        write(connfd, error_msg, strlen(error_msg));
        return;
    }
    
    // Recibir datos RGB
    int bytes_recibidos = 0;
    while (bytes_recibidos < header.size) {
        int bytes_restantes = header.size - bytes_recibidos;
        int resultado = read(connfd, imagen_rgb_buffer + bytes_recibidos, bytes_restantes);
        
        if (resultado <= 0) {
            printf("[ERROR] Fallo recibiendo datos RGB de %s\n", cliente_ip);
            free(imagen_rgb_buffer);
            const char *error_msg = "ERROR_DATOS";
            write(connfd, error_msg, strlen(error_msg));
            return;
        }
        
        bytes_recibidos += resultado;
    }
    
    printf("[RECIBIDO] %d bytes RGB completos de %s\n", bytes_recibidos, cliente_ip);
    
    // Verificar datos RGB recibidos
    printf("[VERIFICACION] Primeros 3 pixels RGB recibidos:\n");
    for (int i = 0; i < 3 && i < (header.ancho * header.alto); i++) {
        int r = imagen_rgb_buffer[i*3 + 0];
        int g = imagen_rgb_buffer[i*3 + 1];
        int b = imagen_rgb_buffer[i*3 + 2];
        printf("               Pixel %d: R=%d, G=%d, B=%d\n", i+1, r, g, b);
    }
    
    // Convertir RGB a escala de grises para procesamiento
    int pixels_grises = header.ancho * header.alto;
    unsigned char *imagen_gris = malloc(pixels_grises);
    
    printf("[CONVERSION] Convirtiendo RGB a escala de grises (EN MEMORIA)...\n");
    for (int i = 0; i < pixels_grises; i++) {
        int r = imagen_rgb_buffer[i*3 + 0];
        int g = imagen_rgb_buffer[i*3 + 1];
        int b = imagen_rgb_buffer[i*3 + 2];
        
        // Fórmula estándar RGB → Gris
        imagen_gris[i] = (unsigned char)(0.299 * r + 0.587 * g + 0.114 * b);
    }
    printf("[CONVERSION] RGB convertido a escala de grises exitosamente (SIN archivos temporales)\n");
    
    // Crear header para datos en escala de grises
    ImageHeader header_gris = header;
    header_gris.size = pixels_grises;
    header_gris.canales = 1;
    
    // Almacenar en cola del cliente (RGB y escala de grises en memoria)
    char filename[64];
    snprintf(filename, sizeof(filename), "image_%ld.jpg", time(NULL));
    insertar_en_cola_cliente(connfd, header_gris, imagen_gris, imagen_rgb_buffer, cliente_ip, filename);
    
    free(imagen_rgb_buffer); // Se libera aquí porque ya se copió en insertar_en_cola_cliente
    free(imagen_gris);       // Se libera aquí porque ya se copió en insertar_en_cola_cliente
    
    // Confirmar recepción
    const char *confirmacion = "OK";
    write(connfd, confirmacion, strlen(confirmacion) + 1);
    printf("[CONFIRMADO] Imagen RGB procesada y almacenada en memoria para %s\n", cliente_ip);
}

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
        
        printf("[COMANDO] De %s: '%.20s%s'\n", 
               cliente_ip, buffer, bytes_leidos > 20 ? "..." : "");
        
        if (strncmp(buffer, "IMAGE", 5) == 0) {
            printf("[PROCESANDO] Comando IMAGE RGB de %s\n", cliente_ip);
            recibir_imagen_rgb(sockfd, cliente_ip);
            
        } else if (strncmp(buffer, "exit", 4) == 0 || strncmp(buffer, "EXIT", 4) == 0) {
            printf("[CLIENTE] %s envió EXIT - INICIANDO PROCESAMIENTO RGB\n", cliente_ip);
            iniciar_procesamiento_cliente(sockfd);
            break;
            
        } else {
            // Verificar datos binarios
            int datos_binarios = 0;
            for (int i = 0; i < bytes_leidos && i < 50; i++) {
                if (buffer[i] < 32 && buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] != '\t') {
                    datos_binarios = 1;
                    break;
                }
            }
            
            if (datos_binarios) {
                printf("[ERROR] Datos binarios recibidos de %s - DESCARTANDO\n", cliente_ip);
                continue;
            } else {
                printf("[ADVERTENCIA] Comando no reconocido de %s: '%.20s'\n", cliente_ip, buffer);
            }
        }
    }
    
    printf("[CLIENTE] %s terminó envío - manteniendo conexión para respuestas\n", cliente_ip);
    return NULL;
}

void mostrar_estadisticas() {
    printf("\n=== ESTADÍSTICAS SERVIDOR RGB LIMPIO ===\n");
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
    
    pthread_mutex_lock(&clientes_mutex);
    ClientSession *cliente = clientes_activos;
    while (cliente != NULL) {
        printf("Cliente %s: %d imágenes RGB, procesamiento %s\n", 
               cliente->cliente_ip, cliente->num_imagenes,
               cliente->procesamiento_iniciado ? "INICIADO" : "PENDIENTE");
        cliente = cliente->next;
    }
    pthread_mutex_unlock(&clientes_mutex);
    
    printf("Estado: ACTIVO (SIN archivos temporales)\n");
    printf("Guardado ÚNICAMENTE en: verde/, azul/, rojo/, img_ecualizadas/\n");
    printf("==========================================\n\n");
}

int main() {
    int sockfd, len;
    struct sockaddr_in servaddr, cli;
    
    printf("=== IMAGESERVER RGB LIMPIO - Solo directorios designados ===\n");
    printf("• Recibe imágenes RGB reales del cliente\n");
    printf("• Clasifica por color predominante RGB (EN MEMORIA)\n");
    printf("• Aplica histograma de ecualización\n");
    printf("• Guarda ÚNICAMENTE en:\n");
    printf("  - Directorios por color: verde/, azul/, rojo/\n");
    printf("  - Directorio general: img_ecualizadas/\n");
    printf("• SIN archivos temporales en directorio raíz\n");
    printf("Iniciando servidor en puerto %d...\n\n", PORT);
    
    crear_directorios_colores();
    
    pthread_t procesador_thread;
    pthread_create(&procesador_thread, NULL, procesador_imagenes, NULL);
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Error creando socket\n");
        exit(0);
    }
    
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
    
    printf("Servidor RGB LIMPIO escuchando...\n");
    printf("Flujo: Recibir RGB → Mantener en memoria → Clasificar → Convertir a gris → Histograma → Guardar SOLO en directorios designados\n");
    printf("Garantía: NO se crean archivos temporales en directorio raíz\n");
    printf("Guardado ÚNICAMENTE en:\n");
    printf("  • Por color: verde/, azul/, rojo/ (clasificación por RGB)\n");
    printf("  • General: img_ecualizadas/ (TODAS las imágenes procesadas)\n");
    printf("Presiona Ctrl+C para estadísticas\n\n");
    
    while (1) {
        len = sizeof(cli);
        int *connfd = malloc(sizeof(int));
        *connfd = accept(sockfd, (SA*)&cli, &len);
        
        if (*connfd < 0) {
            free(connfd);
            continue;
        }
        
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, manejar_cliente, connfd);
        pthread_detach(client_thread);
        
        if (clientes_conectados % 3 == 0) {
            mostrar_estadisticas();
        }
    }
    
    servidor_activo = 0;
    pthread_cond_broadcast(&cola_condition);
    pthread_join(procesador_thread, NULL);
    close(sockfd);
    
    return 0;
}