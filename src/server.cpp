#include "HttpServer.h"

int main(){
    u_short port = 8888;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    HttpServer * server = new HttpServer(port);
    SOCKET client,server_sock;
    server_sock = server->startup();
    printf("Httpd running on port %d\n", port);
    while(1){
        client = accept(server_sock,(sockaddr *)&client_addr,&client_addr_len);
        if(client == -1){
            server->error_die("accept");
        }
        server->accept_request(client);
    }
    server->close_server();
    return 0;
}