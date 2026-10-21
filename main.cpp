#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cctype>
#include <iostream>
#include <list>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

constexpr int PORT = 8080;
constexpr int WORKER_COUNT = 5;
constexpr std::size_t CACHE_CAPACITY = 10;
constexpr std::size_t MAX_PENDING_CLIENTS = 100;
constexpr std::size_t BUFFER_SIZE = 4096;

using namespace std;

// -------------------- Thread-safe LRU cache --------------------

class LRUCache {
    using Entry = pair<string, string>;

    size_t capacity;
    list<Entry> items; // Most recently used item is at the front.
    unordered_map<string, list<Entry>::iterator> index;
    mutex mtx;

public:
    explicit LRUCache(size_t cap) : capacity(cap) {}

    bool get(const string& key, string& value) {
        lock_guard<mutex> lock(mtx);

        auto it = index.find(key);
        if (it == index.end()) return false;

        items.splice(items.begin(), items, it->second);
        value = it->second->second;
        return true;
    }

    void put(const string& key, const string& value) {
        lock_guard<mutex> lock(mtx);

        auto it = index.find(key);
        if (it != index.end()) {
            it->second->second = value;
            items.splice(items.begin(), items, it->second);
            return;
        }

        if (capacity == 0) return;

        if (items.size() == capacity) {
            index.erase(items.back().first);
            items.pop_back();
        }

        items.emplace_front(key, value);
        index[key] = items.begin();
    }
};

LRUCache responseCache(CACHE_CAPACITY);

// -------------------- Small HTTP helpers --------------------

