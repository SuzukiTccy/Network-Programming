#include <iostream>          // 标准输入输出流，用于控制台I/O操作
#include <cstring>           // C风格字符串操作函数，如strlen、strerror等
#include <sys/socket.h>      // 核心Socket API，提供socket、bind、listen等系统调用
#include <netinet/in.h>      // Internet地址结构定义，包含sockaddr_in和IP协议常量
#include <arpa/inet.h>       // IP地址转换函数，如inet_pton、inet_ntop等
#include <unistd.h>          // POSIX系统服务API，提供close、read、write等文件操作
#include <poll.h>            // poll系统调用头文件，用于I/O多路复用
#include <vector>            // C++标准模板库向量容器，用于动态数组管理
#include <csignal>           // 信号处理头文件，用于捕获和处理信号，如SIGINT、SIGQUIT等
#include <atomic>            // 原子操作头文件，用于实现线程安全的计数器

const int PORT = 8080;  // 服务器监听端口
const int BUFFER_SIZE = 1024;  // 缓冲区大小
const int MAX_CLIENTS = 10;  // 最大客户端连接数

std::atomic<bool> _running{false};
void signalHandler() {
    struct sigaction sa;
    sa.sa_handler = [](int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            std::cout << "[INFO] " << "收到退出信号: " << signal
                        << " 正在关闭服务器..." << std::endl;
            _running.store(false);
            return;
        }
        std::cout << "[INFO] " << "收到信号: " << signal << std::endl;
    };

    sigemptyset(&sa.sa_mask); // 清空信号集
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // 设置信号处理标志

    sigaction(SIGINT, &sa, NULL); // 捕获SIGINT信号
    sigaction(SIGTERM, &sa, NULL); // 捕获SIGTERM信号
    signal(SIGPIPE, SIG_IGN); // 忽略SIGPIPE信号
}


int main() {
    signalHandler();
    // 1. 创建监听socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[ERROR] ";
        perror("Socket Creation Failed");
        return -1;
    }

    // 2. 设置地址重用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[ERROR] ";
        perror("Setsockopt Failed");
        close(server_fd);
        return -1;
    }

    // 3. 绑定地址和端口
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[ERROR] ";
        perror("Bind Failed");
        close(server_fd);
        return -1;
    }

    // 4. 监听连接
    if(listen(server_fd, MAX_CLIENTS) < 0) {
        std::cerr << "[ERROR] ";
        perror("Listen Failed");
        close(server_fd);
        return -1;
    }
    std::cout << "[INFO] " << "服务器进程: " << getpid() << std::endl;
    std::cout << "[INFO] " << "服务器已启动，监听端口 " << PORT << std::endl;

    // 5. 使用poll进行I/O多路复用
    std::vector<pollfd> fds;  // 存储pollfd结构体

    pollfd server_pollfd;
    server_pollfd.fd = server_fd;  // 监听socket
    server_pollfd.events = POLLIN; // 监听可读事件
    server_pollfd.revents = 0;     // 清除上一次的revents
    fds.push_back(server_pollfd);

    _running.store(true);
    while(_running){
        // 6. 等待事件
        int activaty = poll(fds.data(), fds.size(), 1000);
        if(activaty < 0) {
            std::cerr << "[ERROR] ";
            perror("Poll Failed");
            continue;
        }
        else if(activaty == 0) {
            continue;
        }

        // 7. 检查所有文件描述符
        for(size_t i = 0; i < fds.size(); ++i){
            if(fds[i].revents & POLLIN){
                // 如果是监听socket有事件发生，表示有新的连接请求
                if(fds[i].fd == server_fd){
                    sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);

                    if(client_fd < 0) {
                        std::cerr << "[ERROR] ";
                        perror("Accept Failed");
                        continue;
                    }

                    // 添加新客户端到poll结构
                    pollfd client_pollfd;
                    client_pollfd.fd = client_fd;
                    client_pollfd.events = POLLIN;
                    client_pollfd.revents = 0;
                    fds.push_back(client_pollfd);

                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
                    std::cout << "[INFO] " << "客户端 " << client_ip << ":" 
                            << ntohs(client_addr.sin_port) << " 已连接" << std::endl;

                    continue;
                }
                else{
                    // 如果是客户端有事件发生，表示有数据可读
                    char buffer[BUFFER_SIZE] = {0};
                    int bytes_read = recv(fds[i].fd, buffer, BUFFER_SIZE, 0);
                    if (bytes_read > 0){
                        buffer[bytes_read] = '\0';
                        std::cout << "[INFO] " << "客户端消息: " << buffer << std::endl;
                        // 发送响应
                        const char* response = "Response from Server";
                        send(fds[i].fd, response, strlen(response), 0);
                        continue;
                    }
                    else if(bytes_read < 0) {
                        std::cerr << "[ERROR] ";
                        perror("Received Failed");
                        continue;
                    }
                    else if(bytes_read == 0) {
                        std::cout << "[INFO] " << "客户端已断开连接" << std::endl;
                        close(fds[i].fd); // 关闭客户端socket
                        fds.erase(fds.begin() + i); // 从poll结构中移除
                        --i; // 重置当前元素，因为下一个元素会移动到当前位置
                        continue;
                    }
                }
            }

            // 处理错误事件
            if(fds[i].revents & POLLERR || fds[i].revents & POLLHUP){
                if(fds[i].fd != server_fd) {
                    std::cerr << "[ERROR] " << "客户端 " << fds[i].fd << " 发生错误或断开连接" << std::endl;
                }
                else {
                    std::cerr << "[ERROR] " << "服务器发生错误或断开连接" << std::endl;
                }
                close(fds[i].fd); // 关闭socket
                fds.erase(fds.begin() + i); // 从poll结构中移除
                --i; // 重置当前元素，因为下一个元素会移动到当前位置
                continue;
            }
        }
    }

    std::cout << "[INFO] " << "服务器关闭中..." << std::endl;

    // 8. 关闭所有socket
    for(auto &fd : fds){
        close(fd.fd);
    }
    std::cout << "[INFO] " << "服务器关闭所有连接" << std::endl;
    std::cout << "[INFO] " << "服务器已关闭" << std::endl;
    return 0;
}