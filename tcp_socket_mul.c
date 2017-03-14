#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#include <netinet/tcp.h>

#define BACKLOG 10
#define MAXCONN 100		//最大连接数
#define BUFFSIZE 1024	//


void sig_wait_alarm(void);
//引入alarm作为协议的超时控制

typedef struct{
	unsigned char funcid;
	unsigned short len;
	unsigned short checksum;
	unsigned char ack;
}pkg_type_t;

pkg_type_t client_pkg;

typedef unsigned char BYTE;
typedef struct						//客户端结构体
{
    struct sockaddr_in addr;		//客户端ip
    int clientfd;					//客户端文件描述符
    int isConn;						//客户端连接状态
    int index;						//客户端在列表中的索引
    pthread_t threadID;				//客户端对应的线程ID
	
} ClientInfo;

typedef struct{
	struct sockaddr_in addr;		//服务器ip
	int listenfd;					//监视文件描述符
	int isExit;						//服务器退出状态
	pthread_t threadID;				//服务器线程ID
	int mode;						//通讯方式,=0透明传输,=1协议传输
}ServerInfo;

pthread_mutex_t activeConnMutex;
pthread_mutex_t clientsMutex[MAXCONN];
pthread_cond_t connDis;

pthread_mutex_t waitMutex;			//线程挂起锁
pthread_cond_t waitCond;			//线程唤醒条件变量

//pthread_t serverManagerID;

ServerInfo server={
	.isExit=0,
	.mode=0,
};
ClientInfo clients[MAXCONN];

struct itimerval new_value,old_value;
int waitflag=0;     //=0没有等待事件,=1正在等待,=2接收到响应结束等待,=3超时结束等待
//int serverExit = 0;

int anetKeepAlive(char *err, int fd, int interval);

/*@brief Transform the all upper case 
*
*/
void tolowerString(char *s)
{
    int i=0;
    while(i < strlen(s))
     {
        s[i] = tolower(s[i]);
        ++i;
    } 
}

void listAll(char *all)
{
    int i=0, len = 0;
    len += sprintf(all+len, "Index   \t\tIP Address   \t\tPort\n");			//列出表头
    for(;i<MAXCONN;++i)
    {
        pthread_mutex_lock(&clientsMutex[i]);
        if(clients[i].isConn)
            len += sprintf(all+len, "%.8d\t\t%s\t\t%d\n",clients[i].index, inet_ntoa(clients[i].addr.sin_addr), ntohs(clients[i].addr.sin_port));
        pthread_mutex_unlock(&clientsMutex[i]);
    }
}

