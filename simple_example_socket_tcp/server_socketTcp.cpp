#include <iostream>
#include <cstring>
#include <sys/socket.h> // 核心Socket API
#include <netinet/in.h> // Internet地址结构: struct sockaddr_in
#include <arpa/inet.h> // IP地址转换: inet_pton
#include <unistd.h> // POSIX系统服务 close(), read(), write() sleep(), getpid()


const int PORT = 8080; // 端口号
const int BUFFER_SIZE = 1024; // 缓冲区大小

int main(){
    // 1. 创建TCP嵌套字
    int server_fd = socket(
        AF_INET, // 协议族: IPv4
        SOCK_STREAM, // 套接字类型: SOCK_STREAM
        0 // 具体协议, 0表示根据前两个参数自动设置，SOCK_STREAM对应TCP
    );
    if(server_fd == -1){
        // std::cerr << "Socket creation failed!" << strerror(errno) << std::endl;
        perror("Socket creation failed!"); // 自动包含errno描述
        return -1;
    }
    std::cout << "Create Socket success!" << std::endl;
    std::cout << "Pid : " << getpid() << std::endl;

    // 2. 设置地址重用，避免端口占用问题
    int opt = 1;
    if(setsockopt(
        server_fd, // 需要配置的嵌套字
        SOL_SOCKET, // 选项级别：SOL_SOCKET(嵌套字通用层)
        SO_REUSEADDR, // 选项名：允许重用本地地址
        &opt, // 指向选项值的指针
        sizeof(opt) // 指向选项值的长度
        )){
        perror("Setsockopt failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Setsockopt success!" << std::endl;

    // 3. 绑定地址和端口
    sockaddr_in address; // IPv4地址容器
    address.sin_family = AF_INET; // 指定地址族为IPv4
    address.sin_addr.s_addr = INADDR_ANY; // 监听所有的网络接口
    address.sin_port = htons(PORT); // 转化为网络字节序

    if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
        // std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        perror("Bind failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Bind the PORT :  " << PORT << std::endl; 

    // 4. 监听连接请求 (已完成连接队列的最大长度为3)
    if(listen(server_fd, 3) < 0){
        // std::cerr << "Listen failed: " << strerror(errno) << std::endl;
        perror("Listen failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Server listening on port " << PORT << std::endl;

    // 5. 接受客户端连接
    int new_socket; // 新的嵌套字
    sockaddr_in client_addr; // 客户端IPv4容器
    int client_addr_len = sizeof(client_addr);
    while((new_socket =                     // 赋值表达式的值就是赋值后左操作数的值
        accept(                             // 从已完成队列中取出一个已建立的连接
            server_fd,                      // 监听嵌套字
            (struct sockaddr*)&client_addr, // 客户端地址信息
            (socklen_t*)&client_addr_len    // 客户端地址长度
        )) < 0){
        if(errno == EINTR) continue;  // 被信号中断，重试
        // std::cerr << "Accept failed: " << strerror(errno) << std::endl;
        perror("Accept failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Accept success!" << std::endl;

    char client_ip[INET_ADDRSTRLEN];
    // 将网络字节序的二进制IP地址转换为点分十进制字符串
    inet_ntop(
        AF_INET,                   // 地址族：IPv4
        &client_addr.sin_addr,     // 来源：accept()填充的客户端地址
        client_ip,                 // 目标：存储字符串的缓冲区
        INET_ADDRSTRLEN);          // 缓冲区长度（16字节足以存放"255.255.255.255"）

    // 将网络字节序的端口号转换为主机字节序并输出
    std::cout << "Accepted connection from " << client_ip << ":"
    << ntohs(client_addr.sin_port) << std::endl;

    
    // 6. 数据交互
    char buffer[BUFFER_SIZE] = {0};
    ssize_t valrecv;
    if((valrecv = recv(new_socket, buffer, BUFFER_SIZE, 0)) < 0){
        perror("Data Received Fail!");
        close(new_socket);
        close(server_fd);
        return -1;
    }
    if (valrecv > 0){
        buffer[valrecv] = '\0'; // 手动添加字符终止符
        std::cout << "Received: " << buffer << std::endl;
    }

    const char *response = "Hello from TCP server";
    if(send(new_socket, response, strlen(response), 0) < 0){
        perror("Response send Fail!");
        close(new_socket);
        close(server_fd);
        return -1;
    }
    std::cout << "Response send" << std::endl;

    // 7. 关闭连接
    // 改进：错误判断
    while(close(new_socket) == -1 && errno != EINTR){
        perror("Close new_socket failed (non-fatal)");
    }
    while(close(server_fd) == -1 && errno != EINTR){
        perror("Close server_fd failed (non-fatal)");
    }

    return 0;
}