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
#include <list>
#include <unordered_map>
#include <utility>


#define MAX_CLIENTS 3
#define CACHE_CAPACITY 10

// Thread-safe LRU Cache
class LRUCache
{
private:
    size_t capacity;
    std::list<std::pair<std::string, std::string>> cache_list; // (key, value) pairs
    std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> cache_map;
    mutable std::mutex cache_mutex;

public:
    LRUCache(size_t cap) : capacity(cap) {}

    // Get value from cache, returns empty string if not found
    std::string get(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache_map.find(key);
        if (it == cache_map.end())
        {
            return ""; // Not found
        }

        // Move accessed item to front (most recently used)
        cache_list.splice(cache_list.begin(), cache_list, it->second);
        return it->second->second;
    }

    // Check if key exists in cache
    bool contains(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return cache_map.find(key) != cache_map.end();
    }

    // Put value into cache
    void put(const std::string &key, const std::string &value)
    {
        std::lock_guard<std::mutex> lock(cache_mutex);

        auto it = cache_map.find(key);
        if (it != cache_map.end())
        {
            // Key exists, update value and move to front
            it->second->second = value;
            cache_list.splice(cache_list.begin(), cache_list, it->second);
            return;
        }

        // Evict least recently used if at capacity
        if (cache_list.size() >= capacity)
        {
            auto &last = cache_list.back();
            cache_map.erase(last.first);
            cache_list.pop_back();
        }

        // Insert new item at front
        cache_list.emplace_front(key, value);
        cache_map[key] = cache_list.begin();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return cache_list.size();
    }

    // Clear the cache
    void clear()
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_list.clear();
        cache_map.clear();
    }
};

// Global LRU cache instance
LRUCache response_cache(CACHE_CAPACITY);

// Helper to check if response is cacheable (only cache GET requests with 200 OK)
bool is_cacheable_response(const std::string &method, const std::string &response)
{
    if (method != "GET")
        return false;
    return true;
}
  
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
// Generate cache key from hostname + path
    std::string cache_key = hostname + ":" + port + path;

    // Check cache for GET requests
    if (method == "GET")
    {
        std::string cached_response = response_cache.get(cache_key);
        if (!cached_response.empty())
        {
            std::cout << "Cache HIT for: " << cache_key << std::endl;
            send(client_fd, cached_response.c_str(), cached_response.size(), 0);
            close(client_fd);
            return;
        }
        std::cout << "Cache MISS for: " << cache_key << std::endl;
    }


            std::string ip_address;

        addrinfo hints{}, *res;
        hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP

        int status = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);

        if (status != 0)
        {
            std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
            return;
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
            close(client_fd);
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
std::string full_response; // Buffer to store complete response for caching

       while (true)
    {
        ssize_t bytes = read(remote_socket, buffer, sizeof(buffer));
        if (bytes <= 0)
            break;

      full_response.append(buffer, bytes);
      
       ssize_t total_sent = 0;
        while (total_sent < bytes)
        {
            ssize_t sent = send(client_fd, buffer + total_sent, bytes - total_sent, 0);
            if (sent <= 0){
              client_alive = false;
                break;
            }
            total_sent += sent;
        }
          if (!client_alive)
            break;
    }
      
    // Cache the response if it's cacheable
    if (client_alive && is_cacheable_response(method, full_response))
    {
        response_cache.put(cache_key, full_response);
        std::cout << "Cached response for: " << cache_key << " (size: " << full_response.size() << " bytes)" << std::endl;
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
      std::unique_lock<std::mutex> lock(queue_mutex);
            while (tasks.empty() && stop == false)
            {
                cv.wait(lock);
            }
            if (stop == true and tasks.empty())
            {
                return;
            }
            int fd = tasks.front();
            tasks.pop();
            lock.unlock();
            handle_client(fd);
        }
    }
    void enqueue(int fd)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(fd);
        }
              cv.notify_one();
    }

    ~ThreadPool()
    {
        {
           std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();

        for (auto &t : workers)
            t.join();
    }
};
      int main()
{
    std::cout << "Main Function starting" << std::endl;

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
    address.sin_port = htons(8080);   //port 8080
    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cout << "Unable to bind";
    }
  int server_listen = listen(server_socket, MAX_CLIENTS);
    if (server_listen < 0)
    {
        std::cout << "Unale to listen";
    }
  ThreadPool thread_pool(5);
    while (true)
    {
      sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);

        int server_accept = accept(
            server_socket,
            (struct sockaddr *)&client_address,
            (socklen_t *)&client_len);
        if (server_accept < 0)
            continue;

        thread_pool.enqueue(server_accept);
    }
        
       
    close(server_socket);
}
