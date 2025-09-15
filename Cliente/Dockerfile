# Dockerfile para cliente de procesamiento de imágenes RGB
FROM gcc:latest

# Instalar dependencias necesarias
RUN apt-get update && apt-get install -y \
    make \
    && rm -rf /var/lib/apt/lists/*

# Crear directorio de trabajo
WORKDIR /app

# Crear directorio para imágenes
RUN mkdir -p /app/images

# Copiar archivos del cliente
COPY cn.c .
COPY stb_image.h .
COPY Makefile .

# Copiar imágenes de prueba al contenedor
COPY images/ /app/images/

# Compilar el cliente usando el Makefile
RUN make cliente

# Configurar variables de entorno
ENV IMAGES_DIRECTORY=/app/images

# El contenedor ejecutará el cliente interactivamente
CMD ["./cliente"]