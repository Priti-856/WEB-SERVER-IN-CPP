#include <sys/socket.h> // For socket functions
#include <netinet/in.h> // For sockaddr_in
#include <cstdlib>      // For exit() and EXIT_FAILURE
#include <iostream>     // For cout
#include <unistd.h>     // For read
#include <string>
#include<netdb.h>
#include<arpa/inet.h>


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
        //char buffer[1024] = {0};

        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);

        int server_accept = accept(
            server_socket,
            (struct sockaddr *)&client_address,
            (socklen_t *)&client_len);

        char buffer[1024] = {0};
        std::string request;

        while (true)
        {
            auto request_data = read(server_accept, buffer, sizeof(buffer));
            if (request_data < 0)
            {
                std::cout << "Read in read" << std::endl;
                break;
            }

            if (request_data == 0)
            {
                // client closed connection
                break;
            }

            request.append(buffer, request_data);
            if (request.find("\r\n\r\n") != std::string::npos)
            {
                break;
            }
        }

        std::cout << "Message from client: " << request << std::endl;

           std::string host_header;
        std::size_t host_pos = request.find("Host:");
        std::string hostname;
        std::string port;
        if (host_pos != std::string::npos)
        {
            std::size_t start = host_pos + 6;
            std::size_t end = request.find("\r\n", start);
            host_header = request.substr(start, end - start);
            auto spilt_index = host_header.find(":");
            if (spilt_index == std::string::npos)
            {
                hostname = host_header;
                port = "80";
            }
            else
            {
                hostname = host_header.substr(0, spilt_index);
                port = host_header.substr(spilt_index + 1);
            }
        }

        std::istringstream stream(request);
        std::string method, path, version;

        stream >> method >> path >> version;
        std::cout << "Method: " << method << std::endl;
        std::cout << "Path: " << path << std::endl;
        std::cout << "Version: " << version << std::endl;
        std::cout << "Host Header: " << host_header << std::endl;
        std::cout << "Host: " << hostname << std::endl;
        std::cout << "Port: " << port << std::endl;


            std::string ip_address;

        addrinfo hints{}, *res;
        hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP

        int status = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);

        if (status != 0)
        {
            std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
            break;
        }
        char ipstr[INET6_ADDRSTRLEN];

        for (addrinfo *p = res; p != nullptr; p = p->ai_next)
        {

            void *addr;

            if (p->ai_family == AF_INET)
            {
                sockaddr_in *ipv4 = (sockaddr_in *)p->ai_addr;
                addr = &(ipv4->sin_addr);
            }
            else
            { // AF_INET6
                sockaddr_in6 *ipv6 = (sockaddr_in6 *)p->ai_addr;
                addr = &(ipv6->sin6_addr);
            }

            inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);

            ip_address = ipstr;
            std::cout << "Resolved IP: " << ip_address << std::endl;

            break; // take first valid one
        }

      
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
