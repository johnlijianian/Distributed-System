//
//  管道模式 - worker 设计2
//  添加发布-订阅消息流，用以接收自杀消息
//
#include "zhelpers.hpp"
 
int main (void) 
{
    void *context = zmq_init (1);
 
    //  用于接收消息的套接字
    void *receiver = zmq_socket (context, ZMQ_PULL);
    zmq_connect (receiver, "tcp://localhost:5557");
 
    //  用户发送消息的套接字
    void *sender = zmq_socket (context, ZMQ_PUSH);
    zmq_connect (sender, "tcp://localhost:5558");
 
    //  用户接收控制消息的套接字 (kill指令)
    void *controller = zmq_socket (context, ZMQ_SUB);
    zmq_connect (controller, "tcp://localhost:5559");
    zmq_setsockopt (controller, ZMQ_SUBSCRIBE, "", 0);
 
    //  处理接收到的任务或控制消息
    zmq_pollitem_t items [] = {
        { receiver, 0, ZMQ_POLLIN, 0 },
        { controller, 0, ZMQ_POLLIN, 0 }
    };
    //  处理消息
    while (1) {
        zmq_msg_t message;
        zmq_poll (items, 2, -1);
        if (items [0].revents & ZMQ_POLLIN) {
            zmq_msg_init (&message);
            zmq_recv (receiver, &message, 0);
 
            //  工作
            s_sleep (atoi ((char *) zmq_msg_data (&message)));
 
            //  发送结果
            zmq_msg_init (&message);
            zmq_send (sender, &message, 0);
 
            //  简单的任务进图指示
            printf (".");
            fflush (stdout);
 
            zmq_msg_close (&message);
        }
        //  任何控制命令都表示自杀
        if (items [1].revents & ZMQ_POLLIN)
            break;                      //  退出循环
    }
    //  结束程序
    zmq_close (receiver);
    zmq_close (sender);
    zmq_close (controller);
    zmq_term (context);
    return 0;
}