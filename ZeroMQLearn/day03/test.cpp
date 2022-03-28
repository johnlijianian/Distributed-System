//  Least-recently used (LRU) queue device
//  Clients and workers are shown here in-process
//
#include <iostream>
#include <zmq.hpp>
#include "zhelpers.hpp"
#include <string>
using namespace std;

int main(int argc, char *argv[]){

    zmq::context_t new_context(1);
    zmq::socket_t worker(new_context, ZMQ_ROUTER);
    worker.setsockopt(ZMQ_IDENTITY, "worker1", 7);
    worker.bind("ipc://frontend.ipc");

    zmq::socket_t client(new_context, ZMQ_ROUTER);
    client.setsockopt(ZMQ_IDENTITY, "worker2", 7);
    client.connect("ipc://frontend.ipc");

    sleep(1);

    s_sendmore(worker, "worker1");
    s_sendmore(worker,"");
    s_send(worker,"hello");

    // s_send(worker, "hello");

    // std::string identity = s_recv(client);
    // s_recv(client);     //  Envelope delimiter
    // s_recv(client);     //  Response from worker  

    // std::cout << identity << std::endl;  

    // s_sendmore(client, "worker1");
    // s_sendmore(client, "");
    // s_send(client,"hello2");

    s_dump(client);

    return 0;
}