void clientManager(void* argv)
{
    ClientInfo *client = (ClientInfo *)(argv);
    
    BYTE buff[BUFFSIZE];
    int recvbytes;
    
    int i=0;
    int clientfd = client->clientfd;
    struct sockaddr_in addr = client->addr;
    int isConn = client->isConn;
    int clientIndex = client->index;
    
    while((recvbytes = recv(clientfd, buff, BUFFSIZE, 0)) != -1)
    { 
		if(recvbytes==0)	break;

		if(server.mode==0)		//透明传输
		{
			int i=0;
			printf("len=%d\n",recvbytes);
			printf("[clientIndex = %04d]", clientIndex);
			for(i=0;i<recvbytes;i++)
			{
				printf("0x%02X ",(unsigned char)buff[i]);
			}
			printf("\n");
		}
		else
	 	{
			if(recvbytes<8)	continue;
			else{
				if(buff[0]!=0xFB)	continue;
				else{
                    //unsigned short lenH=(unsigned short)buff[2]<<8;
                    //printf("lenH=%d\n",lenH);
					client_pkg.funcid=buff[1];
					client_pkg.len=(((unsigned short)buff[3])<<8)+buff[2];
                    printf("buff[2]=0x%02x,buff[3]=0x%02x\n",buff[2],buff[3]);
                    printf("funcid=%d,len=%d\n",client_pkg.funcid,client_pkg.len);
					if(client_pkg.len+8!=recvbytes)	continue;
					else{
						if(buff[7+client_pkg.len]!=0xFD)	continue;
						else{
							client_pkg.ack=buff[6+client_pkg.len];
							switch(client_pkg.funcid)
							{
								case 0x20:				//数据包
								if(client_pkg.ack==0)	//请求
								{
									//数据封装，并发送响应
									unsigned char resBuff[8];

									resBuff[0]=0xFB;
									resBuff[1]=client_pkg.funcid;
									resBuff[2]=0;
									resBuff[3]=0;
									resBuff[4]=0;
									resBuff[5]=0;
									resBuff[6]=1;
									resBuff[7]=0xFD;
									send(clientfd,resBuff,8,0);/*向客户端发送数据*/
		 						}
								else					//响应
								{
                                    if(waitflag==1)
                                    {
                                        waitflag=2;
    									//终止定时器
    									new_value.it_value.tv_sec=0;
    									new_value.it_value.tv_usec=0;
    									new_value.it_interval=new_value.it_value;
    									setitimer(ITIMER_REAL,&new_value,NULL);
    
    									pthread_mutex_lock(&waitMutex);
    
    									pthread_cond_signal(&waitCond);
    
    									pthread_mutex_unlock(&waitMutex);
                                    }
		 						}
								break;
								case 0x01:			//上线包
								if(client_pkg.ack==0)	//请求
								{
									//数据封装，并发送响应
									unsigned char resBuff[8];

									resBuff[0]=0xFB;
									resBuff[1]=client_pkg.funcid;
									resBuff[2]=0;
									resBuff[3]=0;
									resBuff[4]=0;
									resBuff[5]=0;
									resBuff[6]=1;
									resBuff[7]=0xFD;
									send(clientfd,resBuff,8,0);/*向客户端发送数据*/
								}
//								else					//响应
//								{
//									//终止定时器
//									new_value.it_value.tv_sec=0;
//									new_value.it_value.tv_usec=0;
//									new_value.it_interval=new_value.it_value;
//									setitimer(ITIMER_REAL,&new_value,NULL);
//
//									pthread_mutex_lock(&waitMutex);
//
//									pthread_cond_signal(&waitCond);
//
//									pthread_mutex_unlock(&waitMutex);
//								}
								break;
								case 0x02:			//下线包
								if(client_pkg.ack==0)	//请求
 								{
									//数据封装，并发送响应
									unsigned char resBuff[8];

									resBuff[0]=0xFB;
									resBuff[1]=client_pkg.funcid;
									resBuff[2]=0;
									resBuff[3]=0;
									resBuff[4]=0;
									resBuff[5]=0;
									resBuff[6]=1;
									resBuff[7]=0xFD;
									send(clientfd,resBuff,8,0);/*向客户端发送数据*/
								}
//								else					//响应
//								{
//									//终止定时器
//									new_value.it_value.tv_sec=0;
//									new_value.it_value.tv_usec=0;
//									new_value.it_interval=new_value.it_value;
//									setitimer(ITIMER_REAL,&new_value,NULL);
//
//									pthread_mutex_lock(&waitMutex);
//
//									pthread_cond_signal(&waitCond);
//
//									pthread_mutex_unlock(&waitMutex);
//								}
								break;
								case 0x03:			//心跳包
								if(client_pkg.ack==0)	//请求
								{
									//数据封装，并发送响应
									unsigned char resBuff[8];

									resBuff[0]=0xFB;
									resBuff[1]=client_pkg.funcid;
									resBuff[2]=0;
									resBuff[3]=0;
									resBuff[4]=0;
									resBuff[5]=0;
									resBuff[6]=1;
									resBuff[7]=0xFD;
									send(clientfd,resBuff,8,0);/*向客户端发送数据*/
								}
//								else					//响应
//								{
//									//终止定时器
//									new_value.it_value.tv_sec=0;
//									new_value.it_value.tv_usec=0;
//									new_value.it_interval=new_value.it_value;
//									setitimer(ITIMER_REAL,&new_value,NULL);
//
//									pthread_mutex_lock(&waitMutex);
//
//									pthread_cond_signal(&waitCond);
//
//									pthread_mutex_unlock(&waitMutex);
//								}
								break;
								default:
								break;	
		 					}
		 				}
		 			}
		 		}	
		 	}
		}
 	}   //end while 
    
    pthread_mutex_lock(&clientsMutex[clientIndex]);
    client->isConn = 0;
    pthread_mutex_unlock(&clientsMutex[clientIndex]);
    
    if(close(clientfd) == -1)
        fprintf(stderr, "Close %d client eroor: %s(errno: %d)\n", clientfd, strerror(errno), errno);
    fprintf(stderr, "ClientIndex %d connetion is closed\n", clientIndex);
    
    pthread_exit(NULL);
} 

