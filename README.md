# WEB-SERVER-IN-CPP

# Multi-Threaded HTTP Proxy Server with LRU Cache

A high-performance, multi-threaded **HTTP Proxy Server** written in C++ using Linux system sockets. The proxy acts as an intermediary, receiving HTTP requests from clients, checking a local thread-safe **LRU (Least Recently Used) Cache** for fast responses, and forwarding cache-miss requests to destination remote servers.

## 🚀 Features

- **Multi-Threaded Architecture:** Utilizes a custom-built, thread-safe `ThreadPool` to handle multiple client connections simultaneously.
- **Thread-Safe LRU Cache:** Implements a strict LRU caching mechanism protected by mutex locks to safely store and serve `GET` responses, reducing latency and external network calls.
- **Dynamic IP Resolution:** Resolves remote hostnames dynamically using `getaddrinfo` with support for both IPv4 and IPv6 addresses.
- **Efficient Resource Management:** Recycles socket descriptors properly and reuses server port (`SO_REUSEADDR`) to ensure quick restarts.

---

## 🛠️ System Architecture

1. **Client Request:** The client connects to the proxy on port `8080` and sends an HTTP request.
2. **Task Enqueue:** The proxy intercepts the connection and enqueues the client socket descriptor into the `ThreadPool`.
3. **Cache Lookup:** A worker thread picks up the task, parses the `Host:` header, generates a custom cache key (`hostname:port/path`), and checks the LRU Cache.
   - **Cache HIT:** Serves the response directly from memory and closes the client connection.
   - **Cache MISS:** Connects to the target remote server (Port 80/specified port), fetches the live data, sends it back to the client, and safely populates the cache.

---

## 💻 Getting Started

## 🛠️ VS Code Integration (macOS Setup)

If you are opening this project in **Visual Studio Code on macOS**, you need to configure the build task to use `C++11` or higher. Otherwise, the compiler will throw errors about `std::thread` or `std::mutex` not being supported.


### Prerequisites
- **Operating System:** Linux (including WSL on Windows) or **macOS**.
- **Compiler:** GCC or Clang supporting C++11 or higher.
- *(For macOS users)*: Ensure Xcode Command Line Tools are installed (run `xcode-select --install` in terminal if needed).
  
- ### 1. Prerequisites for VS Code
Ensure you have the following installed:
* **C/C++ Extension** by Microsoft (available in the VS Code Marketplace).
* **Xcode Command Line Tools** (Run `xcode-select --install` in your Mac terminal if you haven't already).

### 2. Configure `tasks.json`
To build the project directly using `Cmd + Shift + B` (or *Run Build Task*), create a folder named `.vscode` in your project root and add a `tasks.json` file with this exact configuration:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "type": "shell",
            "label": "C/C++: clang++ build active file",
            "command": "/usr/bin/clang++",
            "args": [
                "-std=c++11",
                "-fcolor-diagnostics",
                "-fansi-escape-codes",
                "${file}",
                "-o",
                "${fileDirname}/${fileBasenameNoExtension}"
            ],
            "options": {
                "cwd": "${fileDirname}"
            },
            "problemMatcher": [
                "$gcc"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "detail": "Compiler task generated for macOS Clang matching C++11"
        }
    ]
}
```

### 3. Mac Network Permission Note
When you run the compiled proxy binary inside VS Code's integrated terminal for the first time, macOS may trigger a firewall popup asking: *"Do you want the application to accept incoming network connections?"*. 
* Click **Allow**. Because this is a network proxy server listening on port `8080`, it must be allowed to accept incoming client connections.
Use code with caution.


### Compilation
Open your terminal and run the appropriate command for your OS:

* **On macOS:**
  ```bash
  g++ -std=c++11 main.cpp -o main
  ```

* **On Linux:**
  ```bash
  g++ -std=c++11 -pthread main.cpp -o main
  ```
