    // 忽略管道破裂信号，针对服务端主动关闭连接的情况，防止程序崩溃
    signal(SIGPIPE, SIG_IGN);