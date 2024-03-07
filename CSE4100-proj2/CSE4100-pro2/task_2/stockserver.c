/* 
 * stockserver_thread.c - A thread-based concurrent server
 */ 

#include "csapp.h"

#define MAX_STOCKS 100000
#define FILENAME "stock.txt"
#define SBUFSIZE 1000
#define NTHREADS 1000

typedef struct stock_item {
    int fd;
    int id;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex, w;
    struct stock_item *left;
    struct stock_item *right;
} stock_item;

typedef struct {
    int *buf;           //buffer array
    int n;              //Maximum number of slots
    int front;          //buf[(front_1)%n] is first item
    int rear;           //buf[rear%n] is last item
    sem_t mutex;        //protects acceesses to buf
    sem_t slots;        //Count available slots
    sem_t items;        //Count available items
}sbuf_t;

typedef struct {
    int connfd;
} thread_args;
 
void sigint_handler(int signum);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);
void *thread(void *vargp);

void get_stock_from_file();
void handle_client_request(int connfd);
void remove_client(int connfd);
void handle_show_request(int connfd);
void handle_buy_request(int connfd, int id, int num);
void handle_sell_request(int connfd, int id, int num);
void update_stock_file(const char *filename);
stock_item* insertItem(stock_item* node, stock_item* item);
stock_item* findStockById(int id);

int byte_cnt = 0;
sbuf_t sbuf;
int request_cnt = 0;
struct timeval start;	/* starting time */
struct timeval end;	/* ending time */
unsigned long e_usec;	/* elapsed microseconds */

stock_item* root = NULL;
struct stock_item* stocks[MAX_STOCKS];

int num_stocks = 0;

int main(int argc, char **argv) 
{
    int i, listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);

    Signal(SIGINT, sigint_handler);
    get_stock_from_file();

    for (i = 0; i < NTHREADS; i++) 
        Pthread_create(&tid, NULL, thread, NULL);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);   //connfd를 버퍼에 넣어줄 것임. connfd가 버퍼에 들어가 있다가 thread가 하나씩 꺼내감.
    }
    update_stock_file(FILENAME);
    exit(0);
}


void sigint_handler(int signum){
    update_stock_file(FILENAME);
    if(root == NULL) printf("root is null\n");
    exit(0);
}

void sbuf_init(sbuf_t *sp, int n){
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) {
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);                  //비어있는 슬롯이 없다면 slot이 생길 때까지 기다림(slot이 1 이상이 되는 순간 slot을 0으로 만들고 리턴)
    P(&sp->mutex);
    if(sp->rear == 0) 
        gettimeofday(&start, 0);        // 시간측정시작점
    sp->buf[(++sp->rear)%(sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);                  //item을 1증가
}

int sbuf_remove(sbuf_t *sp) { 
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front) % (sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        handle_client_request(connfd);
        Close(connfd);
    }
    return NULL;
}

void get_stock_from_file(){
    FILE *fp = fopen(FILENAME, "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    char line[MAXLINE];
    while (fgets(line, MAXLINE, fp) != NULL) {
        char *token = strtok(line, " ");
        if (token == NULL) {
            continue;
        }
        int id = atoi(token);
        token = strtok(NULL, " ");
        if (token == NULL) {
            continue;
        }
        int left_stock = atoi(token);
        token = strtok(NULL, " ");
        if (token == NULL) {
            continue;
        }
        int price = atoi(token);
        stock_item *item = (struct stock_item*)malloc(sizeof(stock_item));
        item->id = id;
        item->left_stock = left_stock;
        item->price = price;
        item->readcnt = 0;
        item->left = NULL;
        item->right = NULL;
        Sem_init(&(item->mutex), 0, 1);
        Sem_init(&(item->w), 0, 1);
        stocks[++num_stocks] = item;
        root = insertItem(root, item);
    }

    fclose(fp);
}

void handle_client_request(int connfd)
{

    char buf[MAXLINE];
    int n;
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        request_cnt++;
        printf("Server received %d bytes on connfd %d\n", n, connfd);
        char *token = strtok(buf, " ");
        if (token == NULL) {
            return;
        }

        //명령어 분해 후, 명령어에 맞는 함수 호출
        if (strcmp(token, "show\n") == 0) {
            handle_show_request(connfd);
        } else if (strcmp(token, "buy") == 0) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                return;
            }
            int id = atoi(token);
            token = strtok(NULL, " ");
            if (token == NULL) {
                return;
            }
            int num = atoi(token);
            handle_buy_request(connfd, id, num);
        } else if (strcmp(token, "sell") == 0) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                return;
            }
            int id = atoi(token);
            token = strtok(NULL, " ");
            if (token == NULL) {
                return;
            }
            int num = atoi(token);
            handle_sell_request(connfd, id, num);
        } else if (strcmp(token, "exit\n") == 0) {
            update_stock_file(FILENAME);
        } else {
            Rio_writen(connfd, "Unvalid command\n", MAXLINE);
        }
    }    
}

