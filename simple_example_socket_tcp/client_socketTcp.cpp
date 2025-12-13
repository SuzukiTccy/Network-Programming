#include <iostream>
#include <cstring>
#include <sys/socket.h> // 核心Socket API
#include <netinet/in.h> // Internet地址结构: struct sockaddr_in
#include <arpa/inet.h> // IP地址转换: inet_pton
#include <unistd.h> // POSIX系统服务 close(), read(), write() sleep(), getpid()


const char* SERVER_IP = "127.0.0.1";
const int PORT = 8080;
const int BUFFER_SIZE = 1024;

int main(){
    // 1. 创建TCP嵌套字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        perror("Socket creation failed!");
    }
    std::cout << "Pid : " << getpid() << std::endl;

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

    // 4. 数据交互
    const char* message = "Hello from TCP client";
    send(sock, message, strlen(message), 0);
    std::cout << "Message send" << std::endl;

    char buffer[BUFFER_SIZE] = {0};
    ssize_t valrecv = recv(sock, buffer, BUFFER_SIZE, 0);
    if(valrecv > 0){
        buffer[valrecv] = '\0';
        std::cout << "Received: " << buffer << std::endl;
    }
    
    // 5. 关闭连接
    if(sock >= 0){
        // 优雅的关闭：先发FIN，等待处理
        shutdown(sock, SHUT_WR);

        char buf[1024];
        while (recv(sock, buf, sizeof(buf), 0) > 0){}
        close(sock);
        sock = -1;
    }



    return 0;
}