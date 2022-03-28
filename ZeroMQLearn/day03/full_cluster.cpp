#include <iostream>
#include <zmq.hpp>
#include "zhelpers.hpp"
#include <string>
#include <thread.h>

using namespace std;

#define NBR_CLIENTS 10
#define NBR_WORKERS 3
#define LRU_READY   "\001"      //  消息：worker已就绪

using namespace std;

static char *self;

// client
void client_task() {
    //  准备上下文和套接字
    zmq::context_t context(1);

    zmq::socket_t client(context, ZMQ_REQ);
    char *client_url;
    sprintf(client_url, "ipc://%s-localfe.ipc", self);
    client.connect(client_url);

    zmq::socket_t monitor(context, ZMQ_PUSH);
    char *monitor_url;
    sprintf(monitor_url, "ipc://%s-monitor.ipc", self);
    monitor.connect(monitor_url);

    while(1){
        sleep (randof (5));
        int burst = randof (15);

        while (burst--) {
            char task_id [5];
            sprintf (task_id, "%04X", randof (0x10000));

            //  使用随机的十六进制ID来标注任务
            zstr_send (client, task_id); // 发送至localfe [uuid/ /task_id]

            //  最多等待10秒
            zmq::pollitem_t pollset [] = { { client, 0, ZMQ_POLLIN, 0 } };
            int rc = zmq::poll (pollset, 1, 10 * 1000 * ZMQ_POLL_MSEC);

            if (rc == -1)
                break;          //  中断

            if (pollset [0].revents & ZMQ_POLLIN) {
                char *reply = zstr_recv (client);
                if (!reply)
                    break;              //  中断
                //  worker的应答中应包含任务ID
                puts (reply);
                assert (streq (reply, task_id));
                free (reply);
            } else {
                char *send_str;
                sprintf(send_str, "E: 客户端退出，丢失的任务为： %s", task_id);
            }
        }
    }
}

