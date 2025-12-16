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
    // 1. 创建UDP嵌套字
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1){
        perror("Socket Creation Failed!");
        return -1;
    }
    std::cout << "PID: " << getpid() << std::endl;

    // 2. 设置服务器地址
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if ((inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr.s_addr)) <= 0 ){
        perror("Invalid address or address not supported");
        close(sock);
        return -1;
    }

    // 3. 数据交互
    // 发送数据
    const char* message = "Hello from UDP client";
    sendto(sock, message, strlen(message), 0,
            (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    std::cout << "Message sent to " << SERVER_IP << ":" << PORT << std::endl;

    // 接收响应
    char buffer[BUFFER_SIZE] = {0};
    sockaddr_in address;
    address.sin_family = AF_INET;
    int address_len = sizeof(address);

    ssize_t len = recvfrom(sock, &buffer, BUFFER_SIZE, 0,
                        (struct sockaddr*)&address, (socklen_t*)&address_len);
    if(len < 0){
        perror("Received Failed!");
        close(sock);
        return -1;
    }
    buffer[len] = '\0';

    char SERVER_IP[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &address.sin_addr, SERVER_IP, INET_ADDRSTRLEN)){
        perror("inet_ntop error!");
        close(sock);
        return -1;
    }
    std::cout << "Received from " << SERVER_IP << ":" << ntohs(address.sin_port) << " : "
                << buffer << std::endl;
    
    // 4. 关闭连接
    close(sock);
    return 0;
}