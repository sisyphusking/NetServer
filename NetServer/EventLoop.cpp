
#include "EventLoop.h"
#include <iostream>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>

//参照muduo，实现跨线程唤醒
int CreateEventFd()
{   
    //eventfd实现进程间通讯的系统调用，这里就是利用信号量来实现的？  
    // EFD_CLOEXEC: eventfd()会返回一个文件描述符，如果该进程被fork的时候，这个文件描述符也会复制过去
    // EFD_NONBLOCK: 如果没有设置了这个标志位，那read操作将会阻塞直到计数器中有值。如果没有设置这个标志位，计数器没有值的时候也会立即返回-1；
    int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        std::cout << "Failed in eventfd" << std::endl;
        exit(1);
    }
    return evtfd;
}

EventLoop::EventLoop(/* args */)
    : functorlist_(), //设置为空
    channellist_(),
    activechannellist_(), //活跃事件
    poller_(),//eventloop封装
    quit_(true),
    tid_(std::this_thread::get_id()),
    mutex_(),
    wakeupfd_(CreateEventFd()),//设置跨进程唤醒的fd
    wakeupchannel_()
{
    wakeupchannel_.SetFd(wakeupfd_); //设置唤醒fd id
    
    //设置触发事件， 这里是epoll的边缘触发模式，ET  
    // EPOLLIN:表示关联的fd可以进行读操作了。
    // EPOLLOUT:表示关联的fd可以进行写操作了。
    wakeupchannel_.SetEvents(EPOLLIN | EPOLLET);  
    //设置读事件回调函数
    wakeupchannel_.SetReadHandle(std::bind(&EventLoop::HandleRead, this));
    wakeupchannel_.SetErrorHandle(std::bind(&EventLoop::HandleError, this));
    AddChannelToPoller(&wakeupchannel_);//将channel加入到poller中，这里的channel只是wakeup的，后面还有个serverchannel? 
}

EventLoop::~EventLoop()
{
    close(wakeupfd_);
}

void EventLoop::WakeUp()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupfd_, (char*)(&one), sizeof one);
}

//当有数据到达时的处理逻辑?
void EventLoop::HandleRead()
{
    uint64_t one = 1;
    //系统调用api
    ssize_t n = read(wakeupfd_, &one, sizeof one);
}

void EventLoop::HandleError()
{
    ;
}    

void EventLoop::loop()
{
    quit_ = false;
    while(!quit_)
    {   
        //取出所有的活跃连接到activechannellist_中
        poller_.poll(activechannellist_);
        //std::cout << "server HandleEvent" << std::endl;
        for(Channel *pchannel : activechannellist_)
        {   
            //serverchannel         
            pchannel->HandleEvent();//处理事件
        }
        //活跃连接的事件处理完了。。。
        activechannellist_.clear();
        ExecuteTask();
    }
}