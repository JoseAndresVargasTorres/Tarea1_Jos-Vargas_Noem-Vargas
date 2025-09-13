#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
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

// Declaraciones de funciones
void enviar_imagen(int sockfd, const char *nombre_archivo);
void recibir_imagen_procesada(int sockfd, const char *nombre_original);
void mostrar_imagenes_disponibles();
void mostrar_menu();
void mostrar_banner();
unsigned char* cargar_imagen(const char *nombre_imagen, int *ancho, int *alto);

// Función para mostrar banner inicial
void mostrar_banner() {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                    CLIENTE PROCESAMIENTO IMÁGENES                ║\n");
    printf("║                     Instituto Tecnológico CR                    ║\n");
    printf("║                         Versión 1.0                             ║\n");
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
    printf("│  enviar <archivo>  - Enviar imagen para procesamiento           │\n");
    printf("│  listar           - Mostrar imágenes disponibles                │\n");
    printf("│  help             - Mostrar ayuda                               │\n");
    printf("│  exit             - Salir del cliente                           │\n");
    printf("└─────────────────────────────────────────────────────────────────┘\n");
}

// Función para cargar imagen
unsigned char* cargar_imagen(const char *nombre_imagen, int *ancho, int *alto) {
    int canales = 1;
    
    unsigned char *pixel_array = stbi_load(nombre_imagen, ancho, alto, &canales, 1);
    if (!pixel_array) {
        printf("❌ Error al cargar imagen: %s\n", nombre_imagen);
        return NULL;
    }
    
    printf("✓ Imagen cargada: %s (%dx%d pixels, %d bytes)\n", 
           nombre_imagen, *ancho, *alto, (*ancho) * (*alto));
    return pixel_array;
}

