#include <iostream>            // C++标准输入输出流，用于控制台输入输出
#include <cstring>             // C风格字符串操作函数，如strlen、memset等
#include <vector>              // C++动态数组容器，用于存储线程等对象
#include <thread>              // C++11线程库，提供std::thread类
#include <mutex>               // C++11互斥锁，用于线程同步
#include <condition_variable>  // C++11条件变量，用于线程间通信
#include <queue>               // C++队列容器，用于任务队列
#include <atomic>              // C++11原子操作，提供线程安全的原子变量
#include <functional>          // C++函数对象和包装器，用于回调函数
#include <memory>              // C++智能指针，用于自动内存管理
#include <sys/socket.h>        // 核心Socket API：socket()、bind()、listen()、accept()等
#include <netinet/in.h>        // 因特网地址族结构：sockaddr_in、INADDR_ANY等
#include <arpa/inet.h>         // IP地址转换函数：inet_pton()、inet_ntop()等
#include <unistd.h>            // POSIX系统服务：close()、read()、write()、fork()等
#include <signal.h>            // 信号处理函数：signal()、sigaction()等
#include <fcntl.h>             // 文件控制选项：fcntl()，用于设置非阻塞I/O

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

// 线程安全日志
class Logger{
private:
    std::mutex mtx_;
public:
    // 带日志级别输出
    template <typename ...Args>
    void info(Args&& ...args){
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << "[INFO] ";
        (std::cout << ... << std::forward<Args>(args)) << std::endl;
    }

    template <typename ...Args>
    void error(Args&& ...args){
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << "[ERROR] ";
        (std::cout << ... << std::forward<Args>(args)) << std::endl;
    }
};



// 线程池
class ThreadPool{
private:
    void worker();
    std::vector<std::thread> threadpool;
    std::queue<std::function<void()>> task_queue;
    std::condition_variable task_available;
    std::mutex queue_mtx;
    std::atomic<bool> stop_flag{false};
    Logger& _logger;

public:
    ThreadPool(size_t thread_num, Logger& logger): _logger(logger){
        for(size_t i = 0; i < thread_num; ++i){
            threadpool.emplace_back(&ThreadPool::worker, this);
        }
        _logger.info("线程池创建完成");
    }

    ~ThreadPool(){
        stop();
    }

    template <class F, class ...Args>
    void add_task(F&& f, Args ...args){
        std::unique_lock<std::mutex> lock(queue_mtx);
        task_queue.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        lock.unlock();

        task_available.notify_one();
        _logger.info("任务添加成功!");
    }

    void stop(){
        stop_flag.store(true);
        task_available.notify_all();
        std::queue<std::function<void()>> empty;
        queue_mtx.lock();
        std::swap(task_queue, empty);
        queue_mtx.unlock();
        for(auto &t : threadpool){
            if(t.joinable()){
                t.join();
            }
        }
         _logger.info("线程池销毁完成");
    }

};

void ThreadPool::worker(){
    std::function<void()> task;
    while(true){
        std::unique_lock<std::mutex> lock(queue_mtx);
        task_available.wait(lock, [this](){return !task_queue.empty() || stop_flag.load();});

        if(stop_flag.load()) return;
        task = std::move(task_queue.front());
        task_queue.pop();
        lock.unlock();

        task();
    }
}



// 连接处理器类
class ConnectionHandler{
private:
    Logger& _logger;

public:
    ConnectionHandler(Logger& logger) : _logger(logger){}

    ~ConnectionHandler(){}

    void handle(int client_fd, sockaddr_in client_addr){
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        _logger.info("线程PID: ", getpid(), 
                    " 连接客户端: ", client_ip, ":", ntohs(client_addr.sin_port));

        // 数据交互
        char buffer[BUFFER_SIZE];
        fd_set readfds;
        while(true){
            FD_ZERO(&readfds);
            FD_SET(client_fd, &readfds);

            struct timeval tv = {1, 0};
            int activity = select(client_fd + 1, &readfds, NULL, NULL, &tv);
            if(activity < 0){
                std::cerr << "[ERROR] ";
                perror("Select Failed");
            }
            else if(activity == 0){
                continue;
            }

            if(!FD_ISSET(client_fd, &readfds)){
                _logger.error("FD_ISSET Failed!");
                continue;
            }

            // 接收数据
            ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE, 0);
            if(bytes_read > 0){
                buffer[bytes_read] = '\0';
                std::cout << "\nFrom client " << client_ip << ":" << ntohs(client_addr.sin_port) 
                            << "\nReceived:" << buffer << std::endl;
            }
            else if(bytes_read < 0){
                if(errno == EINTR) {}
                std::cerr << "[ERROR] ";
                perror("Receive Failed");
            }
            else if(bytes_read == 0){
                std::cerr << "[ERROR] ";
                perror("客户端连接已关闭");
                break;
            }

            // 发送响应
            std::string response = "\nHTTP/1.1 200 OK\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: 21\r\n"
                                "\r\n"
                                "Hello from thread pool\n";
            ssize_t send_len = send(client_fd, response.c_str(), response.size(), 0);
            if(send_len > 0){
                _logger.info("Send response");
            }
            else if(send_len == 0){
                std::cerr << "[ERROR] ";
                perror("客户端连接已关闭");
                break;
            }
            else if(send_len < 0){
                if(errno == EINTR) {}
                std::cerr << "[ERROR] ";
                perror("Send Failed");
            }
        }

