#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE 10
/* You won't lose style points for including this long line in your code */
static const char* user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
struct rwlock_t {
	sem_t lock;
	sem_t writelock;
	int readers;
};
struct Cache {
	int  used;
	char key[MAXLINE];	//URL索引
	char value[MAX_OBJECT_SIZE];	//URL所对应的缓存 
};
/*url信息的结构体*/
struct UrlData {
	char host[MAXLINE];
	char port[MAXLINE];
	char path[MAXLINE];
};
struct Cache cache[MAX_CACHE];
struct rwlock_t* rw;
int nowpointer;

/*函数定义*/
void doit(int fd);
void parse_url(char* url, struct UrlData* u);
void change_httpdata(rio_t* rio, struct UrlData* u, char* new_httpdata);	//修改http数据 
void thread(void* v);
void rwlock_init();
int  readcache(int fd, char* key);
void writecache(char* buf, char* key);

int main(int argc, char** argv) {
	rw = Malloc(sizeof(struct rwlock_t));
	pthread_t tid;//线程标识
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	rwlock_init();

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	listenfd = Open_listenfd(argv[1]);
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
		Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s %s)\n", hostname, port);
		Pthread_create(&tid, NULL, thread, (void*)&connfd);//创建线程
	}
	return 0;
}
//初始化读者写者锁
void rwlock_init() {
	rw->readers = 0; //读者个数设为0
	sem_init(&rw->lock, 0, 1);
	sem_init(&rw->writelock, 0, 1);
}

void thread(void* v) {
	int fd = *(int*)v;
	Pthread_detach(pthread_self());
	doit(fd);
	close(fd);
}

void doit(int fd) {
	char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	char new_httpdata[MAXLINE], urltemp[MAXLINE];
	struct UrlData u;
	rio_t rio, server_rio;
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);

	sscanf(buf, "%s %s %s", method, url, version);
	strcpy(urltemp, url);	//赋值url副本以供读者写者使用，因为在解析url中，url可能改变 
	//urltemp临时存储url值
	/*只接受GEI请求*/
	if (strcmp(method, "GET") != 0) {
		printf("The proxy can not handle this method: %s\n", method);
		return;
	}

	if (readcache(fd, urltemp) != 0)//在缓存中	
		return;

	parse_url(url, &u);	//解析url 
	change_httpdata(&rio, &u, new_httpdata);//修改http数据

	int server_fd = Open_clientfd(u.host, u.port);
	size_t n;

	Rio_readinitb(&server_rio, server_fd);
	Rio_writen(server_fd, new_httpdata, strlen(new_httpdata));

	char cache[MAX_OBJECT_SIZE];
	int sum = 0;
	while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
		Rio_writen(fd, buf, n);
		sum += n;
		strcat(cache, buf);
	}
	printf("proxy send %ld bytes to client\n", sum);
	if (sum < MAX_OBJECT_SIZE)//还可以缓存，数量没有超出
		writecache(cache, urltemp);
	close(server_fd);
	return;
}
//写缓存
void writecache(char* buf, char* key) {
	sem_wait(&rw->writelock);//等待锁
	int index;
	while (cache[nowpointer].used != 0) {
		cache[nowpointer].used = 0;
		nowpointer = (nowpointer + 1) % MAX_CACHE;
	}//时钟算法，最近的未使用者替换
	index = nowpointer;//新的缓存位置
	cache[index].used = 1;//标记最近使用过
	strcpy(cache[index].key, key);//存储
	strcpy(cache[index].value, buf);//存储
	sem_post(&rw->writelock);//释放锁 
	return;
}
//读缓存，经典的多读一写问题
int readcache(int fd, char* url) {
	sem_wait(&rw->lock);//等待锁
	if (rw->readers == 0)//如果没有读者，可能有写者在写，必须等待并获取写者锁 
		sem_wait(&rw->writelock);//读时禁止写，于是占有写着锁 
	rw->readers++;//更改数量
	sem_post(&rw->lock);//全局变量修改完毕，接下来不会进入临界区，释放锁给更多读者使用 
	int i, flag = 0;

	/*依次遍历找到缓存，成功则设置返回值为 1*/
	for (i = 0; i < MAX_CACHE; i++) {
		if (strcmp(url, cache[i].key) == 0) {//寻找成功		
			Rio_writen(fd, cache[i].value, strlen(cache[i].value));
			printf("proxy send %d bytes to client\n", strlen(cache[i].value));
			cache[i].used = 1;
			flag = 1;
			break;
		}
	}

	sem_wait(&rw->lock);//获取读者锁
	rw->readers--;
	if (rw->readers == 0)//没有读者才能释放写者锁
		sem_post(&rw->writelock);
	sem_post(&rw->lock);//释放读者锁
	return flag;
}
/*解析url，解析为host, port, path*/
void parse_url(char* url, struct UrlData* u) {
	char* hostpose = strstr(url, "//");//strstr函数用于寻找字符串中的子串位置
	if (hostpose == NULL) {//没有"//"
		char* pathpose = strstr(url, "/");
		if (pathpose != NULL)
			strcpy(u->path, pathpose);
		strcpy(u->port, "80");//默认端口80端口
		return;
	}
	else {
		char* portpose = strstr(hostpose + 2, ":");
		if (portpose != NULL) {
			int tmp;
			sscanf(portpose + 1, "%d%s", &tmp, u->path);
			sprintf(u->port, "%d", tmp);
			*portpose = '\0';

		}
		else {
			char* pathpose = strstr(hostpose + 2, "/");
			if (pathpose != NULL) {
				strcpy(u->path, pathpose);
				strcpy(u->port, "80");
				*pathpose = '\0';
			}
		}
		strcpy(u->host, hostpose + 2);
	}
	return;
}

void change_httpdata(rio_t* rio, struct UrlData* u, char* new_httpdata) {
	static const char* Con_hdr = "Connection: close\r\n";
	static const char* Pcon_hdr = "Proxy-Connection: close\r\n";
	char buf[MAXLINE];
	char Reqline[MAXLINE], Host_hdr[MAXLINE], Cdata[MAXLINE];//分别为请求行，Host首部字段，和其他不动的数据信息 
	sprintf(Reqline, "GET %s HTTP/1.0\r\n", u->path);	//获取请求行 
	while (Rio_readlineb(rio, buf, MAXLINE) > 0) {
		/*读到空行就算结束，GET请求没有实体体*/
		if (strcmp(buf, "\r\n") == 0) {
			strcat(Cdata, "\r\n");
			break;
		}
		else if (strncasecmp(buf, "Host:", 5) == 0) {
			strcpy(Host_hdr, buf);
		}
		else if (!strncasecmp(buf, "Connection:", 11) && !strncasecmp(buf, "Proxy_Connection:", 17) && !strncasecmp(buf, "User-agent:", 11)) {
			strcat(Cdata, buf);
		}
	}
	if (!strlen(Host_hdr)) {
		/*如果Host_hdr为空，说明该host被加载进请求行的URL中，我们格式读从URL中解析的host*/
		sprintf(Host_hdr, "Host: %s\r\n", u->host);
	}
	sprintf(new_httpdata, "%s%s%s%s%s%s", Reqline, Host_hdr, Con_hdr, Pcon_hdr, user_agent_hdr, Cdata);
	return;
}