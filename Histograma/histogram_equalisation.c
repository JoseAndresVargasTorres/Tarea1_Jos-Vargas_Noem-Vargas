#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"



// Función que aplica la ecualización del histograma a los pixeles y genera una nueva imagen
// De ser el caso, esta es la función que debe estar en el servidor
int Histograma_Ecualizacion(unsigned char *pixels, int *ancho, int *alto) {

    // Calcular la frecuencia acumulada o CDF
    int frecuencia[256] = {0}; // 255 + 1, para la frecuencia individual
    int frecuencia_acumulada[256] = {0}; // Para la frecuencia total o acumulada (suma de todas)

    // Contar pixeles de cada intensidad 
    for(int i = 0; i < ((*ancho) * (*alto)); i++){
        unsigned char valor = pixels[i];
        frecuencia[valor]++;
    }

    // Calcular la frecuencia acumulada
    frecuencia_acumulada[0] = frecuencia[0]; // Asignar primer valor

    for(int i = 1; i < 256; i++){
        frecuencia_acumulada[i] = frecuencia_acumulada[i-1] + frecuencia[i];
    }

    // Calcular el nuevo valor del pixel
    for(int i = 0; i < ((*ancho) * (*alto)); i++){ // Recorrer todos los pixeles
    unsigned char Nuevo_pixel = (frecuencia_acumulada[pixels[i]] * 255) / ((*ancho) * (*alto));
    pixels[i] = Nuevo_pixel;
    }

    //Convertir los pixeles a una imagen
    stbi_write_jpg("imagen_ecualizada.jpg", *ancho, *alto, 1, pixels, 100);
    // nombre, tamaño ancho y alto, cantidad canales, el array, calidad 

    return 0; //Funcionó



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

int main() {
    const char *image_original = "img/girl.gif";
    //const char *image_pgm = "imagen.pgm";
    int ancho, alto;

    // Llama a la función para convertir la imagen
    unsigned char *pixels = Convertir_PGM(image_original, &ancho, &alto);
    if (!pixels) return 1;

    // Print del ancho y alto para comprobar
    //printf("Ancho: %d\n", ancho);
    //printf("Alto: %d\n", alto);

    // Aplicar el histograma
    Histograma_Ecualizacion(pixels, &ancho, &alto);
    

    stbi_image_free(pixels); // liberar memoria
    return 0;
}

// Para compilar:
// gcc histogram_equalisation.c -o histogram_equalisation -lm
// ./histogram_equalisation