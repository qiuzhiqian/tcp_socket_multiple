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
#include <unistd.h>

#include <netinet/tcp.h>


//#define PORT  8888
#define BACKLOG 10
#define MAXCONN 100
#define BUFFSIZE 1024

typedef unsigned char BYTE;
typedef struct ClientInfo
{
    struct sockaddr_in addr;
    int clientfd;
    int isConn;
    int index;
    pthread_t threadID;
} ClientInfo;

pthread_mutex_t activeConnMutex;
pthread_mutex_t clientsMutex[MAXCONN];
pthread_cond_t connDis;

//pthread_t threadID[MAXCONN];
pthread_t serverManagerID;

ClientInfo clients[MAXCONN];

int serverExit = 0;

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
    len += sprintf(all+len, "Index   \t\tIP Address   \t\tPort\n");
    for(;i<MAXCONN;++i)
    {
        pthread_mutex_lock(&clientsMutex[i]);
        if(clients[i].isConn)
            len += sprintf(all+len, "%.8d\t\t%s\t\t%d\n",clients[i].index, inet_ntoa(clients[i].addr.sin_addr), clients[i].addr.sin_port);
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
		int i=0;
		printf("len=%d\n",recvbytes);
		printf("[clientIndex = %04d]", clientIndex);
		for(i=0;i<recvbytes;i++)
		{
				printf("0x%02X ",(unsigned char)buff[i]);
		}
		printf("\n");
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
            serverExit = 1;
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
				while((c=getchar())!='\n')
                {
					buff[num]=c;
					num++;
                }
                buff[num]=0;
                send(clients[index].clientfd,buff,strlen(buff),0);/*向客户端发送数据*/
                num=0;
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
   int i=0;
   for(;i<MAXCONN;++i)
       pthread_mutex_init(&clientsMutex[i], NULL); 
   
   for(i=0;i<MAXCONN;++i)
       clients[i].isConn = 0; 
       
   //create the server manager thread
   pthread_create(&serverManagerID, NULL, (void *)(serverManager), NULL);
   
   
   int listenfd;
   struct sockaddr_in  servaddr;
   int ServerPort;   
 
   //create a socket
   if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
       fprintf(stderr, "Create socket error: %s(errno: %d)\n", strerror(errno), errno);
       exit(0);
   } 
   else
       fprintf(stdout, "Create a socket successfully\n");
   
   fcntl(listenfd, F_SETFL, O_NONBLOCK);       //set the socket non-block
  
   printf("Input Listen Port:");
   scanf("%d",&ServerPort);

   //ServerPort=atoi(ServerPorts);
   printf("ServerPort=%d\n",ServerPort);

   //清空缓冲区，不然会影响后面的输入
   char ch;
   while((ch=getchar())!='\n'&&ch!=EOF);
 
   //set the server address
   memset(&servaddr, 0, sizeof(servaddr));  //initialize the server address 
   servaddr.sin_family = AF_INET;           //AF_INET means using TCP protocol
   servaddr.sin_addr.s_addr = htonl(INADDR_ANY);    //any in address(there may more than one network card in the server)
   servaddr.sin_port = htons(ServerPort);            //set the port
   
   //bind the server address with the socket
   if(bind(listenfd, (struct sockaddr*)(&servaddr), sizeof(servaddr)) == -1)
   {
        fprintf(stderr, "Bind socket error: %s(errno: %d)\n", strerror(errno), errno);
        exit(0);
   } 
   else
       fprintf(stdout, "Bind socket successfully\n");
   
   //listen
   if(listen(listenfd, BACKLOG) == -1)
   { 
       fprintf(stderr, "Listen socket error: %s(errno: %d)\n", strerror(errno), errno);
       exit(0);
   }
   else
       fprintf(stdout, "Listen socket successfully\n");
    
   
   while(1)
   {
       if(serverExit)
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
       if((clientfd = accept(listenfd, (struct sockaddr*)(&addr), &sin_size)) == -1)
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


