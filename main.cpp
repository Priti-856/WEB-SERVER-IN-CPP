#include <sys/socket.h> // For socket functions
#include <netinet/in.h> // For sockaddr_in
#include <cstdlib>      // For exit() and EXIT_FAILURE
#include <iostream>     // For cout
#include <unistd.h>     // For read
#include <string>

#define Max_Clients 3
  
  int main()
{
    std::cout << "Main Function starting";

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        std::cout << "Unable to create the socket";
        exit(1);
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Accept any IP
    address.sin_port = htons(8080);       // Port 8080

    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cout << "Unable to bind";
    }

    int server_listen = listen(server_socket, Max_Clients);
    if (server_listen < 0)
    {
        std::cout << "Unale to listen";
    }

    while (true)
    {
        char buffer[1024] = {0};

        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);

        int server_accept = accept(
            server_socket,
            (struct sockaddr *)&client_address,
            (socklen_t *)&client_len);

        read(server_accept, buffer, 1024);
        std::cout << "Message from client: " << buffer << std::endl;

        // 7. Send response
        std::string body = "Hello from server";

        std::string http_response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n"
                                          "Connection: close\r\n"
                                          "\r\n" +
            body;
        send(server_accept, http_response.c_str(), http_response.size(), 0);
        close(server_accept);
    }

    close(server_socket);
}
