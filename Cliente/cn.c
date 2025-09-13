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
#define MAX 80
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
void enviar_imagen_secuencial(int sockfd, const char *nombre_archivo);
void recibir_imagenes_procesadas(int sockfd);
void mostrar_imagenes_disponibles();
void mostrar_menu();
void mostrar_banner();
unsigned char* cargar_imagen(const char *nombre_imagen, int *ancho, int *alto);
void agregar_imagen_enviada(const char *filename);
void mostrar_progreso_recepcion();

// Función para agregar imagen a la lista de enviadas
void agregar_imagen_enviada(const char *filename) {
    ImageRecord *nueva = malloc(sizeof(ImageRecord));
    strcpy(nueva->filename, filename);
    nueva->next = imagenes_enviadas;
    imagenes_enviadas = nueva;
    total_imagenes_enviadas++;
}

// Función para mostrar banner inicial
void mostrar_banner() {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                  CLIENTE PROCESAMIENTO IMÁGENES SECUENCIAL       ║\n");
    printf("║                     Instituto Tecnológico CR                    ║\n");
    printf("║                         Versión 2.0                             ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
}

// Función para mostrar imágenes disponibles
void mostrar_imagenes_disponibles() {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    printf("┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│                    IMÁGENES DISPONIBLES                         │\n");
    printf("├─────────────────────────────────────────────────────────────────┤\n");
    
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
    printf("└─────────────────────────────────────────────────────────────────┘\n");
}

// Función para mostrar menú
void mostrar_menu() {
    printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│                         OPCIONES                                │\n");
    printf("├─────────────────────────────────────────────────────────────────┤\n");
    printf("│  enviar <archivo>  - Enviar imagen (se almacena en servidor)     │\n");
    printf("│  listar           - Mostrar imágenes disponibles                │\n");
    printf("│  estado           - Ver estado de la sesión                     │\n");
    printf("│  exit             - Finalizar envío e INICIAR procesamiento     │\n");
    printf("│  help             - Mostrar ayuda                               │\n");
    printf("└─────────────────────────────────────────────────────────────────┘\n");
}

// Función para cargar imagen
unsigned char* cargar_imagen(const char *nombre_imagen, int *ancho, int *alto) {
    int canales = 1;
    
    unsigned char *pixel_array = stbi_load(nombre_imagen, ancho, alto, &canales, 1);
    if (!pixel_array) {
        printf("Error al cargar imagen: %s\n", nombre_imagen);
        return NULL;
    }
    
    printf("Imagen cargada: %s (%dx%d pixels, %d bytes)\n", 
           nombre_imagen, *ancho, *alto, (*ancho) * (*alto));
    return pixel_array;
}

// Función para enviar imagen (solo enviar, no esperar respuesta procesada)
void enviar_imagen_secuencial(int sockfd, const char *nombre_archivo) {
    int ancho, alto;
    unsigned char *pixels = cargar_imagen(nombre_archivo, &ancho, &alto);
    
    if (!pixels) {
        printf("No se pudo cargar la imagen: %s\n", nombre_archivo);
        return;
    }
    
    ImageHeader header;
    header.ancho = ancho;
    header.alto = alto;
    header.size = ancho * alto;
    
    // Enviar comando IMAGE
    if (write(sockfd, "IMAGE", 6) < 0) {
        printf("Error enviando comando IMAGE\n");
        stbi_image_free(pixels);
        return;
    }
    
    printf("Enviando imagen al servidor (MODO SECUENCIAL)...\n");
    
    // Enviar header
    if (write(sockfd, &header, sizeof(ImageHeader)) < 0) {
        printf("Error enviando información de imagen\n");
        stbi_image_free(pixels);
        return;
    }
    
    // Enviar datos en chunks
    int bytes_enviados = 0;
    printf("Progreso: ");
    while (bytes_enviados < header.size) {
        int bytes_restantes = header.size - bytes_enviados;
        int chunk_size = (bytes_restantes > 4096) ? 4096 : bytes_restantes;
        
        int resultado = write(sockfd, pixels + bytes_enviados, chunk_size);
        if (resultado < 0) {
            printf("\nError enviando datos de imagen\n");
            stbi_image_free(pixels);
            return;
        }
        
        bytes_enviados += resultado;
        int porcentaje = (bytes_enviados * 100) / header.size;
        printf("\rProgreso: [");
        int barras = porcentaje / 5;
        for (int i = 0; i < 20; i++) {
            printf(i < barras ? "=" : " ");
        }
        printf("] %d%%", porcentaje);
        fflush(stdout);
    }
    printf("\n");
    
    // Esperar confirmación del servidor
    char confirmacion[16];
    memset(confirmacion, 0, sizeof(confirmacion));
    int bytes_leidos = read(sockfd, confirmacion, sizeof(confirmacion) - 1);
    if (bytes_leidos > 0) {
        confirmacion[bytes_leidos] = '\0';
        if (strncmp(confirmacion, "OK", 2) == 0) {
            printf("Imagen %s enviada y almacenada en servidor!\n", nombre_archivo);
            agregar_imagen_enviada(nombre_archivo);
        } else if (strncmp(confirmacion, "ERROR", 5) == 0) {
            printf("El servidor rechazó la imagen: %s\n", confirmacion);
        } else {
            printf("Confirmación no válida del servidor: %s\n", confirmacion);
        }
    } else {
        printf("No se recibió confirmación del servidor\n");
    }
    
    stbi_image_free(pixels);
}

// Función para mostrar estado de la sesión
void mostrar_progreso_recepcion() {
    printf("Estado de la sesión:\n");
    printf("   • Imágenes enviadas: %d\n", total_imagenes_enviadas);
    printf("   • Imágenes procesadas recibidas: %d\n", imagenes_recibidas);
    
    if (total_imagenes_enviadas > 0) {
        printf("   • Lista de imágenes enviadas:\n");
        ImageRecord *img = imagenes_enviadas;
        int contador = 1;
        while (img != NULL) {
            printf("     %d. %s\n", contador++, img->filename);
            img = img->next;
        }
    }
}

// Función para recibir todas las imágenes procesadas después de EXIT
void recibir_imagenes_procesadas(int sockfd) {
    printf("\nFASE DE RECEPCIÓN INICIADA\n");
    printf("Esperando %d imágenes procesadas del servidor...\n", total_imagenes_enviadas);
    printf("Las imágenes se reciben en orden de prioridad (tamaño menor primero)\n\n");
    
    for (int i = 0; i < total_imagenes_enviadas; i++) {
        ImageHeader header;
        
        printf("Esperando imagen %d/%d...\n", i+1, total_imagenes_enviadas);
        
        // Recibir header
        int header_bytes = read(sockfd, &header, sizeof(ImageHeader));
        if (header_bytes <= 0) {
            printf("Error recibiendo información de imagen procesada %d\n", i+1);
            break;
        }
        
        // Validar que el header tenga valores válidos
        if (header.ancho <= 0 || header.alto <= 0 || header.size <= 0 || header.size > 10000000) {
            printf("Header inválido recibido. Posible corrupción de datos.\n");
            printf("   Dimensiones: %dx%d, Tamaño: %d bytes\n", header.ancho, header.alto, header.size);
            break;
        }
        
        printf("Recibiendo imagen procesada %d: %dx%d (%d bytes)\n", 
               i+1, header.ancho, header.alto, header.size);
        
        // Alocar memoria
        unsigned char *imagen_procesada = malloc(header.size);
        if (!imagen_procesada) {
            printf("Error: No se pudo alocar memoria para imagen %d\n", i+1);
            break;
        }
        
        // Recibir datos
        int bytes_recibidos = 0;
        printf("Descarga %d/%d: ", i+1, total_imagenes_enviadas);
        while (bytes_recibidos < header.size) {
            int bytes_restantes = header.size - bytes_recibidos;
            int resultado = read(sockfd, imagen_procesada + bytes_recibidos, bytes_restantes);
            
            if (resultado <= 0) {
                printf("\nError recibiendo imagen procesada %d\n", i+1);
                free(imagen_procesada);
                return;
            }
            
            bytes_recibidos += resultado;
            int porcentaje = (bytes_recibidos * 100) / header.size;
            printf("\rDescarga %d/%d: [", i+1, total_imagenes_enviadas);
            int barras = porcentaje / 5;
            for (int j = 0; j < 20; j++) {
                printf(j < barras ? "=" : " ");
            }
            printf("] %d%%", porcentaje);
            fflush(stdout);
        }
        
        // Guardar imagen procesada
        char nombre_salida[256];
        snprintf(nombre_salida, sizeof(nombre_salida), "processed_image_%d_%ld.jpg", i+1, (long)time(NULL));
        
        if (stbi_write_jpg(nombre_salida, header.ancho, header.alto, 1, imagen_procesada, 90)) {
            printf("\nImagen %d procesada guardada como: %s\n", i+1, nombre_salida);
            imagenes_recibidas++;
        } else {
            printf("\nError guardando imagen procesada %d\n", i+1);
        }
        
        free(imagen_procesada);
        printf("\n");
    }
    
    // AQUÍ ESTÁ EL CAMBIO IMPORTANTE:
    printf("PROCESAMIENTO COMPLETADO!\n");
    printf("   • Total imágenes enviadas: %d\n", total_imagenes_enviadas);
    printf("   • Total imágenes procesadas recibidas: %d\n", imagenes_recibidas);
    printf("   • Histograma de ecualización aplicado a todas las imágenes!\n");
    
    printf("Terminando cliente automáticamente...\n");
    printf("Sesión completada. Cliente desconectado del servidor\n");
    printf("Cliente terminado.\n");
    
    close(sockfd);
    exit(0); // ESTA LÍNEA TERMINA EL PROGRAMA COMPLETAMENTE
}

// Función principal del cliente
void cliente_interactivo(int sockfd) {
    char comando[256];
    char archivo[200];
    
    mostrar_banner();
    printf("MODO SECUENCIAL ACTIVADO\n");
    printf("   1. Envíe imágenes una por una (se almacenan en el servidor)\n");
    printf("   2. Escriba 'exit' para iniciar el procesamiento\n");
    printf("   3. Reciba todas las imágenes procesadas por orden de prioridad\n\n");
    
    mostrar_imagenes_disponibles();
    mostrar_menu();
    
    printf("\nConectado al servidor de procesamiento de imágenes\n");
    printf("Las imágenes se procesan por tamaño después de enviar EXIT\n");
    
    while (1) {
        printf("\n> Ingrese comando: ");
        fflush(stdout);
        
        if (!fgets(comando, sizeof(comando), stdin)) {
            break;
        }
        
        // Remover salto de línea
        comando[strcspn(comando, "\n")] = '\0';
        
        // Parsear comando
        if (strncmp(comando, "enviar ", 7) == 0) {
            sscanf(comando, "enviar %199s", archivo);
            printf("\nProcesando: %s\n", archivo);
            printf("La imagen se almacena en el servidor para procesamiento posterior\n");
            enviar_imagen_secuencial(sockfd, archivo);
            
        } else if (strcmp(comando, "listar") == 0) {
            printf("\n");
            mostrar_imagenes_disponibles();
            
        } else if (strcmp(comando, "estado") == 0) {
            printf("\n");
            mostrar_progreso_recepcion();
            
        } else if (strcmp(comando, "help") == 0) {
            printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
            printf("│                            AYUDA                                │\n");
            printf("├─────────────────────────────────────────────────────────────────┤\n");
            printf("│  • Para enviar una imagen: enviar nombre_archivo.jpg            │\n");
            printf("│  • Formatos soportados: JPG, JPEG, PNG, GIF                     │\n");
            printf("│  • MODO SECUENCIAL: Las imágenes se almacenan en el servidor    │\n");
            printf("│  • Al escribir 'exit' se inicia el procesamiento por prioridad  │\n");
            printf("│  • Las más pequeñas se procesan primero                         │\n");
            printf("│  • Recibirá todas las imágenes procesadas al final              │\n");
            printf("│  • Use 'estado' para ver cuántas imágenes ha enviado            │\n");
            printf("└─────────────────────────────────────────────────────────────────┘\n");
            
        } else if (strcmp(comando, "exit") == 0 || strcmp(comando, "EXIT") == 0) {
            if (total_imagenes_enviadas == 0) {
                printf("\nNo ha enviado ninguna imagen. Envíe al menos una antes de usar 'exit'\n");
                continue;
            }
            
            printf("\nFINALIZANDO ENVÍO E INICIANDO PROCESAMIENTO...\n");
            printf("Total de imágenes enviadas: %d\n", total_imagenes_enviadas);
            printf("Enviando señal EXIT al servidor...\n");
            
            // Enviar comando exit al servidor
            write(sockfd, "exit", 5);
            
            printf("Señal enviada. El servidor procesará las imágenes por prioridad.\n");
            printf("Esperando imágenes procesadas...\n");
            
            // Recibir todas las imágenes procesadas
            recibir_imagenes_procesadas(sockfd);
            
            // Esta línea nunca se ejecutará porque exit(0) termina el programa
            break;
            
        } else if (strlen(comando) == 0) {
            // Comando vacío, no hacer nada
            continue;
            
        } else {
            printf("Comando no reconocido: '%s'\n", comando);
            printf("Use 'help' para ver comandos disponibles\n");
        }
    }
    
    printf("Sesión completada. Cliente desconectado del servidor\n");
}

int main() {
    int sockfd, port;
    char ip_address[32];
    struct sockaddr_in servaddr;
    
    // Solicitar datos de conexión
    printf("CONFIGURACIÓN DE CONEXIÓN\n");
    printf("═══════════════════════════════\n");
    
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
    while (getchar() != '\n'); // Limpiar buffer
    
    // Validar puerto
    if (port < 1 || port > 65535) {
        printf("Puerto inválido (debe estar entre 1 y 65535)\n");
        exit(1);
    }
    
    // Crear socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Error creando socket\n");
        exit(1);
    }
    
    // Configurar dirección del servidor
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip_address);
    servaddr.sin_port = htons(port);
    
    if (servaddr.sin_addr.s_addr == INADDR_NONE) {
        printf("Dirección IP inválida\n");
        exit(1);
    }
    
    printf("\nConectando a %s:%d...\n", ip_address, port);
    
    // Conectar al servidor
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("Error conectando al servidor\n");
        printf("Verifique que el servidor esté ejecutándose\n");
        exit(1);
    }
    
    // Iniciar sesión interactiva
    cliente_interactivo(sockfd);
    
    // Esta línea nunca se ejecutará si el procesamiento se completa
    // porque exit(0) termina el programa en recibir_imagenes_procesadas()
    close(sockfd);
    return 0;
}

/*
COMPILACIÓN:
gcc cn.c -o cliente -lm -lpthread

FUNCIONAMIENTO SECUENCIAL CON TERMINACIÓN AUTOMÁTICA:
1. Cliente se conecta al servidor
2. Usuario envía imágenes una por una con "enviar <archivo>"
3. Cada imagen se almacena en el servidor (no se procesa aún)
4. Usuario escribe "exit" cuando termina de enviar
5. Servidor procesa todas las imágenes por orden de prioridad (tamaño)
6. Cliente recibe todas las imágenes procesadas secuencialmente
7. Se guardan como "processed_image_X_timestamp.jpg"
8. AUTOMÁTICAMENTE termina el programa con exit(0)

CAMBIO CLAVE:
- Al final de recibir_imagenes_procesadas() se usa exit(0)
- Esto termina completamente el programa sin esperar más entrada
- No necesita Ctrl+C manual
*/

/*
error en el primer comando de cliente
*/