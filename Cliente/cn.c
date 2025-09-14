#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/select.h>
#define MAX 80
#define SA struct sockaddr

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
// NOTA: NO incluimos STB_IMAGE_WRITE porque no guardaremos imágenes localmente

// Estructura para información de imagen RGB
typedef struct {
    int ancho;
    int alto;
    int size;        // ancho * alto * 3 (RGB)
    int canales;     // Siempre 3 para RGB
} ImageHeader;

// Estructura para almacenar imágenes enviadas
typedef struct ImageRecord ImageRecord;
struct ImageRecord {
    char filename[256];
    ImageRecord *next;
};

// Variables globales para tracking
ImageRecord *imagenes_enviadas = NULL;
int total_imagenes_enviadas = 0;
int imagenes_recibidas = 0;

// Declaraciones de funciones
void enviar_imagen_rgb(int sockfd, const char *nombre_archivo);
void recibir_imagenes_procesadas(int sockfd);
void mostrar_imagenes_disponibles();
void mostrar_menu();
void mostrar_banner();
unsigned char* cargar_imagen_rgb_real(const char *nombre_imagen, int *ancho, int *alto);
void agregar_imagen_enviada(const char *filename);
void mostrar_progreso_recepcion();
void limpiar_buffer_socket(int sockfd);

void limpiar_buffer_socket(int sockfd) {
    printf("Limpiando buffer del socket...\n");
    char buffer_limpieza[1024];
    fd_set readfds;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 100ms timeout

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    if (select(sockfd + 1, &readfds, NULL, NULL, &timeout) > 0) {
        int bytes_residuales = read(sockfd, buffer_limpieza, sizeof(buffer_limpieza));
        if (bytes_residuales > 0) {
            printf("Datos residuales limpiados: %d bytes\n", bytes_residuales);
        }
    }
    printf("Buffer limpiado exitosamente.\n");
}

void agregar_imagen_enviada(const char *filename) {
    ImageRecord *nueva = malloc(sizeof(ImageRecord));
    strcpy(nueva->filename, filename);
    nueva->next = imagenes_enviadas;
    imagenes_enviadas = nueva;
    total_imagenes_enviadas++;
}

void mostrar_banner() {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                CLIENTE PROCESAMIENTO IMÁGENES RGB REAL           ║\n");
    printf("║                     Instituto Tecnológico CR                    ║\n");
    printf("║                    Versión 4.0 - SIN GUARDADO LOCAL             ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
}

void mostrar_imagenes_disponibles() {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    printf("┌───────────────────────────────────────────────────────────────────┐\n");
    printf("│                    IMÁGENES DISPONIBLES                         │\n");
    printf("├───────────────────────────────────────────────────────────────────┤\n");
    
    dir = opendir(".");
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext != NULL) {
                if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 || 
                    strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0) {
                    printf("│  %d. %-58s │\n", ++count, entry->d_name);
                }
            }
        }
        closedir(dir);
    }
    
    if (count == 0) {
        printf("│  No se encontraron imágenes (jpg, jpeg, png, gif)               │\n");
    }
    printf("└───────────────────────────────────────────────────────────────────┘\n");
}

void mostrar_menu() {
    printf("\n┌───────────────────────────────────────────────────────────────────┐\n");
    printf("│                         OPCIONES                                │\n");
    printf("├───────────────────────────────────────────────────────────────────┤\n");
    printf("│  enviar <archivo>  - Enviar imagen RGB REAL (preserva colores)   │\n");
    printf("│  listar           - Mostrar imágenes disponibles                │\n");
    printf("│  estado           - Ver estado de la sesión                     │\n");
    printf("│  exit             - Finalizar envío e INICIAR procesamiento     │\n");
    printf("│  help             - Mostrar ayuda                               │\n");
    printf("└───────────────────────────────────────────────────────────────────┘\n");
}

// FUNCIÓN: Cargar imagen RGB real
unsigned char* cargar_imagen_rgb_real(const char *nombre_imagen, int *ancho, int *alto) {
    int canales_detectados;
    
    printf("[CARGA] Cargando imagen: %s\n", nombre_imagen);
    
    // CRUCIAL: Cargar imagen FORZANDO 3 canales RGB
    unsigned char *pixel_array = stbi_load(nombre_imagen, ancho, alto, &canales_detectados, 3);
    
    if (!pixel_array) {
        printf("[ERROR] No se pudo cargar la imagen: %s\n", nombre_imagen);
        return NULL;
    }
    
    printf("[CARGA] Imagen cargada exitosamente:\n");
    printf("        • Archivo: %s\n", nombre_imagen);
    printf("        • Dimensiones: %dx%d\n", *ancho, *alto);
    printf("        • Canales detectados: %d\n", canales_detectados);
    printf("        • Canales forzados: 3 (RGB)\n");
    printf("        • Tamaño total: %d bytes RGB\n", (*ancho) * (*alto) * 3);
    
    // Verificar que tenemos datos RGB válidos
    int total_pixels = (*ancho) * (*alto);
    printf("[VERIFICACION] Primeros 3 pixels RGB:\n");
    for (int i = 0; i < 3 && i < total_pixels; i++) {
        int r = pixel_array[i*3 + 0];
        int g = pixel_array[i*3 + 1];
        int b = pixel_array[i*3 + 2];
        printf("        Pixel %d: R=%d, G=%d, B=%d\n", i+1, r, g, b);
    }
    
    return pixel_array;
}