void remove_client(int connfd) {
    Close(connfd);
}

void handle_show_request(int connfd) {
    int i;
    char buf[MAXLINE] = "show\n";
    char temp[MAXLINE];

    for (int i = 1; i <= num_stocks; i++) {
        stock_item *item = stocks[i];
        P(&(item->mutex));
        item->readcnt++;
        if (item->readcnt == 1)
            P(&(item->w));
        V(&(item->mutex));

        sprintf(temp, "%d %d %d\n", item->id, item->left_stock, item->price);
        strcat(buf, temp);

        P(&(item->mutex));
        item->readcnt--;
        if (item->readcnt == 0)
            V(&(item->w));
        V(&(item->mutex));
    }

    Rio_writen(connfd, buf, MAXLINE);
}

void handle_buy_request(int connfd, int id, int num) {
    char buf[MAXLINE];
    char temp[MAXLINE];
    sprintf(buf, "buy %d %d\n", id, num);
    stock_item *item = findStockById(id);
    if(item != NULL) {
        P(&(item->w));
        if (item->left_stock >= num) {
            item->left_stock -= num;
            sprintf(temp, "[buy] success\n");
            strcat(buf, temp);
        } else {
            sprintf(temp, "Not enough left stock\n");
            strcat(buf, temp);
        }
        V(&(item->w));
    } else {
        sprintf(temp, "there is no such id\n");
        strcat(buf, temp);
    }
    Rio_writen(connfd, buf, MAXLINE);
}

void handle_sell_request(int connfd, int id, int num) {
    int i = 1;
    char buf[MAXLINE];
    char temp[MAXLINE];
    sprintf(buf, "sell %d %d\n", id, num);
    stock_item *item = findStockById(id);
    if(item != NULL) {
        P(&(item->w));
        item->left_stock += num;
        sprintf(temp, "[sell] success\n");
        strcat(buf, temp);
        V(&(item->w));
    } else {
            sprintf(temp, "there is no such id\n");
            strcat(buf, temp);
    }
    Rio_writen(connfd, buf, MAXLINE);
}

void update_stock_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen");
        return;
    }

    int i;
    for (i = 1; i <= num_stocks; i++) {
        stock_item *item = stocks[i];
        fprintf(fp, "%d %d %d\n", item->id, item->left_stock, item->price);
    }

    fclose(fp);
}

// Function to insert a new item into the binary tree
stock_item* insertItem(stock_item* node, stock_item* item) {
    if (node == NULL) {
        return item;
    }

    if (item->id < (node)->id) {
        node->left = insertItem((node)->left, item);
    } else if (item->id > (node)->id) {
        node->right = insertItem((node)->right, item);
    } else {
        // Duplicate ID, handle accordingly (e.g., error or update existing item)
    }
    return node;
}

stock_item* findStockById(int id) {
    stock_item* node = root;
    while(1) {
        if (node == NULL) return NULL;
        
        if (node->id == id) return node;
        else if (node->id > id) node = node->left;
        else node = node->right;
    }
}
