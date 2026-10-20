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
#include<sstream>

#define MAX_CLIENTS 3
#define CACHE_CAPACITY 10

// Thread-safe LRU Cache
class LRUCache
{
private:
    size_t capacity;
    list<pair<string, string>> cache_list; // (key, value) pairs
    unordered_map<string, list<pair<string, string>>::iterator> cache_map;
    mutable mutex cache_mutex;

public:
    LRUCache(size_t cap) : capacity(cap) {}

    // Get value from cache, returns empty string if not found
    string get(const string &key)
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
        lock_guard<std::mutex> lock(cache_mutex);

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
bool is_cacheable_response(const string &method, const string &response)
{
    if (method != "GET")
        return false;
    return true;
}
  
 void handle_client(int client_fd)
{
    char buffer[1024] = {0};
    string request;

   while(true)
    {
       auto request_data = read(client_fd, buffer, sizeof(buffer));
        if (request_data < 0)
        {
            cout << "Error in read" << std::endl;
            break;
        }

        if (request_data == 0)
        {
            // client closed connection
            break;
        }

        request.append(buffer, request_data);
        if (request.find("\r\n\r\n") != string::npos)
        {
            break;
        }
    }
    cout << "Message from client: " << request << endl;     

       string host_header;
    size_t host_pos = request.find("Host:");
    string hostname;
    string port;
    
        if (host_pos != string::npos)
        {
            size_t start = host_pos + 6;
            size_t end = request.find("\r\n", start);
            host_header = request.substr(start, end - start);
            auto spilt_index = host_header.find(":");
            if (spilt_index == string::npos)
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

        istringstream stream(request);
        string method, path, version;

        stream >> method >> path >> version;
        cout << "Method: " << method << std::endl;
        cout << "Path: " << path << std::endl;
        cout << "Version: " << version << std::endl;
        cout << "Host Header: " << host_header << std::endl;
        cout << "Host: " << hostname << std::endl;
        cout << "Port: " << port << std::endl;

  
     if (path.find("http://") == 0)
        {
            size_t pos = path.find("/", 7);
            if (pos != string::npos)
                path = path.substr(pos);
            else
                path = "/";
        }
// Generate cache key from hostname + path
    std::string cache_key = hostname + ":" + port + path;

    // Check cache for GET requests
    if (method == "GET")
    {
        string cached_response = response_cache.get(cache_key);
        if (!cached_response.empty())
        {
            cout << "Cache HIT for: " << cache_key << endl;
            send(client_fd, cached_response.c_str(), cached_response.size(), 0);
            close(client_fd);
            return;
        }
        cout << "Cache MISS for: " << cache_key << endl;
    }


            string ip_address;

        addrinfo hints{}, *res;
        hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP

        int status = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);

        if (status != 0)
        {
            cerr << "getaddrinfo error: " << gai_strerror(status) << endl;
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
            cout << "Resolved IP: " << ip_address << endl;

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

        std::cout << "Connected to remote server!" << endl;

        freeaddrinfo(res);

          // 7. Send response
   string forward_request = method + " " + path + " " + version + "\r\n";

    // Append rest of headers (skip first line)
   size_t first_line_end = request.find("\r\n");
    forward_request += request.substr(first_line_end + 2);

    // Send to remote
    send(remote_socket, forward_request.c_str(), forward_request.size(), 0);
    bool client_alive = true;
string full_response; // Buffer to store complete response for caching

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
       cout << "Cached response for: " << cache_key << " (size: " << full_response.size() << " bytes)" <<endl;
    }

      
   close(remote_socket);
    close(client_fd);
}
      class ThreadPool
{
public:
    vector<std::thread> workers;
    queue<int> tasks;
    mutex queue_mutex;
   condition_variable cv;
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
      unique_lock<mutex> lock(queue_mutex);
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
            unique_lock<mutex> lock(queue_mutex);
            tasks.push(fd);
        }
              cv.notify_one();
    }

    ~ThreadPool()
    {
        {
           unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();

        for (auto &t : workers)
            t.join();
    }
};
      int main()
{
    cout << "Main Function starting" << endl;

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        cout << "Unable to create the socket";
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
        cout << "Unable to bind";
    }
  int server_listen = listen(server_socket, MAX_CLIENTS);
    if (server_listen < 0)
    {
        cout << "Unale to listen";
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