// FUNCIÓN: Enviar imagen RGB real al servidor
void enviar_imagen_rgb(int sockfd, const char *nombre_archivo) {
    int ancho, alto;
    unsigned char *pixels_rgb = cargar_imagen_rgb_real(nombre_archivo, &ancho, &alto);
    
    if (!pixels_rgb) {
        printf("[ERROR] No se pudo cargar la imagen RGB: %s\n", nombre_archivo);
        return;
    }
    
    // Header RGB
    ImageHeader header;
    header.ancho = ancho;
    header.alto = alto;
    header.canales = 3;  // Siempre RGB
    header.size = ancho * alto * 3;  // Tamaño RGB completo
    
    printf("\n[ENVIO] Preparando envío de imagen RGB:\n");
    printf("        • Archivo: %s\n", nombre_archivo);
    printf("        • Header: %dx%d, %d canales\n", header.ancho, header.alto, header.canales);
    printf("        • Tamaño: %d bytes RGB\n", header.size);
    
    // PASO 1: Enviar comando IMAGE
    char comando[] = "IMAGE\n";
    if (write(sockfd, comando, strlen(comando)) < 0) {
        printf("[ERROR] Fallo enviando comando IMAGE\n");
        stbi_image_free(pixels_rgb);
        return;
    }
    printf("[ENVIO] Comando IMAGE enviado\n");
    
    // PASO 2: Pausa para sincronización
    usleep(100000); // 100ms
    
    // PASO 3: Enviar header RGB
    if (write(sockfd, &header, sizeof(ImageHeader)) < 0) {
        printf("[ERROR] Fallo enviando header RGB\n");
        stbi_image_free(pixels_rgb);
        return;
    }
    printf("[ENVIO] Header RGB enviado\n");
    
    // PASO 4: Enviar datos RGB en chunks
    int bytes_enviados = 0;
    printf("[ENVIO] Enviando datos RGB: ");
    
    while (bytes_enviados < header.size) {
        int bytes_restantes = header.size - bytes_enviados;
        int chunk_size = (bytes_restantes > 4096) ? 4096 : bytes_restantes;
        
        int resultado = write(sockfd, pixels_rgb + bytes_enviados, chunk_size);
        if (resultado < 0) {
            printf("\n[ERROR] Fallo enviando datos RGB\n");
            stbi_image_free(pixels_rgb);
            return;
        }
        
        bytes_enviados += resultado;
        int porcentaje = (bytes_enviados * 100) / header.size;
        printf("\r[ENVIO] Enviando datos RGB: [");
        int barras = porcentaje / 5;
        for (int i = 0; i < 20; i++) {
            printf(i < barras ? "=" : " ");
        }
        printf("] %d%%", porcentaje);
        fflush(stdout);
    }
    printf("\n");
    
    printf("[ENVIO] %d bytes RGB enviados completamente\n", bytes_enviados);
    
    // PASO 5: Esperar confirmación
    char confirmacion[32];
    memset(confirmacion, 0, sizeof(confirmacion));
    int bytes_leidos = read(sockfd, confirmacion, sizeof(confirmacion) - 1);
    
    if (bytes_leidos > 0) {
        confirmacion[bytes_leidos] = '\0';
        printf("[RESPUESTA] Servidor: '%s'\n", confirmacion);
        
        if (strncmp(confirmacion, "OK", 2) == 0) {
            printf("✓ Imagen RGB %s enviada exitosamente!\n", nombre_archivo);
            printf("  • Colores RGB reales preservados para clasificación\n");
            agregar_imagen_enviada(nombre_archivo);
        } else if (strncmp(confirmacion, "ERROR", 5) == 0) {
            printf("✗ El servidor rechazó la imagen: %s\n", confirmacion);
        } else {
            printf("? Confirmación no reconocida: %s\n", confirmacion);
        }
    } else {
        printf("✗ No se recibió confirmación del servidor\n");
    }
    
    stbi_image_free(pixels_rgb);
}

