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
	char key[MAXLINE];	//URL����
	char value[MAX_OBJECT_SIZE];	//URL����Ӧ�Ļ��� 
};
/*url��Ϣ�Ľṹ��*/
struct UrlData {
	char host[MAXLINE];
	char port[MAXLINE];
	char path[MAXLINE];
};
struct Cache cache[MAX_CACHE];
struct rwlock_t* rw;
int nowpointer;

/*��������*/
void doit(int fd);
void parse_url(char* url, struct UrlData* u);
void change_httpdata(rio_t* rio, struct UrlData* u, char* new_httpdata);	//�޸�http���� 
void thread(void* v);
void rwlock_init();
int  readcache(int fd, char* key);
void writecache(char* buf, char* key);

int main(int argc, char** argv) {
	rw = Malloc(sizeof(struct rwlock_t));
	pthread_t tid;//�̱߳�ʶ
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
		Pthread_create(&tid, NULL, thread, (void*)&connfd);//�����߳�
	}
	return 0;
}
//��ʼ������д����
void rwlock_init() {
	rw->readers = 0; //���߸�����Ϊ0
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
	strcpy(urltemp, url);	//��ֵurl�����Թ�����д��ʹ�ã���Ϊ�ڽ���url�У�url���ܸı� 
	//urltemp��ʱ�洢urlֵ
	/*ֻ����GEI����*/
	if (strcmp(method, "GET") != 0) {
		printf("The proxy can not handle this method: %s\n", method);
		return;
	}

	if (readcache(fd, urltemp) != 0)//�ڻ�����	
		return;

	parse_url(url, &u);	//����url 
	change_httpdata(&rio, &u, new_httpdata);//�޸�http����

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
	if (sum < MAX_OBJECT_SIZE)//�����Ի��棬����û�г���
		writecache(cache, urltemp);
	close(server_fd);
	return;
}
//д����
void writecache(char* buf, char* key) {
	sem_wait(&rw->writelock);//�ȴ���
	int index;
	while (cache[nowpointer].used != 0) {
		cache[nowpointer].used = 0;
		nowpointer = (nowpointer + 1) % MAX_CACHE;
	}//ʱ���㷨�������δʹ�����滻
	index = nowpointer;//�µĻ���λ��
	cache[index].used = 1;//������ʹ�ù�
	strcpy(cache[index].key, key);//�洢
	strcpy(cache[index].value, buf);//�洢
	sem_post(&rw->writelock);//�ͷ��� 
	return;
}
//�����棬����Ķ��һд����
int readcache(int fd, char* url) {
	sem_wait(&rw->lock);//�ȴ���
	if (rw->readers == 0)//���û�ж��ߣ�������д����д������ȴ�����ȡд���� 
		sem_wait(&rw->writelock);//��ʱ��ֹд������ռ��д���� 
	rw->readers++;//��������
	sem_post(&rw->lock);//ȫ�ֱ����޸���ϣ���������������ٽ������ͷ������������ʹ�� 
	int i, flag = 0;

	/*���α����ҵ����棬�ɹ������÷���ֵΪ 1*/
	for (i = 0; i < MAX_CACHE; i++) {
		if (strcmp(url, cache[i].key) == 0) {//Ѱ�ҳɹ�		
			Rio_writen(fd, cache[i].value, strlen(cache[i].value));
			printf("proxy send %d bytes to client\n", strlen(cache[i].value));
			cache[i].used = 1;
			flag = 1;
			break;
		}
	}

	sem_wait(&rw->lock);//��ȡ������
	rw->readers--;
	if (rw->readers == 0)//û�ж��߲����ͷ�д����
		sem_post(&rw->writelock);
	sem_post(&rw->lock);//�ͷŶ�����
	return flag;
}
/*����url������Ϊhost, port, path*/
void parse_url(char* url, struct UrlData* u) {
	char* hostpose = strstr(url, "//");//strstr��������Ѱ���ַ����е��Ӵ�λ��
	if (hostpose == NULL) {//û��"//"
		char* pathpose = strstr(url, "/");
		if (pathpose != NULL)
			strcpy(u->path, pathpose);
		strcpy(u->port, "80");//Ĭ�϶˿�80�˿�
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
	char Reqline[MAXLINE], Host_hdr[MAXLINE], Cdata[MAXLINE];//�ֱ�Ϊ�����У�Host�ײ��ֶΣ�������������������Ϣ 
	sprintf(Reqline, "GET %s HTTP/1.0\r\n", u->path);	//��ȡ������ 
	while (Rio_readlineb(rio, buf, MAXLINE) > 0) {
		/*�������о��������GET����û��ʵ����*/
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
		/*���Host_hdrΪ�գ�˵����host�����ؽ������е�URL�У����Ǹ�ʽ����URL�н�����host*/
		sprintf(Host_hdr, "Host: %s\r\n", u->host);
	}
	sprintf(new_httpdata, "%s%s%s%s%s%s", Reqline, Host_hdr, Con_hdr, Pcon_hdr, user_agent_hdr, Cdata);
	return;
}