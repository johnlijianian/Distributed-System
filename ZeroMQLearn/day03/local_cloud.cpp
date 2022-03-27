#include <iostream>
#include <zmq.hpp>
#include "zhelpers.hpp"
#include <string>
#include <thread.h>

using namespace std;

#define NBR_CLIENTS 10
#define NBR_WORKERS 3
#define LRU_READY   "\001"      //  消息：worker已就绪

char* self;

//  请求-应答客户端使用REQ套接字
void client_task() {
    //  准备上下文和套接字
    zmq::context_t context(1);
    zmq::socket_t client(context, ZMQ_REQ);

    char *url;
    sprintf(url, "ipc://%s-localfe.ipc", self);
    client.connect(url);

    while(1) {
        s_send(client, "HELLO");
        char *reply = s_recv();
        if(!reply)
            break;
        printf ("Client: %s\n", reply);
        free (reply);
        sleep (1);
    }
    // 删除client socket
}

void worker_task() {
    //  准备上下文和套接字
    zmq::context_t context(1);
    zmq::socket_t worker(context, ZMQ_REQ);

    char *url;
    sprintf(url, "ipc://%s-localbe.ipc", self);
    client.connect(url);

    //  告知代理worker已就绪
    s_send(worker, LRU_READY);

    while(1) {
        char *address = s_recv(worker);
        {
            char *empty = s_recv (worker);
            assert(empty == 0);
        }
        
        //  Get request, send reply
        string request = s_recv(worker);
        cout << "Worker: " << request << endl;

        s_sendmore(worker, address);
        s_sendmore(worker, "");
        s_send(worker, "OK");
    }
}

int main(int argc, char *argv[]){
    //  第一个参数是代理的名称
    //  其他参数是同伴代理的名称
    //
    if (argc < 2) {
        printf ("syntax: peering3 me {you}...\n");
        exit (EXIT_FAILURE);
    }
    self = argv[1];

    printf ("I: 正在准备代理程序 %s...\n", self);
    srandom ((unsigned) time (NULL));

    //  准备上下文和套接字
    zmq::context_t context(1);

    zmq::socket_t cloudfe(context, ZMQ_ROUTER); // fe收数据
    char* self_url;
    sprintf(self_url, "ipc://%s-cloud.ipc", self);
    cloudfe.setsockopt(ZMQ_IDENTITY, self, strlen(self));
    cloudfe.bind(self_url);

    //  将cloudbe连接至同伴代理的端点
    zmq::socket_t cloudbe(context, ZMQ_ROUTER); 
    zmq_setsocketopt(cloudbe, ZMQ_IDENTITY, self, strlen(self));

    for (int argn = 2; argn < argc; argn ++){
        char *peer = argv [argn];
        printf ("I: 正在连接至同伴代理 '%s' 的状态流后端\n", peer);
        char* peer_url;
        sprintf(peer_url, "ipc://%s-cloud.ipc", peer);
        cloudbe.connect(peer_url);
    }

    //  准备本地前端和后端
    zmq::socket_t localfe(context, ZMQ_ROUTER);
    char *localfe_url;
    sprintf(localfe_url, "ipc://%s-localfe.ipc", self);
    localfe.bind(localfe_url);

    zmq::socket_t localbe(context, ZMQ_ROUTER);
    char *localbe_url;
    sprintf(localbe_url, "ipc://%s-localbe.ipc", self);
    localbe.bind(localbe_url);

    //  让用户告诉我们何时开始
    printf ("请确认所有代理已经启动，按任意键继续: ");
    getchar ();

    // 启动本地worker
    for(int worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++){
        std::thread t(worker_task);
        t.join();
    }

    // 启动本地client
    for(int client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++){
        std::thread t(client_task);
        t.join();
    }

    //  有趣的部分
    //  -------------------------------------------------------------
    //  请求-应答消息流
    //  - 若本地有可用worker，则轮询获取本地或云端的请求；
    //  - 将请求路由给本地worker或其他集群。

    //  可用worker队列
    std::queue<std::string> worker_queue;
    const std::chrono::milliseconds timeout{1000};

    while(1) {
        //  Initialize poll set
        zmq::pollitem_t items[] = {
                //  本地请求
                { localfe, 0, ZMQ_POLLIN, 0 },
                //  云端请求
                { cloudfe, 0, ZMQ_POLLIN, 0 }
        };

        int rc = zmq::poll(items, 2, -1);
        assert(rc>=0)

        int reroutable = 0;
        //  优先处理同伴代理的请求，避免资源耗尽
        if (frontends [1].revents & ZMQ_POLLIN) {

            worker_queue.push(s_recv (cloudfe));

            {
                //  Second frame is empty
                std::string empty = s_recv(cloudfe);
                assert(empty.size() == 0);
            }

            //  Third frame is READY or else a client reply address
            std::string cloud_addr = s_recv(cloudfe);
        } else { 
            // 本地请求
            if (frontends [0].revents & ZMQ_POLLIN) {
                msg = s_recv (localfe);
                reroutable = 1;
            } else { // 无请求
                break;
            }
        }

        if(reroutable && argc > 2 && randof (5) == 0) {
            //  随地地路由给同伴代理
            int random_peer = randof (argc - 2) + 2;
        }


        
    }




}