void serverManager(void* argv)
{
    while(1)
    {
        char cmd[100];
        scanf("%s", cmd);
        tolowerString(cmd);
        if(strcmp(cmd, "exit") == 0)
            server.isExit = 1;
        else if(strcmp(cmd, "list") == 0)
        {
            char buff[BUFFSIZE];
            listAll(buff);
            fprintf(stdout, "%s", buff);
        }
        else if(strcmp(cmd, "kill") == 0)
        {
            int clientIndex;
            scanf("%d", &clientIndex);
            if(clientIndex >= MAXCONN)
            {
                fprintf(stderr, "Unkown client!\n");
                continue;
            }
            pthread_mutex_lock(&clientsMutex[clientIndex]);
            if(clients[clientIndex].isConn)
            {
                if(close(clients[clientIndex].clientfd) == -1)
                    fprintf(stderr, "Close %d client eroor: %s(errno: %d)\n", clients[clientIndex].clientfd, strerror(errno), errno);
            }
            else
            {
                fprintf(stderr, "Unknown client!\n");
            }
            pthread_mutex_unlock(&clientsMutex[clientIndex]);
            pthread_cancel(clients[clientIndex].threadID);
                
        }
		else if(strncmp(cmd, "send",4) == 0)
		{ 
			BYTE buff[BUFFSIZE];
			if(sizeof(cmd)>=5)
		 	{
				int index=0;
				int num=0;
				char c;
				sscanf(cmd+4,"%d",&index);
				fprintf(stdout,"wait for sending data to client %d\n>",index);
				//	清空缓冲区，不然会影响后面的输入
				while((c=getchar())!='\n'&&c!=EOF);
				if(server.mode==0)										//透明传输
				{
					while((c=getchar())!='\n')
					{
						buff[num]=c;
						num++;
					}
					
					buff[num]=0;
					send(clients[index].clientfd,buff,strlen(buff),0);/*向客户端发送数据*/
					num=0;
				}
				else													//协议传输
				{
					//格式化数据
					//发送
					//等待响应或者超时
					buff[0]=0xFB;
					buff[1]=0x20;
					
					num=0;
					while((c=getchar())!='\n')
					{
						buff[4+num]=c;
						num++;
					}
					buff[2]=(unsigned char)num;
					buff[3]=(unsigned char)(num>>8);
					
					buff[4+num]=0;
					buff[5+num]=0;
					buff[6+num]=0;
					buff[7+num]=0xFD;
					
					waitflag=1;         //开启等待
					send(clients[index].clientfd,buff,8+num,0);/*向客户端发送数据*/
					
					new_value.it_value.tv_sec=3;			//只延时，不定时
					new_value.it_value.tv_usec=0;
					new_value.it_interval.tv_sec=0;
					new_value.it_interval.tv_usec=0;
					setitimer(ITIMER_REAL,&new_value,&old_value);
					
					pthread_mutex_lock(&waitMutex);
					pthread_cond_wait(&waitCond,&waitMutex);
					pthread_mutex_unlock(&waitMutex);

					//判断响应还是超时
					if(waitflag==3)				                    //超时
					{
						printf("get response timeout\n");
					} 
					else if(waitflag==2)							//成功接收到应答数据
					{ 
						printf("response ok\n");
					}
                    waitflag=0;
				}
			}
			
		}
		else if(strncmp(cmd,"mode",4) == 0)						//模式切换
		{
			int oldmode=server.mode;
			server.mode=atoi(cmd+4);
			if(server.mode==0)									//切换成透明模式
			{
				if(oldmode==0)
					printf("old mode:transport,new mode:transport\n");
				else
					printf("old mode:protocol,new mode:transport\n");
			}
			else
			{
				if(oldmode==0)
					printf("old mode:transport,new mode:protocol\n");
				else
					printf("old mode:protocol,new mode:protocol\n");
			}
		}
        else
        {
            fprintf(stderr, "Unknown command!\n");
        } 
    }
}