string trim(const string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

string toLower(string text) {
    transform(text.begin(), text.end(), text.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return text;
}

struct HttpRequest {
    string method;
    string target;
    string version;
    string host;
    string port = "80";
    string path;
    string headers;
};

bool parseRequest(const string& raw, HttpRequest& req) {
    const size_t lineEnd = raw.find("\r\n");
    if (lineEnd == string::npos) return false;

    istringstream firstLine(raw.substr(0, lineEnd));
    if (!(firstLine >> req.method >> req.target >> req.version)) return false;

    const size_t headersEnd = raw.find("\r\n\r\n");
    if (headersEnd == string::npos) return false;
    req.headers = raw.substr(lineEnd + 2, headersEnd - (lineEnd + 2));

    // Find Host header without depending on its capitalization.
    istringstream headerStream(req.headers);
    string line;
    while (getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t colon = line.find(':');
        if (colon == string::npos) continue;

        if (toLower(trim(line.substr(0, colon))) == "host") {
            string hostValue = trim(line.substr(colon + 1));

            // This simple proxy supports hostname:port and ordinary hostnames.
            // IPv6 literals in Host headers should be bracketed, e.g. [::1]:8080.
            if (!hostValue.empty() && hostValue.front() == '[') {
                const size_t bracket = hostValue.find(']');
                if (bracket == string::npos) return false;
                req.host = hostValue.substr(1, bracket - 1);
                if (bracket + 1 < hostValue.size() && hostValue[bracket + 1] == ':')
                    req.port = hostValue.substr(bracket + 2);
            } else {
                const size_t hostColon = hostValue.rfind(':');
                if (hostColon != string::npos && hostValue.find(':') == hostColon) {
                    req.host = hostValue.substr(0, hostColon);
                    req.port = hostValue.substr(hostColon + 1);
                } else {
                    req.host = hostValue;
                }
            }
            break;
        }
    }

    // Proxies commonly receive absolute URLs; convert them to origin-form.
    if (req.target.rfind("http://", 0) == 0) {
        const size_t authorityStart = 7;
        const size_t pathStart = req.target.find('/', authorityStart);
        const string authority = req.target.substr(
            authorityStart, pathStart == string::npos ? string::npos : pathStart - authorityStart);

        // Use the URL authority if a Host header was not present.
        if (req.host.empty()) {
            const size_t colon = authority.rfind(':');
            if (colon != string::npos && authority.find(':') == colon) {
                req.host = authority.substr(0, colon);
                req.port = authority.substr(colon + 1);
            } else {
                req.host = authority;
            }
        }
        req.path = (pathStart == string::npos) ? "/" : req.target.substr(pathStart);
    } else {
        req.path = req.target;
    }

    return !req.host.empty() && !req.port.empty() && !req.path.empty();
}

int connectToServer(const string& host, const string& port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;       // Try IPv4 and IPv6.
    hints.ai_socktype = SOCK_STREAM;   // TCP.

    addrinfo* results = nullptr;
    const int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
    if (status != 0) {
        cerr << "getaddrinfo: " << gai_strerror(status) << '\n';
        return -1;
    }

    int remoteFd = -1;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
        remoteFd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (remoteFd == -1) continue;

        if (connect(remoteFd, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }

        close(remoteFd);
        remoteFd = -1;
    }

    freeaddrinfo(results);
    return remoteFd;
}

bool sendAll(int fd, const char* data, size_t length) {
    size_t sentTotal = 0;
    while (sentTotal < length) {
        const ssize_t sent = send(fd, data + sentTotal, length - sentTotal, 0);
        if (sent <= 0) return false;
        sentTotal += static_cast<size_t>(sent);
    }
    return true;
}

bool isSuccessfulGet(const HttpRequest& req, const string& response) {
    if (req.method != "GET") return false;

    const size_t lineEnd = response.find("\r\n");
    if (lineEnd == string::npos) return false;

    // Cache only HTTP 200 responses.
    const string statusLine = response.substr(0, lineEnd);
    return statusLine.find(" 200 ") != string::npos ||
           statusLine.size() >= 4 && statusLine.substr(statusLine.size() - 4) == " 200";
}

// -------------------- Handle one client connection --------------------

void handleClient(int clientFd) {
    string rawRequest;
    char buffer[BUFFER_SIZE];

    // Read until the end of the HTTP headers.
    while (rawRequest.find("\r\n\r\n") == string::npos) {
        const ssize_t bytes = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            close(clientFd);
            return;
        }
        rawRequest.append(buffer, static_cast<size_t>(bytes));

        // Keep this small example from accepting unbounded header data.
        if (rawRequest.size() > 64 * 1024) {
            close(clientFd);
            return;
        }
    }

    HttpRequest req;
    if (!parseRequest(rawRequest, req)) {
        const string error = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        sendAll(clientFd, error.c_str(), error.size());
        close(clientFd);
        return;
    }

    const string cacheKey = req.host + ":" + req.port + req.path;
    string cachedResponse;

    if (req.method == "GET" && responseCache.get(cacheKey, cachedResponse)) {
        sendAll(clientFd, cachedResponse.c_str(), cachedResponse.size());
        close(clientFd);
        return;
    }

    const int remoteFd = connectToServer(req.host, req.port);
    if (remoteFd == -1) {
        const string error = "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        sendAll(clientFd, error.c_str(), error.size());
        close(clientFd);
        return;
    }

    // Forward the request line in origin-form and ask the server to close
    // after the response, making response reading straightforward.
    string forwarded = req.method + " " + req.path + " " + req.version + "\r\n";
    istringstream headerStream(req.headers);
    string line;
    bool hasConnectionHeader = false;

    while (getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon != string::npos &&
            toLower(trim(line.substr(0, colon))) == "connection") {
            hasConnectionHeader = true;
            continue; // Replace client preference so the upstream closes cleanly.
        }
        if (!line.empty()) forwarded += line + "\r\n";
    }

    (void)hasConnectionHeader;
    forwarded += "Connection: close\r\n";
    forwarded += "\r\n";

    if (!sendAll(remoteFd, forwarded.c_str(), forwarded.size())) {
        close(remoteFd);
        close(clientFd);
        return;
    }

    string fullResponse;
    bool clientAlive = true;

    // Stream the response to the client while retaining a copy for the cache.
    while (true) {
        const ssize_t bytes = recv(remoteFd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        fullResponse.append(buffer, static_cast<size_t>(bytes));
        if (!sendAll(clientFd, buffer, static_cast<size_t>(bytes))) {
            clientAlive = false;
            break;
        }
    }

    if (clientAlive && isSuccessfulGet(req, fullResponse)) {
        responseCache.put(cacheKey, fullResponse);
    }

    close(remoteFd);
    close(clientFd);
}

// -------------------- Fixed-size worker thread pool --------------------

class ThreadPool {
    vector<thread> workers;
    queue<int> pendingClients;
    mutex queueMutex;
    condition_variable queueReady;
    bool stopping = false;

    void workerLoop() {
        while (true) {
            int clientFd;

            {
                unique_lock<mutex> lock(queueMutex);
                queueReady.wait(lock, [this] {
                    return stopping || !pendingClients.empty();
                });

                if (stopping && pendingClients.empty()) return;

                clientFd = pendingClients.front();
                pendingClients.pop();
            }

            handleClient(clientFd);
        }
    }

public:
    explicit ThreadPool(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            workers.emplace_back([this] { workerLoop(); });
        }
    }

    bool enqueue(int clientFd) {
        {
            lock_guard<mutex> lock(queueMutex);
            if (stopping || pendingClients.size() >= MAX_PENDING_CLIENTS)
                return false;
            pendingClients.push(clientFd);
        }
        queueReady.notify_one();
        return true;
    }

    ~ThreadPool() {
        {
            lock_guard<mutex> lock(queueMutex);
            stopping = true;
        }
        queueReady.notify_all();

        for (thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }

        while (!pendingClients.empty()) {
            close(pendingClients.front());
            pendingClients.pop();
        }
    }
};

// -------------------- Start proxy server --------------------

int main() {
    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        cerr << "Could not create server socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        cerr << "bind failed\n";
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 100) == -1) {
        cerr << "listen failed\n";
        close(serverFd);
        return 1;
    }

    cout << "HTTP proxy listening on port " << PORT << '\n';
    ThreadPool pool(WORKER_COUNT);

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);

        const int clientFd = accept(
            serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);

        if (clientFd == -1) continue;

        if (!pool.enqueue(clientFd)) {
            const string busy =
                "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            sendAll(clientFd, busy.c_str(), busy.size());
            close(clientFd);
        }
    }

    close(serverFd);
    return 0;
}
