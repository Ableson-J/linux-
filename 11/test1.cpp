//
// Created by jiang on 22-11-19.
//

#include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <stdbool.h>
 #include <signal.h>
 #include <sys/types.h>
 #include <errno.h>
 #include <string.h>
#include <assert.h>
#include <iostream>

using namespace std;
 void int_handler (int signum)
 {
     cout << "here5" << endl;
     printf("int handler %d\n",signum);
 }

void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = int_handler;
//    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

 int main(int argc, char **argv)
{
     char buf[100];
     ssize_t ret;
     printf("here4\n");
    addsig(SIGINT);
    printf("here3\n");
    bzero(buf,100);
    printf("here1\n");
    ret = read(STDIN_FILENO,buf,10);
    printf("here2\n");
    if (ret == -1)
    {
        printf("read error %s\n", strerror(errno));

    }
    printf("read %d bytes, content is %s\n",ret,buf    );
    return 0;
 }