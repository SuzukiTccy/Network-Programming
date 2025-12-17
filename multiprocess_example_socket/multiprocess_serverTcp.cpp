#include <iostream>            // C++标准输入输出流，用于控制台输入输出
#include <cstring>             // C风格字符串操作函数，如strlen、strcpy等
#include <sys/socket.h>        // 核心Socket API：socket()、bind()、listen()、accept()等
#include <sys/wait.h>          // 进程控制函数：wait()、waitpid()等，用于回收子进程
#include <netinet/in.h>        // 因特网地址族结构：sockaddr_in、INADDR_ANY等常量
#include <arpa/inet.h>         // IP地址转换函数：inet_pton()、inet_ntop()等
#include <unistd.h>            // POSIX系统服务：close()、read()、write()、fork()等
#include <signal.h>            // 信号处理函数：signal()、sigaction()等
#include <sys/ipc.h>           // IPC键值生成和权限控制：ftok()等
#include <sys/shm.h>           // 共享内存操作：shmget()、shmat()、shmdt()等
#include <sys/msg.h>           // 消息队列操作：msgget()、msgsnd()、msgrcv()等


// 信号处理函数：Ctrl+C退出进程
volatile sig_atomic_t stop_server = 0;
std::vector<pid_t> child_pids;
void sigint_handle(int sig){
    stop_server = 1;
    std::cout << "\n收到关闭信号, 正在关闭进程..." << std::endl;
}

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

// 信号处理函数：回收僵尸进程
void sigchld_handler(int sig){
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

// 子进程处理函数
void handle_client(int client_fd, sockaddr_in client_addr){
    // 1. 获取客户端IP
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    std::cout << "Child process PID: " << getpid() << " handle client "
            << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    // 全双工通信：可以同时收发
    char buffer[BUFFER_SIZE];
    fd_set read_fds;

    while(!stop_server){
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);

        // 设置超时，避免拥塞
        struct timeval tv = {1, 0}; // 1秒超时

        int ready = select(client_fd + 1, &read_fds, NULL, NULL, &tv);

        if(ready < 0){
            perror("select error");
            break;
        }
        else if(ready == 0){
            // 超时，继续循环
            continue;
        }

        if (FD_ISSET(client_fd, &read_fds)){
            ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

            if(bytes_read <= 0){
                if(bytes_read == 0){
                    std::cout << "Client disconnected!" << std::endl;
                }
                else{
                    perror("Recv Failed!");
                }
                break;
            }

            buffer[bytes_read] = '\0';
            std::cout << "Child Process " << getpid() << " received:" << buffer << std::endl;
            std::cout << std::endl;
            
            // 发送响应
            const char* response = "Message received by child process";
            send(client_fd, response, sizeof(response), 0);
        }
    }

    close(client_fd);
    std::cout << "Child process " << getpid() << " exiting" << std::endl;
    exit(0);
}

int main(){
    // 注册信号处理，退出进程
    signal(SIGINT, sigint_handle);

    // 1. 创建socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1){
        perror("Socket Create Failed!");
        return -1;
    }
    std::cout << "Create Socket success!" << std::endl;
    std::cout << "Pid : " << getpid() << std::endl;

    // 2. 设置地址重用
    int opt = 1;
    if ((setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)){
        perror("Setsockopt Failed!");
        close(server_fd);
        return -1;
    }

    // 3. 绑定地址
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
        perror("Bind failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Bind the PORT" << PORT << std::endl;

    // 4. 监听
    if(listen(server_fd, 10) < 0){
        perror("Listen failed!");
        close(server_fd);
        return -1;
    }
    std::cout << "Server PID: " << getpid() << " listening on port " << PORT << std::endl;

    // 5. 设置信号处理，避免僵尸进程
    struct sigaction sa;
    sa.__sigaction_u.__sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask); // 清空sa_mask
    sigaddset(&sa.sa_mask, SIGINT);  // 额外阻塞 SIGINT, 为了能在Ctrl+C时，也可以正常处理回收子进程
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("sigaction()");
        close(server_fd);
        return -1;
    }

    // 设置监听socket I/O复用，避免阻塞主循环
    fd_set read_fd;

    // 5. 主循环
    while(!stop_server){

        // 设置监听socket I/O复用，避免阻塞主循环
        FD_ZERO(&read_fd);
        FD_SET(server_fd, &read_fd);
        struct timeval tv = {1, 0}; // 1秒超时
        int ready = select(server_fd + 1, &read_fd, NULL, NULL, &tv);
        if(ready < 0){
            if(!stop_server) perror("Select Failed!");
            else if(stop_server) break; // 避免select失败后，陷入死循环
            continue;
        }
        else if(ready == 0){
            // 超时，继续循环
            continue;
        }

        if(!FD_ISSET(server_fd, &read_fd)){
            perror("FD_ISSET Failed!");
            if(stop_server) break; // 避免FD_ISSET失败后，陷入死循环
            continue;
        }

        // 5.1 连接客户端
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if(client_fd < 0){
            if(errno == EINTR && stop_server == 0) continue; // 被信号中断
            perror("Accept Failed!");
            continue;
        }

        // 5.2 创建子进程
        pid_t pid = fork(); // 在子进程中，fork()会返回0
        if(pid < 0){ // 子进程创建失败
            perror("Fork Failed!");
            continue;
        }
        else if (pid == 0){ // 子进程
            close(server_fd); // 子进程不需要监听socket
            
            // 处理客户端
            handle_client(client_fd, client_addr);
            // 不会执行到这里，因为handle_client里面会exit退出子进程

        }else { // 父进程
            child_pids.push_back(pid);
            close(client_fd); // 父进程不需要客户端socket
            std::cout << "Created child process " << pid << std::endl;
        }
    }

    std::cout << "正在关闭服务器..." << std::endl;
    std::cout << "等待 " << child_pids.size() << " 个子进程关闭" << std::endl;

    // 6. 关闭监听socket
    close(server_fd);
    std::cout << "Close server socket success" << std::endl;

    // 等待所有子进程结束
    for(pid_t pid : child_pids){
        int status;
        waitpid(pid, &status, 0);
        std::cout << "子进程: " << getpid() << " 已结束" << std::endl;
    }

    std::cout << "服务器关闭" << std::endl;
    return 0;
}