// Función para enviar imagen
void enviar_imagen(int sockfd, const char *nombre_archivo) {
    int ancho, alto;
    unsigned char *pixels = cargar_imagen(nombre_archivo, &ancho, &alto);
    
    if (!pixels) {
        printf("❌ No se pudo cargar la imagen: %s\n", nombre_archivo);
        return;
    }
    
    ImageHeader header;
    header.ancho = ancho;
    header.alto = alto;
    header.size = ancho * alto;
    
    // Enviar comando IMAGE
    if (write(sockfd, "IMAGE", 6) < 0) {
        printf("❌ Error enviando comando IMAGE\n");
        stbi_image_free(pixels);
        return;
    }
    
    printf("📤 Enviando imagen al servidor...\n");
    
    // Enviar header
    if (write(sockfd, &header, sizeof(ImageHeader)) < 0) {
        printf("❌ Error enviando información de imagen\n");
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
            printf("\n❌ Error enviando datos de imagen\n");
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
    printf("\n✓ Imagen enviada completamente!\n");
    
    // Recibir imagen procesada
    recibir_imagen_procesada(sockfd, nombre_archivo);
    
    stbi_image_free(pixels);
}

// Función para recibir imagen procesada
void recibir_imagen_procesada(int sockfd, const char *nombre_original) {
    ImageHeader header;
    
    printf("📥 Esperando imagen procesada del servidor...\n");
    
    // Recibir header
    if (read(sockfd, &header, sizeof(ImageHeader)) <= 0) {
        printf("❌ Error recibiendo información de imagen procesada\n");
        return;
    }
    
    printf("📥 Recibiendo imagen procesada: %dx%d (%d bytes)\n", 
           header.ancho, header.alto, header.size);
    
    // Alocar memoria
    unsigned char *imagen_procesada = malloc(header.size);
    if (!imagen_procesada) {
        printf("❌ Error: No se pudo alocar memoria\n");
        return;
    }
    
    // Recibir datos
    int bytes_recibidos = 0;
    printf("Descarga: ");
    while (bytes_recibidos < header.size) {
        int bytes_restantes = header.size - bytes_recibidos;
        int resultado = read(sockfd, imagen_procesada + bytes_recibidos, bytes_restantes);
        
        if (resultado <= 0) {
            printf("\n❌ Error recibiendo imagen procesada\n");
            free(imagen_procesada);
            return;
        }
        
        bytes_recibidos += resultado;
        int porcentaje = (bytes_recibidos * 100) / header.size;
        printf("\rDescarga: [");
        int barras = porcentaje / 5;
        for (int i = 0; i < 20; i++) {
            printf(i < barras ? "=" : " ");
        }
        printf("] %d%%", porcentaje);
        fflush(stdout);
    }
    
    // Guardar imagen procesada
    char nombre_salida[256];
    char *punto = strrchr(nombre_original, '.');
    if (punto) {
        int pos = punto - nombre_original;
        strncpy(nombre_salida, nombre_original, pos);
        nombre_salida[pos] = '\0';
        strcat(nombre_salida, "_processed.jpg");
    } else {
        strcpy(nombre_salida, "processed_image.jpg");
    }
    
    if (stbi_write_jpg(nombre_salida, header.ancho, header.alto, 1, imagen_procesada, 90)) {
        printf("\n✓ Imagen procesada guardada como: %s\n", nombre_salida);
        printf("🎨 ¡Histograma de ecualización aplicado exitosamente!\n");
    } else {
        printf("\n❌ Error guardando imagen procesada\n");
    }
    
    free(imagen_procesada);
}

// Función principal del cliente
void cliente_interactivo(int sockfd) {
    char comando[256];
    char archivo[200];
    
    mostrar_banner();
    mostrar_imagenes_disponibles();
    mostrar_menu();
    
    printf("\n🔗 Conectado al servidor de procesamiento de imágenes\n");
    printf("✨ Las imágenes se procesan por tamaño (pequeñas primero)\n");
    
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
            printf("\n📋 Procesando: %s\n", archivo);
            printf("⏳ Su imagen se procesará según su tamaño en la cola de prioridad\n");
            enviar_imagen(sockfd, archivo);
            
        } else if (strcmp(comando, "listar") == 0) {
            printf("\n");
            mostrar_imagenes_disponibles();
            
        } else if (strcmp(comando, "help") == 0) {
            printf("\n┌─────────────────────────────────────────────────────────────────┐\n");
            printf("│                            AYUDA                                │\n");
            printf("├─────────────────────────────────────────────────────────────────┤\n");
            printf("│  • Para enviar una imagen: enviar nombre_archivo.jpg            │\n");
            printf("│  • Formatos soportados: JPG, JPEG, PNG, GIF                     │\n");
            printf("│  • Las imágenes se procesan por orden de tamaño                 │\n");
            printf("│  • Las más pequeñas tienen prioridad sobre las grandes          │\n");
            printf("│  • La imagen procesada se guarda con sufijo '_processed'        │\n");
            printf("│  • El servidor puede manejar múltiples clientes simultáneamente │\n");
            printf("└─────────────────────────────────────────────────────────────────┘\n");
            
        } else if (strcmp(comando, "exit") == 0) {
            printf("\n👋 Cerrando cliente...\n");
            write(sockfd, "exit", 5);
            break;
            
        } else if (strlen(comando) == 0) {
            // Comando vacío, no hacer nada
            continue;
            
        } else {
            printf("❌ Comando no reconocido: '%s'\n", comando);
            printf("💡 Use 'help' para ver comandos disponibles\n");
        }
    }
    
    printf("✓ Cliente desconectado del servidor\n");
}

int main() {
    int sockfd, port;
    char ip_address[32];
    struct sockaddr_in servaddr;
    
    // Solicitar datos de conexión
    printf("🌐 CONFIGURACIÓN DE CONEXIÓN\n");
    printf("════════════════════════════\n");
    
    printf("IP del servidor: ");
    if (!fgets(ip_address, sizeof(ip_address), stdin)) {
        printf("❌ Error leyendo IP\n");
        exit(1);
    }
    ip_address[strcspn(ip_address, "\n")] = '\0';
    
    printf("Puerto del servidor: ");
    if (scanf("%d", &port) != 1) {
        printf("❌ Error leyendo puerto\n");
        exit(1);
    }
    while (getchar() != '\n'); // Limpiar buffer
    
    // Validar puerto
    if (port < 1 || port > 65535) {
        printf("❌ Puerto inválido (debe estar entre 1 y 65535)\n");
        exit(1);
    }
    
    // Crear socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("❌ Error creando socket\n");
        exit(1);
    }
    
    // Configurar dirección del servidor
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip_address);
    servaddr.sin_port = htons(port);
    
    if (servaddr.sin_addr.s_addr == INADDR_NONE) {
        printf("❌ Dirección IP inválida\n");
        exit(1);
    }
    
    printf("\n🔗 Conectando a %s:%d...\n", ip_address, port);
    
    // Conectar al servidor
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("❌ Error conectando al servidor\n");
        printf("💡 Verifique que el servidor esté ejecutándose\n");
        exit(1);
    }
    
    // Iniciar sesión interactiva
    cliente_interactivo(sockfd);
    
    // Cerrar socket
    close(sockfd);
    return 0;
}