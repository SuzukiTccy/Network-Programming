#include <iostream>
#include <cstring>
#include <sys/socket.h> // 核心Socket API
#include <netinet/in.h> // Internet地址结构: struct sockaddr_in
#include <arpa/inet.h> // IP地址转换: inet_pton
#include <unistd.h> // POSIX系统服务 close(), read(), write() sleep(), getpid()


const int PORT = 8080;
const int BUFFER_SIZE = 1024;


int main(){
    // 1. 创建UDP嵌套字
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd == -1){
        perror("Socket creation failed!");
        return -1;
    }

    // 2. 设置地址重用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))){
        perror("Setsockopt failed!");
        close(server_fd);
        return -1;
    }

    // 3. 绑定地址和端口
    sockaddr_in address; // IPv4地址容器
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
        perror("Bind Failed!");
        close(server_fd);
        return -1;
    }

    std::cout << "PID:" << getpid() << std::endl;
    std::cout << "UDP server listening port:" << PORT << " ..." << std::endl;

    // 4. 数据交互
    char buffer[BUFFER_SIZE] = {0};
    sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    while(true){
        // 缓冲区清空
        memset(buffer, 0, BUFFER_SIZE);

        // 接收客户端消息
        ssize_t len = recvfrom(server_fd, buffer, BUFFER_SIZE, 0,
                            (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        if (len < 0){
            perror("Recefrom failed!");
            continue;
        }
        std::cout << "len = " << len << std::endl;
        buffer[len] = '\0'; // 添加字符串结束符

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Received from " << client_ip << ":" << ntohs(client_addr.sin_port) 
                << " : " << buffer << std::endl;

        // 发送响应
        const char* response = "Hello from UDP server";
        sendto(server_fd, response, strlen(response), 0,
                (struct sockaddr*)&client_addr, client_addr_len);
    }

    // 5. 关闭连接
    // 注意：UDP服务器通常不会主动关闭，此处为示例完整性添加
    close(server_fd);

    return 0;
}