        close(client_fd);
        _logger.info("线程PID: ", getpid(),
            " 关闭客户端连接: ", client_ip, ":", ntohs(client_addr.sin_port));
    }
};


// 主服务器类
class ThreadServer{
private:
    int server_fd;
    Logger _logger;
    std::unique_ptr<ThreadPool> _thread_pool;
    ConnectionHandler _handler;

public:
    static std::atomic<bool> _running;
    ThreadServer() : _handler(_logger){
        // 1. 创建socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(server_fd == -1){
            perror("Create Socket Failed");
            throw std::runtime_error("Failed to create socket");
        }

        // 2. 设置地址重用
        int opt = 1;
        if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            close(server_fd);
            perror("Setsockopt Failed");
            throw std::runtime_error("Failed to Setsockopt");
        }

        // 3. 绑定地址
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);
        if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
            close(server_fd);
            perror("Bind Address Failed");
            throw std::runtime_error("Failed to bind address");
        }

        // 4. 监听
        if (listen(server_fd, 10) < 0){
            close(server_fd);
            perror("Listening Failed");
            throw std::runtime_error("Failed to listen");
        }

        _logger.info("Server initialized on port ", PORT);
    }

    void start(size_t threadpool_size = 4){
        if(_running) return;

        _running.store(true);

        // 创建线程池
        _thread_pool = std::make_unique<ThreadPool>(threadpool_size, _logger);
        _logger.info("Starting server with ", threadpool_size, " handler threads");

        fd_set readfds;
        // 主接收循环
        while(_running.load()){
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);
            struct timeval tv = {1, 0}; // 1秒超时
            int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);
            if(activity < 0){
                if(errno == EINTR) continue;
                if(!_running) break;
                std::cout << "[ERROR] ";
                perror("Select Failed");
                continue;
            }
            else if(activity == 0){
                continue;
            }

            // 接受新连接
            if(!FD_ISSET(server_fd, &readfds)) continue;
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if(client_fd < 0){
                if(errno == EINTR) continue;
                if(!_running) break;
                std::cout << "[ERROR] ";
                perror("Accept Failed");
                continue;
            }

            // 将连接交给线程池处理
            // 永远记住：非静态成员函数必须与对象实例一起使用。你不能单独传递它。使用lambda或std::bind来绑定对象实例是最常见的解决方案。
            _thread_pool->add_task([this, client_fd, client_addr](){
                _handler.handle(client_fd, client_addr);
            });
        }
        _logger.info("服务器接收连接关闭");
    }

    void stop(){
        _running.store(false);

        // 关闭server socket来中断accept
        if(server_fd >= 0){
            close(server_fd);
            server_fd = -1;
        }

        // 销毁线程池
        _thread_pool.reset();

        _logger.info("服务器已关闭");
    }

    ~ThreadServer(){
        stop();
    }
};
std::atomic<bool> ThreadServer::_running{false};


// 信号处理
void setup_signal_handler(){
    struct sigaction sa;
    sa.__sigaction_u.__sa_handler = [](int){
        std::cout << "\n[INFO] ";
        std::cout << "正在关闭服务器..." << std::endl;
        ThreadServer::_running.store(false);
    };

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // 忽略SIGPIPE
    signal(SIGPIPE, SIG_IGN);
}


int main(){
    setup_signal_handler();

    try{
        ThreadServer server;

        // 在单独的线程中启动服务器
        std::thread server_thread([&server](){
            server.start(10); // 10个工作线程
        });

        std::cout << "[INFO] Server running. Press Ctrl+C to stop." << std::endl;

        // 主线程等待服务器线程结束
        server_thread.join();
    }
    catch (const std::exception &e){
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return -1;
    }
    return 0;
}