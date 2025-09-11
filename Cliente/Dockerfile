# Dockerfile para client.c
FROM gcc:latest

# Crear directorio de trabajo dentro del contenedor
WORKDIR /app

# Copiar el código fuente del cliente al contenedor
COPY client.c .

# Compilar el cliente
RUN gcc client.c -o client

# El contenedor se ejecutará iniciando el cliente
ENTRYPOINT ["./client"]
