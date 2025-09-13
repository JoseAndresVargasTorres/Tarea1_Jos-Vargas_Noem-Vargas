# Dockerfile para client.c
FROM gcc:latest

# Crear directorio de trabajo dentro del contenedor
WORKDIR /app

# Copiar el código fuente del cliente al contenedor
COPY cn.c .
COPY stb_image.h .
COPY faro.jpg .

# Compilar el cliente
RUN gcc cn.c -o client -lm

# El contenedor se ejecutará iniciando el cliente
ENTRYPOINT ["./client"]