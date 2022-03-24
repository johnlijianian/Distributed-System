//
//  从多个套接字中接收消息
//  本例使用zmq_poll()函数
//
#include "zhelpers.h"
 
int main (void) 
{
    void *context = zmq_init (1);
 
    //  连接任务分发器
    void *receiver = zmq_socket (context, ZMQ_PULL);
    zmq_connect (receiver, "tcp://localhost:5557");
 
    //  连接气象更新服务
    void *subscriber = zmq_socket (context, ZMQ_SUB);
    zmq_connect (subscriber, "tcp://localhost:5556");
    zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "10001 ", 6);
 
    //  初始化轮询对象
    zmq_pollitem_t items [] = {
        { receiver, 0, ZMQ_POLLIN, 0 },
        { subscriber, 0, ZMQ_POLLIN, 0 }
    };
    //  处理来自两个套接字的消息
    while (1) {
        zmq_msg_t message;
        zmq_poll (items, 2, -1);
        if (items [0].revents & ZMQ_POLLIN) {
            zmq_msg_init (&message);
            zmq_recv (receiver, &message, 0);
            //  处理任务
            zmq_msg_close (&message);
        }
        if (items [1].revents & ZMQ_POLLIN) {
            zmq_msg_init (&message);
            zmq_recv (subscriber, &message, 0);
            //  处理气象更新
            zmq_msg_close (&message);
        }
    }
    //  程序不会运行到这儿
    zmq_close (receiver);
    zmq_close (subscriber);
    zmq_term (context);
    return 0;
}