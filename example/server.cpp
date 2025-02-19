// g++ server.cpp -o server -libverbs

#include <iostream>
#include <infiniband/verbs.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "infiniband.hpp"

#define PORT 8080

int create_server(int &server_fd)
{
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        std::cerr << "Socket failed" << std::endl;
        return 1;
    }

    // Bind address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cerr << "Bind failed" << std::endl;
        return -1;
    }

    // Listen for connections
    if (listen(server_fd, 3) < 0)
    {
        std::cerr << "Listen failed" << std::endl;
        return -1;
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    // Accept a connection
    int new_socket = accept(server_fd, (struct sockaddr *)&address, &addr_len);
    if (new_socket < 0)
    {
        std::cerr << "Accept failed" << std::endl;
        return -1;
    }

    return new_socket;
}

int main(void)
{
    int server_fd;
    int socket;
    if ((socket = create_server(server_fd)) != -1) {
        infiniband(socket);
    }
}