int main()
{
   int activeConn = 0;
   
   //initialize the mutex 
   pthread_mutex_init(&activeConnMutex, NULL);   
   pthread_cond_init(&connDis, NULL);

   pthread_mutex_init(&waitMutex,NULL);
   pthread_cond_init(&waitCond,NULL);
	
   signal(SIGALRM,(void *)sig_wait_alarm);

   int i=0;
   for(;i<MAXCONN;++i)
       pthread_mutex_init(&clientsMutex[i], NULL); 
   
   for(i=0;i<MAXCONN;++i)
       clients[i].isConn = 0; 
       
   //create the server manager thread
   pthread_create(&server.threadID, NULL, (void *)(serverManager), NULL);
   
   
   //int listenfd;
   //struct sockaddr_in  servaddr;
   int ServerPort;   
 
   //create a socket
   if((server.listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
       fprintf(stderr, "Create socket error: %s(errno: %d)\n", strerror(errno), errno);
       exit(0);
   } 
   else
       fprintf(stdout, "Create a socket successfully\n");
   
   fcntl(server.listenfd, F_SETFL, O_NONBLOCK);       //set the socket non-block
  
   printf("Input Listen Port:");
   scanf("%d",&ServerPort);

   //ServerPort=atoi(ServerPorts);
   printf("ServerPort=%d\n",ServerPort);

   //清空缓冲区，不然会影响后面的输入
   char ch;
   while((ch=getchar())!='\n'&&ch!=EOF);
 
   //set the server address
   memset(&server.addr, 0, sizeof(server.addr));  //initialize the server address 
   server.addr.sin_family = AF_INET;           //AF_INET means using TCP protocol
   server.addr.sin_addr.s_addr = htonl(INADDR_ANY);    //any in address(there may more than one network card in the server)
   server.addr.sin_port = htons(ServerPort);            //set the port
   
   //bind the server address with the socket
   if(bind(server.listenfd, (struct sockaddr*)(&server.addr), sizeof(server.addr)) == -1)
   {
        fprintf(stderr, "Bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        exit(0);
   } 
   else
       fprintf(stdout, "Bind socket successfully\n");
   
   //listen
   if(listen(server.listenfd, BACKLOG) == -1)
   { 
       fprintf(stderr, "Listen socket error: %s(errno: %d)\n", strerror(errno), errno);
       exit(0);
   }
   else
       fprintf(stdout, "Listen socket successfully\n");
    
   
   while(1)
   {
       if(server.isExit)
       {
           for(i=0;i<MAXCONN;++i)
           {
               if(clients[i].isConn)
               {
                   if(close(clients[i].clientfd) == -1)         //close the client 
                       fprintf(stderr, "Close %d client eroor: %s(errno: %d)\n", clients[i].clientfd, strerror(errno), errno);
                   if(pthread_cancel(clients[i].threadID) != 0)         //cancel the corresponding client thread
                        fprintf(stderr, "Cancel %d thread eroor: %s(errno: %d)\n", (int)(clients[i].threadID), strerror(errno), errno);
               }
           }
           return 0;    //main exit;
       }
       
       pthread_mutex_lock(&activeConnMutex);
       if(activeConn >= MAXCONN)
            pthread_cond_wait(&connDis, &activeConnMutex);
       pthread_mutex_unlock(&activeConnMutex);
           
       //find an empty postion for a new connnetion
       int i=0;
       while(i<MAXCONN)
       {
           pthread_mutex_lock(&clientsMutex[i]);
           if(!clients[i].isConn)
           {
               pthread_mutex_unlock(&clientsMutex[i]);
               break;
           }
           pthread_mutex_unlock(&clientsMutex[i]);
           ++i;           
       }   
       
       //accept
       struct sockaddr_in addr;
       int clientfd;
       int sin_size = sizeof(struct sockaddr_in);
       if((clientfd = accept(server.listenfd, (struct sockaddr*)(&addr), &sin_size)) == -1)
       {   
           sleep(1);        
           //fprintf(stderr, "Accept socket error: %s(errno: %d)\n", strerror(errno), errno);
           continue;
           //exit(0);
       } 
       else
           fprintf(stdout, "Accept socket successfully\n");

		char err; 
		anetKeepAlive(&err,clientfd,120);     //保活2分钟
       
       pthread_mutex_lock(&clientsMutex[i]);
       clients[i].clientfd = clientfd;
       clients[i].addr = addr;
       clients[i].isConn = 1;
       clients[i].index = i;
       pthread_mutex_unlock(&clientsMutex[i]);
       
       //create a thread for a client
       pthread_create(&clients[i].threadID, NULL, (void *)clientManager, &clients[i]);     
       
   }     //end-while
}

void sig_wait_alarm()
{
	if(waitflag==1)
    {
        waitflag=3;
    	printf("wait timeout\n");
    	
    	pthread_mutex_lock(&waitMutex);
    	
    	pthread_cond_signal(&waitCond);		//唤醒线程挂起
    
    	pthread_mutex_unlock(&waitMutex);
    }	
} 

/* Set TCP keep alive option to detect dead peers. The interval option
 * is only used for Linux as we are using Linux-specific APIs to set
 * the probe send time, interval, and count. */
int anetKeepAlive(char *err, int fd, int interval)
{
	int val = 1;
    //开启keepalive机制
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
	    //anetSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
	    printf("error:0");
	    return 0;
	}

#if 1
    /* Default settings are more or less garbage, with the keepalive time
	 * set to 7200 by default on Linux. Modify settings to make the feature
	 * actually useful. */

	/* Send first probe after interval. */
	val = interval;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
		//anetSetError(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
		printf("error:1");
	    return 0;
	}

	/* Send next probes after the specified interval. Note that we set the
	 * delay as interval / 3, as we send three probes before detecting
	 * an error (see the next setsockopt call). */
	val = 5;
	if (val == 0) val = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
		//anetSetError(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
		printf("error:2");
		return 0;
	}

	/* Consider the socket in error state after three we send three ACK
	 * probes without getting a reply. */
	val = 3;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
	//anetSetError(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
		printf("error:3");
		return 0;
	}
#endif

	return 1;
}


