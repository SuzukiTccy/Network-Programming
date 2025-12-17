#include <iostream>
#include <cstring>
#include <csignal> // 信号处理
#include <sys/socket.h> // 核心Socket API
#include <netinet/in.h> // Internet地址结构: struct sockaddr_in
#include <arpa/inet.h> // IP地址转换: inet_pton
#include <unistd.h> // POSIX系统服务 close(), read(), write() sleep(), getpid()


// 信号处理：退出进程
volatile sig_atomic_t stop_client = 0;
void sigint_handle(int sig){
    stop_client = 1;
    std::cout << "收到Ctrl+C退出信号, 客户端正在退出..." << std::endl;
}

const char* SERVER_IP = "127.0.0.1";
const int PORT = 8080;
const int BUFFER_SIZE = 1024;

int main(){
    // 1. 创建TCP嵌套字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        perror("Socket creation failed!");
    }
    std::cout << "客户端进程Pid : " << getpid() << std::endl;

    // 2. 设置服务器地址
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 转换IP地址为二进制格式
    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0){
        perror("Invalid address or address not supported!");
        close(sock);
        return -1;
    }

    // 3. 连接服务器
    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        perror("Connection failed");
        close(sock);
        return -1;
    }
    std::cout << "Connected to server " << SERVER_IP << ":" << PORT << std::endl;

    // 注册退出信号
    signal(SIGINT, sigint_handle);
    // 忽略管道破裂信号，针对服务端主动关闭连接的情况，防止程序崩溃
    signal(SIGPIPE, SIG_IGN);

    char buffer[BUFFER_SIZE] = {0};
    std::string message;
    message = "Hello, server!";
    send(sock, message.c_str(), message.size(), 0);
    // 4. 数据交互
    while(!stop_client){
        std::cout << "> ";
        std::getline(std::cin, message);

        // 检查是否应该退出
        if (std::cin.eof() || message == "quit") {
            std::cout << "正在退出..." << std::endl;
            break;
        }
        if(message.empty()) continue;// 检查消息是否为空

        ssize_t send_len = send(sock, message.c_str(), message.size(), 0);
        if(send_len > 0){
            std::cout << "Message send" << std::endl;
        }
        else if (send_len < 0){
            if(errno == EPIPE){
                std::cout << "Send Failed: 服务器已关闭连接" << std::endl;
                break;
            }
            perror("Send Failed");
            continue;
        }
        else if (send_len == 0){
            perror("Connection closed");
            break;
        }
        
        // 清空缓冲区
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t valrecv = recv(sock, buffer, BUFFER_SIZE, 0);
        if(valrecv > 0){
            buffer[valrecv] = '\0';
            std::cout << "Received: " << buffer << std::endl;
        }
        else if(valrecv == 0){
            perror("Connection closed");
            break;
        }
        else if(valrecv < 0){
            if(errno == EINTR) continue;
            perror("Receive Failed");
            continue;
        }
    }

    std::cout << "正在关闭客户端..." << std::endl;
    
    // 5. 关闭连接
    if(sock >= 0){
        // 优雅的关闭：先发FIN，等待处理
        shutdown(sock, SHUT_WR);

        char buf[1024];
        while (recv(sock, buf, sizeof(buf), 0) > 0){}
        close(sock);
        sock = -1;
    }
    std::cout << "客户端已关闭" << std::endl;
    return 0;
}