#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <wait.h>
#include <string.h>
#include <sys/stat.h>
using namespace std;
#define SOCKET int
#define INVALID_SOCKET -1
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

class HttpServer{
public:
    HttpServer(u_short);
    void accept_request(SOCKET);//处理监听到的一个HTTP请求
    void bad_request(int);//返回客户端一个请求错误 返回状态码为400
    void cat(int, FILE *);//读取文件写到套接字
    void cannot_execute(int);//处理执行cgi程序时出现的错误
    void error_die(const char *);//错误信息写入perror
    void execute_cgi(SOCKET, const char *,const char *, const char *);//运行cgi程序
    int get_line(SOCKET, char *, int);//读取套接字的一行
    void headers(SOCKET, const char *);//http响应的头部写到套接字
    void not_found(SOCKET);//找不到请求文件时调用
    void serve_file(SOCKET, const char *);//服务器文件返回给浏览器
    SOCKET startup();//初始化httpd服务，建立套接字/绑定端口/监听
    void unimplemented(SOCKET);//http请求不被支持
    void close_server();

private:
    SOCKET listen_socket;//监听套接字
    u_short port;//服务端口
};
#endif