#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h> // read(), write(), close()
#define MAX 80 // Máximo tamaño del buffer 80 bytes
#define PORT 1717 // Puerto por defecto
#define SA struct sockaddr 
  
// Function designed for chat between client and server. 
// Esta función no comprueba si el tamaño es el correcto
// Solo acepta una conexión
void func(int connfd) 
{ 
    char buff[MAX]; 
    int n; 
    // infinite loop for chat 
    for (;;) { 
        bzero(buff, MAX); // Pone todo el buffer a ceros
  
        // read the message from client and copy it in buffer 
        read(connfd, buff, sizeof(buff)); 

        // print buffer which contains the client contents 
        printf("From client: %s\t To client : ", buff); 
        bzero(buff, MAX); 
        n = 0; 
        // copy server message in the buffer 
        while ((buff[n++] = getchar()) != '\n') 
            ; 
  
        // and send that buffer to client 
        write(connfd, buff, sizeof(buff)); 
  
        // if msg contains "Exit" then server exit and chat ended. 
        if (strncmp("exit", buff, 4) == 0) { 
            printf("Server Exit...\n"); 
            break; 
        } 
    } 
} 
  
// Driver function 
int main() 
{ 
    int sockfd, connfd, len; 
    struct sockaddr_in servaddr, cli; 
  
    // socket create and verification 
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // Creación de socket TCP IPv4
    if (sockfd == -1) { 
        printf("socket creation failed...\n"); 
        exit(0); 
    } 
    else
        printf("Socket successfully created..\n"); 
    bzero(&servaddr, sizeof(servaddr)); // Limía sockaddr_in
  
    // assign IP, PORT 
    servaddr.sin_family = AF_INET; // Indica IPv4
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Acepta conexiones por todas las interfaces locales
    servaddr.sin_port = htons(PORT); // Convierte el puerto a orden de bytes de red
  
    // Binding newly created socket to given IP and verification 
    // Enlaza el socket al puerto IṔ
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) { 
        printf("socket bind failed...\n"); 
        exit(0); 
    } 
    else
        printf("Socket successfully binded..\n"); 
  
    // Now server is ready to listen and verification 
    // POne el socket en modo escucha, el 5 representa la cantidad de conexiones que espera
    if ((listen(sockfd, 5)) != 0) { 
        printf("Listen failed...\n"); 
        exit(0); 
    } 
    else
        printf("Server listening..\n"); 
    len = sizeof(cli); 
  
    // Accept the data packet from client and verification 
    connfd = accept(sockfd, (SA*)&cli, &len); 
    if (connfd < 0) { 
        printf("server accept failed...\n"); 
        exit(0); 
    } 
    else
        printf("server accept the client...\n"); 
  
    // Function for chatting between client and server 
    func(connfd);  // Realiza el chat hasta que salga
  
    // After chatting close the socket 
    close(sockfd); 
}