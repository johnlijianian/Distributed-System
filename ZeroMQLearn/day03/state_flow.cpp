#include <iostream>
#include <zmq.hpp>
#include "zhelpers.hpp"
#include <string>
using namespace std;

#define NBR_CLIENTS 10
#define NBR_WORKERS 3
#define WORKER_READY  "READY"      //  Signals worker is ready

int main(int argc, char *argv []){
    //  第一个参数是代理的名称
    //  其他参数是同伴代理的名称
    //
    if (argc < 2) {
        printf ("syntax: peering3 me {you}...\n");
        exit (EXIT_FAILURE);
    }
    char* self = argv[1];

    self = argv [1];
    printf ("I: 正在准备代理程序 %s...\n", self);
    srandom ((unsigned) time (NULL));

    //  准备上下文和套接字
    zmq::context_t context(1);
    zmq::socket_t statebe(context, ZMQ_PUB); //be->publish

    char* self_url;
    sprintf(self_url, "ipc://%s-state.ipc", self);
    statebe.bind(self_url);

    //  连接statefe套接字至所有同伴 [连接所有的Desc]
    zmq::socket_t statefe (context, ZMQ_SUB); // fe->Subscribe
    for (int argn = 2; argn < argc; argn ++){
        char *peer = argv [argn];
        printf ("I: 正在连接至同伴代理 '%s' 的状态流后端\n", peer);
        char* peer_url;
        sprintf(peer_url, "ipc://%s-state.ipc", peer);
        statefe.connect(peer_url);
    }

    std::cout << "完成" << std::endl;

    statefe.setsockopt(ZMQ_SUBSCRIBE, NULL, 0); //订阅所有信息

    //  发送并接受状态消息
    //  zmq_poll()函数使用的超时时间即心跳时间
    //
    const std::chrono::milliseconds timeout{1000};
    while(1) {
        zmq::pollitem_t items[] = {
            { statefe, 0, ZMQ_POLLIN, 0 }
        };
        int rc = zmq::poll(items, 1, timeout);

        if (rc == -1)
            break;              //  中断
        if(items[0].revents & ZMQ_POLLIN) {
            zmq::message_t message;
            int more;
            while(1) {
                statefe.recv(&message);
                size_t more_size = sizeof (more);
                statefe.getsockopt(ZMQ_RCVMORE, &more, &more_size);

                std::cout << message.str() << std::endl;

                if (!more)
                    break;  // 已到达最后一帧
            }
        } else {
            //  Send message to all subscribers
            std::cout << self << " Sending" << std::endl;
            int rc;
            zmq::message_t message(strlen(self));
            snprintf ((char *) message.data(), strlen (self), "%s", self);
            statebe.send(message,ZMQ_SNDMORE);

            
            char *num;
            sprintf(num, "%d", 10);
            message.rebuild(strlen(num));
            snprintf((char *) message.data(), strlen (num), "%s", num);
            statebe.send(message,0);
        }
    }



}