void mostrar_progreso_recepcion() {
    printf("Estado de la sesión RGB:\n");
    printf("   • Imágenes RGB enviadas: %d\n", total_imagenes_enviadas);
    printf("   • Imágenes procesadas recibidas: %d\n", imagenes_recibidas);
    
    if (total_imagenes_enviadas > 0) {
        printf("   • Lista de imágenes RGB enviadas:\n");
        ImageRecord *img = imagenes_enviadas;
        int contador = 1;
        while (img != NULL) {
            printf("     %d. %s (RGB real preservado)\n", contador++, img->filename);
            img = img->next;
        }
    }
}

// FUNCIÓN CORREGIDA: Recibir imágenes procesadas SIN guardarlas localmente
void recibir_imagenes_procesadas(int sockfd) {
    printf("\n[RECEPCION] FASE DE RECEPCIÓN INICIADA\n");
    printf("Esperando %d imágenes procesadas del servidor...\n", total_imagenes_enviadas);
    printf("NOTA: Las imágenes se procesan en el servidor únicamente\n");
    printf("      NO se guardan copias locales en el cliente\n\n");
    
    for (int i = 0; i < total_imagenes_enviadas; i++) {
        ImageHeader header;
        
        printf("[RECEPCION] Esperando imagen %d/%d...\n", i+1, total_imagenes_enviadas);
        
        // Recibir header
        int header_bytes = read(sockfd, &header, sizeof(ImageHeader));
        if (header_bytes <= 0) {
            printf("[ERROR] Error recibiendo header de imagen %d\n", i+1);
            break;
        }
        
        // Validar header (imagen procesada = escala de grises)
        int expected_size = header.ancho * header.alto; // 1 canal (gris)
        if (header.ancho <= 0 || header.alto <= 0 || header.size <= 0 || 
            header.size > 10000000 || header.size != expected_size) {
            printf("[ERROR] Header inválido recibido en imagen %d\n", i+1);
            printf("        Dimensiones: %dx%d, Tamaño: %d (esperado: %d)\n", 
                   header.ancho, header.alto, header.size, expected_size);
            break;
        }
        
        printf("[RECEPCION] Imagen procesada %d: %dx%d (%d bytes, escala de grises)\n", 
               i+1, header.ancho, header.alto, header.size);
        
        // Alocar memoria temporal
        unsigned char *imagen_procesada = malloc(header.size);
        if (!imagen_procesada) {
            printf("[ERROR] Memoria insuficiente para imagen %d\n", i+1);
            break;
        }
        
        // Recibir datos
        int bytes_recibidos = 0;
        while (bytes_recibidos < header.size) {
            int bytes_restantes = header.size - bytes_recibidos;
            int resultado = read(sockfd, imagen_procesada + bytes_recibidos, bytes_restantes);
            
            if (resultado <= 0) {
                printf("[ERROR] Error recibiendo datos de imagen %d\n", i+1);
                free(imagen_procesada);
                return;
            }
            
            bytes_recibidos += resultado;
            int porcentaje = (bytes_recibidos * 100) / header.size;
            printf("\r[RECEPCION] Descarga %d/%d: [", i+1, total_imagenes_enviadas);
            int barras = porcentaje / 5;
            for (int j = 0; j < 20; j++) {
                printf(j < barras ? "=" : " ");
            }
            printf("] %d%%", porcentaje);
            fflush(stdout);
        }
        
        printf("\n[PROCESADA] Imagen %d recibida y verificada correctamente\n", i+1);
        printf("            • Tamaño: %d bytes\n", bytes_recibidos);
        printf("            • Dimensiones: %dx%d\n", header.ancho, header.alto);
        printf("            • Estado: Procesada en servidor con histograma\n");
        printf("            • Guardada en servidor en directorios apropiados\n");
        
        // IMPORTANTE: Solo liberar memoria, NO guardar archivo
        free(imagen_procesada);
        imagenes_recibidas++;
        printf("\n");
    }
    
    printf("\n[COMPLETADO] PROCESAMIENTO RGB TERMINADO!\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("   • Total imágenes RGB enviadas: %d\n", total_imagenes_enviadas);
    printf("   • Total imágenes procesadas en servidor: %d\n", imagenes_recibidas);
    printf("   • Clasificación por color RGB REAL aplicada en servidor\n");
    printf("   • Histograma de ecualización aplicado en servidor\n");
    printf("   • Imágenes guardadas en servidor en:\n");
    printf("     - Directorios por color: verde/, azul/, rojo/\n");
    printf("     - Directorio general: img_ecualizadas/\n");
    printf("   • NO se crean archivos locales en el cliente\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    printf("\nTerminando cliente automáticamente...\n");
    close(sockfd);
    exit(0);
}

void cliente_interactivo(int sockfd) {
    char comando[256];
    char archivo[200];
    
    mostrar_banner();
    printf("MODO RGB REAL ACTIVADO\n");
    printf("   1. Las imágenes se cargan preservando información RGB completa\n");
    printf("   2. El servidor recibirá datos RGB reales para clasificación\n");
    printf("   3. Escriba 'exit' para iniciar procesamiento con histograma\n");
    printf("   4. Las imágenes se guardan ÚNICAMENTE en el servidor\n\n");
    
    mostrar_imagenes_disponibles();
    mostrar_menu();
    
    printf("\nConectado al servidor de procesamiento RGB\n");
    printf("Las imágenes preservan colores reales para clasificación correcta\n");
    
    while (1) {
        printf("\n> Ingrese comando: ");
        fflush(stdout);
        
        if (!fgets(comando, sizeof(comando), stdin)) {
            break;
        }
        
        comando[strcspn(comando, "\n")] = '\0';
        
        if (strncmp(comando, "enviar ", 7) == 0) {
            sscanf(comando, "enviar %199s", archivo);
            printf("\n[PROCESANDO] Archivo: %s\n", archivo);
            printf("Cargando imagen con datos RGB reales...\n");
            enviar_imagen_rgb(sockfd, archivo);
            
        } else if (strcmp(comando, "listar") == 0) {
            printf("\n");
            mostrar_imagenes_disponibles();
            
        } else if (strcmp(comando, "estado") == 0) {
            printf("\n");
            mostrar_progreso_recepcion();
            
        } else if (strcmp(comando, "help") == 0) {
            printf("\n┌───────────────────────────────────────────────────────────────────┐\n");
            printf("│                       AYUDA RGB REAL                            │\n");
            printf("├───────────────────────────────────────────────────────────────────┤\n");
            printf("│  • enviar <archivo> - Envía imagen preservando RGB real         │\n");
            printf("│  • Los colores originales se mantienen para clasificación       │\n");
            printf("│  • El servidor aplicará histograma a la imagen RGB              │\n");
            printf("│  • Clasificación de color funcionará correctamente              │\n");
            printf("│  • Formato: JPG, JPEG, PNG, GIF                                 │\n");
            printf("│  • 'exit' inicia procesamiento por prioridad de tamaño          │\n");
            printf("│  • NO se guardan archivos locales (solo en servidor)            │\n");
            printf("└───────────────────────────────────────────────────────────────────┘\n");
            
        } else if (strcmp(comando, "exit") == 0 || strcmp(comando, "EXIT") == 0) {
            if (total_imagenes_enviadas == 0) {
                printf("\nNo ha enviado imágenes RGB. Envíe al menos una imagen.\n");
                continue;
            }
            
            printf("\n[FINALIZANDO] INICIANDO PROCESAMIENTO RGB...\n");
            printf("Total imágenes RGB enviadas: %d\n", total_imagenes_enviadas);
            printf("Enviando señal EXIT al servidor...\n");
            
            write(sockfd, "exit", 5);
            
            printf("El servidor procesará las imágenes RGB por prioridad de tamaño\n");
            printf("Aplicando clasificación RGB + histograma de ecualización...\n");
            printf("Las imágenes se guardarán ÚNICAMENTE en el servidor\n");
            
            recibir_imagenes_procesadas(sockfd);
            break;
            
        } else if (strlen(comando) == 0) {
            continue;
            
        } else {
            printf("Comando no reconocido: '%s'\n", comando);
            printf("Use 'help' para ver comandos disponibles\n");
        }
    }
}

int main() {
    int sockfd, port;
    char ip_address[32];
    struct sockaddr_in servaddr;
    
    printf("CONFIGURACIÓN DE CONEXIÓN RGB REAL\n");
    printf("═══════════════════════════════════════\n");
    
    printf("IP del servidor: ");
    if (!fgets(ip_address, sizeof(ip_address), stdin)) {
        printf("Error leyendo IP\n");
        exit(1);
    }
    ip_address[strcspn(ip_address, "\n")] = '\0';
    
    printf("Puerto del servidor: ");
    if (scanf("%d", &port) != 1) {
        printf("Error leyendo puerto\n");
        exit(1);
    }
    while (getchar() != '\n');
    
    if (port < 1 || port > 65535) {
        printf("Puerto inválido\n");
        exit(1);
    }
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Error creando socket\n");
        exit(1);
    }
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip_address);
    servaddr.sin_port = htons(port);
    
    if (servaddr.sin_addr.s_addr == INADDR_NONE) {
        printf("Dirección IP inválida\n");
        exit(1);
    }
    
    printf("\nConectando a %s:%d...\n", ip_address, port);
    
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("Error conectando al servidor\n");
        exit(1);
    }
    
    printf("Conexión RGB establecida exitosamente.\n");
    limpiar_buffer_socket(sockfd);
    printf("Listo para enviar imágenes RGB reales.\n");
    printf("Las imágenes se procesarán y guardarán ÚNICAMENTE en el servidor.\n\n");
    
    cliente_interactivo(sockfd);
    
    close(sockfd);
    return 0;
}