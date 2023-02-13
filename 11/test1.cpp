#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
void f(int sig)
{
    printf(" process pid:%d\n",getpid());

}
int main()
{
    sigset_t set;              //创建信号集
    sigemptyset(&set);         //清空信号集
    sigaddset(&set,2);              //添加信号到信号集中，2为SIGINT
    sigprocmask(SIG_BLOCK,&set,NULL);   //阻塞掩码，阻塞信号集
    pid_t pid =     fork();
    if(pid==-1)
    {
        perror("fork filed:");
        return -1;
    }
    else if(pid >0)   //父进程
    {
        signal(SIGINT,f);
        printf("father pid :%d\n",getpid());
        for(int i=20;i>=0;i--)
        {
            printf("%d\n",i);
            sleep(1);
        }
        sigprocmask(SIG_UNBLOCK,&set,NULL);   //解除阻塞信号集
        wait(NULL);
    }
    else               //子进程
    {
        printf("child pid :%d\n",getpid());
        signal(SIGINT,f);      // 这里会继承父进程的阻塞掩码，被阻塞
        printf("child sleep 5s\n");
        sleep(5);
        printf("child wake\n");
        sigprocmask(SIG_UNBLOCK,&set,NULL);   //解除阻塞信号集
        pause();
    }
}