// worker
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
            char *empty = s_recv(worker);
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
    if (argc < 2){
        printf ("syntax: peering3 me {you}...\n");
        exit (EXIT_FAILURE);
    }

    self = argv [1];
    printf ("I: 正在准备代理程序 %s...\n", self);
    srandom ((unsigned) time (NULL));


    //  准备上下文和套接字
    zmq::context_t context(1);

    // 绑定cloudfe
    zmq::socket_t cloudfe(context, ZMQ_ROUTER); 
    char* self_url;
    sprintf(self_url, "ipc://%s-cloud.ipc", self);
    cloudfe.setsockopt(ZMQ_IDENTITY, self, strlen(self));
    cloudfe.bind(self_url);

    // 绑定statebe
    zmq::socket_t statebe(context, ZMQ_PUB); //be->publish
    char* self_url;
    sprintf(self_url, "ipc://%s-state.ipc", self);
    statebe.bind(self_url);

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

    // 准备本地前端
    zmq::socket_t localfe(context, ZMQ_ROUTER);
    char *localfe_url;
    sprintf(localfe_url, "ipc://%s-localfe.ipc", self);
    localfe.setsockopt(ZMQ_IDENTITY, self, strlen(self)); // 设置接收的消息
    localfe.bind(localfe_url);

    // 准备本地后端
    zmq::socket_t localbe(context, ZMQ_ROUTER);
    char *localbe_url;
    sprintf(localbe_url, "ipc://%s-localbe.ipc", self);
    localbe.bind(localbe_url);

    // 准备监控套接字，接收client信息
    zmq::socket_t monitor(context, ZMQ_PULL);
    char *monitor_url;
    sprintf(monitor_url, "ipc://%s-monitor.ipc", self);
    monitor.bind(monitor_url);

    int worker_nbr;
    for (worker_nbr = 0; worker_nbr < NBR_WORKERS; worker_nbr++){
        std::thread t(worker_task);
        t.join();
    }

    int client_nbr;
    for (client_nbr = 0; client_nbr < NBR_CLIENTS; client_nbr++){
        std::thread t(client_task);
        t.join();
    }

      //  有趣的部分
    //  -------------------------------------------------------------
    //  发布-订阅消息流
    //  - 轮询同伴代理的状态信息；
    //  - 当自身状态改变时，对外广播消息。
    //  请求-应答消息流
    //  - 若本地有可用worker，则轮询获取本地或云端的请求；
    //  - 将请求路由给本地worker或其他集群。

    std::queue<std::string> local_queue;
    std::queue<std::string> cloud_queue;

    const std::chrono::milliseconds timeout{1000 * 10};

    while (1){
        zmq::pollitem_t primary [] = {
            { localbe, 0, ZMQ_POLLIN, 0 },
            { cloudbe, 0, ZMQ_POLLIN, 0 },
            { statefe, 0, ZMQ_POLLIN, 0 },
            { monitor, 0, ZMQ_POLLIN, 0 }
        };

        // 判断是否有worker
        int rc = zmq::poll(primary, 4, -1); 

        if(rc == -1)
            break;

        //  跟踪自身状态信息是否改变
        int previous = local_capacity;

        // 本地worker的应答
        if(primary[0].revents & ZMQ_POLLIN) {
            local_queue.push(s_recv(localbe));
            {
                //  Second frame is empty
                std::string empty = s_recv(localbe);
                assert(empty.size() == 0);
            }
            //  Third frame is READY or else a client reply address
            std::string client_addr = s_recv(localbe);

            if (client_addr.compare("READY") != 0) {
                {
                    std::string empty = s_recv(localbe);
                    assert(empty.size() == 0);
                }
                
                std::string reply = s_recv(localbe); // 等待worker发送处理完成的消息
                s_sendmore(localfe, client_addr); 
                s_sendmore(localfe, "");
                s_send(localfe, reply); // 将worker的信息发送出去

                // if (--client_nbr == 0)
                //     break;
            }
        } 
        //  处理来自同伴代理的应答
        else if(primary[1].revents & ZMQ_POLLIN) {

        }
        // 处理同伴代理的状态更新
        else if(primary[2].revents & ZMQ_POLLIN) {

        }
        //  处理监控消息
        else if(primary[3].revents & ZMQ_POLLIN) {

        }

        //  开始处理客户端请求
        //  - 如果本地有空闲worker，则总本地client和云端接收请求；
        //  - 如果我们只有空闲的同伴代理，则只轮询本地client的请求；
        //  - 将请求路由给本地worker，或者同伴代理。
        //
        while(local_queue.size() + cloud_queue.size()){
            zmq::pollitem_t secondary [] = {
                { localfe, 0, ZMQ_POLLIN, 0 }, // 本端请求
                { cloudfe, 0, ZMQ_POLLIN, 0 }  // 云端请求
            };
            int rc;
            if (local_queue.size()){ // 如果本端仍有worker
                rc = zmq::poll(secondary, 2, -1);
            } else { // 本地无worker，但是云端有worker，则只接收本地的请求，需要将其发送到云端
                rc = zmq::poll(secondary, 1, 0);
            }

            assert(rc >= 0);

            std::string request;
            if(secondary [0].revents & ZMQ_POLLIN){
                std::string client_addr = s_recv(localfe);
                {
                    std::string empty = s_recv(localfe);
                    assert(empty.size() == 0);
                }
                request = s_recv(localfe); // 本地客户端发送的消息
            } else if (secondary [1].revents & ZMQ_POLLIN){
                
            } else {
                break; // 无任务
            }

            if (local_queue.size()){
                std::string worker_addr = local_queue.front();//local_queue [0];
                worker_queue.pop();

                s_sendmore(localbe, worker_addr);
                s_sendmore(localbe, "");
                s_sendmore(localbe, client_addr);
                s_sendmore(localbe, "");
                s_send(localbe, request);
            }
            else {
                //  随机路由给同伴代理
                int random_peer = randof (argc - 2) + 2;
                // 构造消息
                char *peer = argv[random_peer];

                s_sendmore(cloudbe, peer);
                s_sendmore(cloudbe, "");
                s_send(cloudbe, request);
            }
        }

        if (local_capacity != previous){
            // 更新状态
        }


    }
    
}