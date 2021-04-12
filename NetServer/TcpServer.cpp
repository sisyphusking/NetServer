

#include "TcpServer.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>

void Setnonblocking(int fd);

TcpServer::TcpServer(EventLoop* loop, const int port, const int threadnum)
    : serversocket_(),
    loop_(loop),
    serverchannel_(),
    conncount_(0),
    //将loop传递进来了
    eventloopthreadpool(loop, threadnum)
{
    //serversocket_.SetSocketOption(); 
    
    serversocket_.SetReuseAddr();   
    //将主线程socket绑定
    serversocket_.BindAddress(port);
    serversocket_.Listen();
    //设置非阻塞，这个很重要
    serversocket_.Setnonblocking();

    //服务器事件
    serverchannel_.SetFd(serversocket_.fd());
    serverchannel_.SetReadHandle(std::bind(&TcpServer::OnNewConnection, this));
    serverchannel_.SetErrorHandle(std::bind(&TcpServer::OnConnectionError, this));
    
}

TcpServer::~TcpServer()
{

}

void TcpServer::Start()
{   
    //启动事件处理线程池
    eventloopthreadpool.Start();

    //主通道的时间
    serverchannel_.SetEvents(EPOLLIN | EPOLLET);
    //将主线程加入到poller中
    loop_->AddChannelToPoller(&serverchannel_);
}

//新TCP连接处理，核心功能，业务功能注册，任务分发
void TcpServer::OnNewConnection()
{
    //循环调用accept，获取所有的建立好连接的客户端fd
    struct sockaddr_in clientaddr;
    int clientfd;
    //从监听队列中获取一个客户端连接
    while( (clientfd = serversocket_.Accept(clientaddr)) > 0) 
    {
        std::cout << "New client from IP:" << inet_ntoa(clientaddr.sin_addr) 
            << ":" << ntohs(clientaddr.sin_port) << std::endl;
        
        if(++conncount_ >= MAXCONNECTION)
        {
            close(clientfd);
            continue;
        }
        Setnonblocking(clientfd);

        //选择IO线程loop
        //当不设置多线程时，这里是主线程
        EventLoop *loop = eventloopthreadpool.GetNextLoop();

        //创建连接，注册业务函数
        //make_shared 在动态内存中分配一个对象并初始化它，返回指向此对象的shared_ptr，与智能指针一样
        //这个地方关注洗tcpconnection的初始化逻辑
        //将每一个fd和对应的channel绑定起来
        std::shared_ptr<TcpConnection> sptcpconnection = std::make_shared<TcpConnection>(loop, clientfd, clientaddr);

        //message的处理逻辑是在这里开始设置的，可以看Httpserver的初始化代码
        //这里用了闭包？  先声明后赋值？
        sptcpconnection->SetMessaeCallback(messagecallback_); //
        sptcpconnection->SetSendCompleteCallback(sendcompletecallback_);
        sptcpconnection->SetCloseCallback(closecallback_);
        sptcpconnection->SetErrorCallback(errorcallback_);
        sptcpconnection->SetConnectionCleanUp(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            //每个tcp连接保存起来
            tcpconnlist_[clientfd] = sptcpconnection;
        }
        

        newconnectioncallback_(sptcpconnection);
        //Bug，应该把事件添加的操作放到最后,否则bug segement fault,导致HandleMessage中的phttpsession==NULL
        //总之就是做好一切准备工作再添加事件到epoll！！！
        sptcpconnection->AddChannelToLoop();
    }
}

//连接清理,bugfix:这里应该由主loop来执行，投递回主线程删除 OR 多线程加锁删除
void TcpServer::RemoveConnection(std::shared_ptr<TcpConnection> sptcpconnection)
{
    std::lock_guard<std::mutex> lock(mutex_);
    --conncount_;
    //std::cout << "clean up connection, conncount is" << conncount_ << std::endl;   
    tcpconnlist_.erase(sptcpconnection->fd());
}

void TcpServer::OnConnectionError()
{    
    std::cout << "UNKNOWN EVENT" << std::endl;
    serversocket_.Close();
}

void Setnonblocking(int fd)
{
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0)
    {
        perror("fcntl(fd,GETFL)");
        exit(1);
    }
    if (fcntl(fd, F_SETFL, opts | O_NONBLOCK) < 0)
    {
        perror("fcntl(fd,SETFL,opts)");
        exit(1);
    }
}
