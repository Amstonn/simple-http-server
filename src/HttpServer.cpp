#include "HttpServer.h"

HttpServer::HttpServer(u_short port){
    listen_socket = INVALID_SOCKET;
    this->port = port;
}
/*
处理监听到的一个HTTP请求
@param client_addr : 客户端套接字地址
*/
void HttpServer::accept_request(SOCKET client){
    char buf[1024];
    int numchars;
    char method[255], url[255], path[512];
    size_t i,j;
    struct stat st;
    int cgi = 0; //标识是否为cgi程序
    char * query_string = NULL;
    numchars = get_line(client, buf,sizeof(buf));
    i = 0; j = 0;
    //读取method
    while(!isspace(buf[j]) && (i<sizeof(method) - 1)){
        method[i] = buf[j];
        i++;j++;
    }
    method[i] = '\0';
    //判断是否为可执行的方法 GET/POST
    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        unimplemented(client);
        return;
    }
    if(strcasecmp(method, "POST") == 0){
        cgi = 1;//启动cgi程序标志 POST方法必定调用cgi程序
    }
    i = 0;
    while(isspace(buf[j]) && (j <sizeof(buf))) j++;//跳过空格
    //读取URL
    while(!isspace(buf[j]) && (i < sizeof(url) - 1)  && (j < sizeof(buf))){
        url[i] = buf[j];
        i++;j++;
    }
    url[i] = '\0';

    //处理GET请求
    if(strcasecmp(method, "GET") == 0){
        query_string = url;
        //移动指针查找GET参数
        while((*query_string != '?') && *query_string != '\0') query_string ++;
        if(*query_string == '?'){
            cgi = 1;  //get有参数时调用cgi程序
            *query_string = '\0';
            query_string++;
        }
    }

    //格式化URL到path数组
    sprintf(path, "/home/amston/C++/http-server/htdocs%s", url);
    //默认地址访问index.html
    if(path[strlen(path) - 1] == '/') strcat(path, "index.html");
    //访问请求的文件
    if(stat(path, &st) == -1){
        //如果不存在 读出所有请求头并丢弃
        while((numchars > 0) && strcmp("\n", buf)){
            numchars = get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    }else{
        //如果path为路径 返回默认页面
        if((st.st_mode & S_IFMT) == S_IFDIR){
            strcat(path, "/index.html");
        }
        //如果文件可执行 则根据访问权限执行 
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) cgi = 1;
        if(!cgi) serve_file(client, path);
        else execute_cgi(client, path, method, query_string);
    }
    close(client);
}
//返回客户端一个请求错误 返回状态码为400
void HttpServer::bad_request(int){

}
//读取文件写到套接字
void HttpServer::cat(SOCKET client, FILE *file){
     // 定义缓冲区大小
    const int bufferSize = 1024;
    char buffer[bufferSize];

    // 读取文件内容并写入套接字
    while (true) {
        // 读取文件内容到缓冲区
        size_t bytesRead = fread(buffer, 1, bufferSize, file);
        if (bytesRead == 0) {
            break;
        }
        ssize_t bytesSent = send(client, buffer, bytesRead, 0);
        if (bytesSent == -1) {
            error_die("send in cat");
            return; // 发送失败，退出
        }
    }
}
//处理执行cgi程序时出现的错误
void HttpServer::cannot_execute(int){
    
}
//错误信息写入perror
void HttpServer::error_die(const char *msg){
    printf("Error: %s\n", msg);
}
//运行cgi程序
void HttpServer::execute_cgi(SOCKET client, const char * path, const char *method, const char *query_string){
    char buf[1024];
    int cgi_output[2], cgi_input[2];
    pid_t pid;
    int status,i;
    char c;
    int numchars = 1; //读取的字符数
    int content_length = -1;//HTTP的content_length

    buf[0] = 'A';
    buf[1] = '\0';
    if(strcasecmp(method, "GET") == 0){
        //忽略其余请求请求头的内容
        while((numchars>0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    }else{
        //POST 读取其余信息
        numchars = get_line(client, buf, sizeof(buf));
        while((numchars > 0) && strcmp("\n", buf)){
            buf[15] = '\0';
            if(strcasecmp(buf, "Content-Length:") == 0){
                content_length = atoi(&(buf[16]));
            }
            numchars = get_line(client, buf, sizeof(buf));
        }
        if(content_length == -1){
            bad_request(client);
            return;
        }
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf),0);

    //建立output管道
    if(pipe(cgi_output) < 0){
        cannot_execute(client);
        return;
    }
    //建立input管道
    if(pipe(cgi_input) < 0){
        cannot_execute(client);
        return;
    }
    //创建子线程用于执行cgi 父进程接受数据及发送子进程的回复数据
    if((pid = fork()) < 0){
        cannot_execute(client);
        return;
    }
    if(pid == 0) { //子进程
        char meth_env[255],query_env[255],length_env[255];
        //子进程输出定向到output的管道1端
        dup2(cgi_output[1], 1);
        //子进程输入重定向到input管道的0端
        dup2(cgi_input[0],0);

        //关闭无用管道口
        close(cgi_input[1]);
        close(cgi_output[0]);

        //cgi环境变量
        sprintf(meth_env,"REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if(strcasecmp(method,"GET") == 0){
            sprintf(query_env,"QUERY_STRING=%s",query_string);
            putenv(query_env);
        }
        else{
            sprintf(length_env,"CONTENT_LENGTH=%d",content_length);
            putenv(length_env);
        }
        //替换执行path  使得当前进程执行指定的程序
        execl(path,path,NULL);
        exit(0);
    }else{ //父进程
        //关闭无用的管道
        close(cgi_input[0]);
        close(cgi_output[1]);
        if(strcasecmp(method, "POST") == 0){
            for(i = 0;i<content_length;i++){
                recv(client,&c,1,0);
                write(cgi_output[0],&c,1);//父进程
            }
        }
        //获取子进程处理后的信息，然后send
        while(read(cgi_input[1],&c,1) > 0 ){
            send(client,&c,1,0);
        }
        close(cgi_input[1]);
        close(cgi_output[0]);
        waitpid(pid,&status,0);//等待子进程结束
    }
}
//读取套接字的一行
int HttpServer::get_line(SOCKET sock, char * buf, int size){
    int i=0;
    char c = '\0';
    int n;
    while((i<size-1) && (c!='\n')){
        n = recv(sock,&c,1,0);
        if(n>0){
            if(c == '\r'){
                n = recv(sock,&c,1,MSG_PEEK);//只读取，但是不从缓冲区删除
                if((n>0) && (c=='\n'))
                    recv(sock,&c,1,0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }else
            c = '\n';
    }
    buf[i] = '\0';
    return i;
}
//http响应的头部写到套接字
void HttpServer::headers(SOCKET client, const char *filename){
    char buf[1024];
    (void)filename; //防止未使用的报错信息

    strcpy(buf,"HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf),0);
    strcpy(buf,SERVER_STRING);
    send(client, buf, strlen(buf),0);
    sprintf(buf,"Content-Type:text/html\r\n");
    send(client,buf,strlen(buf),0);
    strcpy(buf,"\r\n");
    send(client,buf,strlen(buf),0);
}
//找不到请求文件时调用  返回客户端信息
void HttpServer::not_found(SOCKET client){
    char buf[1024];

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,SERVER_STRING);
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"Content-Type:text/html\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"<HTML><TITLE>NOT FOUND</TITLE>\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"<BODY><P>The server could not fulfill \r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"your request because the resource specified \r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"is unavailable or nonexistent.</P>\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"</BODY></HTML>\r\n");
    send(client, &buf, strlen(buf),0);
}
//服务器文件返回给浏览器
void HttpServer::serve_file(SOCKET client, const char *filename){
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    buf[0] = 'A';
    buf[1] = '\0';
    while((numchars>0) && strcmp("\n",buf)){
        numchars = get_line(client,buf,sizeof(buf));
    }
    resource = fopen(filename,"r");
    if(resource == NULL){
        not_found(client);
    }else{
        headers(client,filename);
        cat(client,resource);
    }
    fclose(resource);
}
//初始化httpd服务，建立套接字/绑定端口/监听
SOCKET HttpServer::startup(){
    struct sockaddr_in server_addr;
    listen_socket = socket(PF_INET,SOCK_STREAM, 0);
    if(listen_socket == INVALID_SOCKET){
        error_die("socket");
    }else{
        memset(&server_addr, 0 , sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if(bind(listen_socket,(sockaddr*) &server_addr, sizeof(server_addr)) < 0){
            error_die("bind");
        }
        if(listen(listen_socket, 5 ) < 0){
            error_die("listen");
        }
    }
    return listen_socket;
}
//http请求不被支持
void HttpServer::unimplemented(SOCKET client){
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,SERVER_STRING);
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"Content-Type:text/html\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"<HTML><TITLE>Method Not Implemented</TITLE>\r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"<BODY><P> \r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"HTTP request method not supported.</P> \r\n");
    send(client, &buf, strlen(buf),0);
    sprintf(buf,"</BODY></HTML>\r\n");
    send(client, &buf, strlen(buf),0);
}
void HttpServer::close_server(){
    close(listen_socket);
}