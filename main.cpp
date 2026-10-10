#include <sys/socket.h> // For socket functions
#include <netinet/in.h> // For sockaddr_in
#include <cstdlib>      // For exit() and EXIT_FAILURE
#include <iostream>     // For cout
#include <unistd.h>     // For read
#include <string>
#include<netdb.h>
#include<arpa/inet.h>
#include <queue>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>


#define MAX_CLIENTS 3
  
 void handle_client(int client_fd)
{
    char buffer[1024] = {0};
    std::string request;

   while(true)
    {
       auto request_data = read(client_fd, buffer, sizeof(buffer));
        if (request_data < 0)
        {
            std::cout << "Error in read" << std::endl;
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

  
     if (path.find("http://") == 0)
        {
            std::size_t pos = path.find("/", 7);
            if (pos != std::string::npos)
                path = path.substr(pos);
            else
                path = "/";
        }


            std::string ip_address;

        addrinfo hints{}, *res;
        hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP

        int status = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);

        if (status != 0)
        {
            std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
            continue;
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
      int remote_socket = -1;
        addrinfo *p;

        for (p = res; p != nullptr; p = p->ai_next)
        {
            remote_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (remote_socket < 0)
                continue;

            if (connect(remote_socket, p->ai_addr, p->ai_addrlen) == 0)
                break; // success

            close(remote_socket);
        }

        if (p == nullptr)
        {
            std::cerr << "Failed to connect to any resolved address\n";
            close(server_accept);
            freeaddrinfo(res);
            return;
        }

        std::cout << "Connected to remote server!" << std::endl;

        freeaddrinfo(res);

          // 7. Send response
    std::string forward_request = method + " " + path + " " + version + "\r\n";

    // Append rest of headers (skip first line)
    std::size_t first_line_end = request.find("\r\n");
    forward_request += request.substr(first_line_end + 2);

    // Send to remote
    send(remote_socket, forward_request.c_str(), forward_request.size(), 0);
    bool client_alive = true;

       while (true)
    {
        ssize_t bytes = read(remote_socket, buffer, sizeof(buffer));
        if (bytes <= 0)
            break;

       ssize_t total_sent = 0;
        while (total_sent < bytes)
        {
            ssize_t sent = send(client_fd, buffer + total_sent, bytes - total_sent, 0);
            if (sent <= 0){
              client_alive = false;
                break;
            }
          if (!client_alive)
            break;
    }
   close(remote_socket);
    close(client_fd);
}
      class ThreadPool
{
public:
    std::vector<std::thread> workers;
    std::queue<int> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool stop;

    ThreadPool(int num_threads)
    {
        this->stop = false;
        for (int i = 0; i < num_threads; ++i)
        {
            workers.emplace_back([this]()
                                 { worker_run(); });
        }
      }
    void worker_run()
    {
        while (true){
      
        
        close(server_accept);
    }

    close(server_socket);
}
