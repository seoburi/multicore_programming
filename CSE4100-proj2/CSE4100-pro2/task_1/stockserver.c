/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

#define MAX_STOCKS 100
#define FILENAME "stock.txt"

typedef struct stock_item {
    int id;
    int left_stock;
    int price;
    struct stock_item *left;
    struct stock_item *right;
} stock_item;

typedef struct {    //Represent a pool of connected descriptors
    int maxfd;      //Largest descriptor in read_set
    fd_set read_set;    //Set of all active descriptors (client_fd + listen_fd)
    fd_set ready_set;   //Subset of descriptors ready for reading
    int nready;         //Numver o fready descriptors from select
    int maxi;           //High water index into client array
    int clientfd[FD_SETSIZE];       //Set of active descriptors
    rio_t clientrio[FD_SETSIZE];    //Set of active read buffers
} pool;

int byte_cnt = 0;
int num_stocks;
struct timeval start;	/* starting time */
struct timeval end;	/* ending time */
unsigned long e_usec;	/* elapsed microseconds */
struct stock_item *root = NULL;
struct stock_item *stocks[MAX_STOCKS];

void sigint_handler(int signum);

void echo(int connfd);
void init_pool(int listenfd, pool *p, char *filename);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
void remove_client(int fd);
void handle_client_request(int fd, char *buf);
void handle_show_request(int fd);
void handle_buy_request(int fd, int id, int num);
void handle_sell_request(int fd, int id, int num);
void update_stock_file(char *filename);
stock_item* insertItem(stock_item* node, stock_item* item);
stock_item* findStockById(int id);


int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    static pool pool;

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }

    listenfd = Open_listenfd(argv[1]);
    init_pool(listenfd, &pool, FILENAME);
    Signal(SIGINT, sigint_handler);

    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready*/
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
        /* If listening descriptor ready, add new client to pool*/
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
			printf("Connected to (%s, %s)\n", client_hostname, client_port);
            add_client(connfd, &pool);
        }
        check_clients(&pool);
    }
    update_stock_file(FILENAME);
    exit(0);
}
/* $end echoserverimain */

void sigint_handler(int signum){
    update_stock_file(FILENAME);
    exit(0);
}

void init_pool(int listenfd, pool *p, char *filename) {
    /*Initially, there are no connected descriptors*/
    int i;
    p->maxi = -1;
    num_stocks = 0;

    for(i=0;i<FD_SETSIZE;i++) 
        p->clientfd[i] = -1;
    
    /*Initially, listenfd is only member of select read set*/
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }
    // printf("success open stock.txt\n");
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
        // printf("id : %d, left_stock : %d, price : %d\n", id, left_stock, price);
        stock_item *item = (struct stock_item*)malloc(sizeof(stock_item));
        item->id = id;
        item->left_stock = left_stock; 
        item->price = price;
        stocks[++num_stocks] = item;
        root = insertItem(root, item);
    }

    fclose(fp);
}

void add_client(int connfd, pool *p) {
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++) {      //Find available slot
        if (p->clientfd[i] < 0) {
            /* Add connected descriptor to the poll */
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);

            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set);

            /* Update max descriptor and pool high water mark */
            if (connfd > p->maxfd)
                p->maxfd = connfd;
            if (i > p->maxi)
                p->maxi = i;
            break;
        }
    }
    if (i == FD_SETSIZE) 
        app_error("add_client error: Too many clients");
}

void check_clients(pool *p) {
    int i, connfd, n;
    char buf[MAXLINE];
    rio_t rio;

    for (i=0;(i<=p->maxi) && (p->nready > 0);i++) {     //nready는 처리해야할 event 개수
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        //If the descriptor is ready, echo a text line from it
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {    
            p->nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
                printf("Server received %d bytes on fd %d\n", n, connfd);
                handle_client_request(connfd, buf);
                // Rio_writen(connfd, buf, MAXLINE);
            }

            //EOF detected, remove descriptor from pool
            else {
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }
        }
    }
}

// void remove_client(int fd)
// {
//     for (int i = 0; i < p->maxi; i++) {
//         if (p->clientfd[i] == fd) {
//             p->clientfd[i] = p->clientfd[p->maxi - 1];
//             p->maxi--;
//             FD_CLR(fd, &p->read_set);
//             Close(fd);
//             break;
//         }
//     }
// }

//기존 echoServer에서 Rio_writen 함수 자리를 대체
//buf를 받아와서 그에 맞는 함수를 호출
void handle_client_request(int fd, char* buf)
{

    char *token = strtok(buf, " ");
    if (token == NULL) {
        return;
    }

    //명령어 분해 후, 명령어에 맞는 함수 호출
    if (strcmp(token, "show\n") == 0) {
        handle_show_request(fd);
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
        handle_buy_request(fd, id, num);
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
        handle_sell_request(fd, id, num);
    } else if (strcmp(token, "exit\n") == 0) {
        // remove_client(p, fd);
    }
        
}

void handle_show_request(int fd)
{
    char temp[MAXLINE];
    char buf[MAXLINE] = "show\n";
    int k = 5;              //앞에다가 show\n 적고나서 다음 문자열은 index6부터 입력
    for (int i = 1; i <= num_stocks; i++) {
        stock_item *item = stocks[i];
        sprintf(temp, "%d %d %d\n", item->id, item->left_stock, item->price);
        strcat(buf, temp);
    }
    Rio_writen(fd, buf, MAXLINE);
}

void handle_buy_request(int connfd, int id, int num) {
    char buf[MAXLINE];
    char temp[MAXLINE];
    sprintf(buf, "buy %d %d\n", id, num);
    stock_item *item = findStockById(id);
    if(item != NULL) {
        if (item->left_stock >= num) {
            item->left_stock -= num;
            sprintf(temp, "[buy] success\n");
            strcat(buf, temp);
        } else {
            sprintf(temp, "Not enough left stock\n");
            strcat(buf, temp);
        }
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
        item->left_stock += num;
        sprintf(temp, "[sell] success\n");
        strcat(buf, temp);
    } else {
            sprintf(temp, "there is no such id\n");
            strcat(buf, temp);
    }
    Rio_writen(connfd, buf, MAXLINE);
}

void update_stock_file(char *filename)
{
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    for (int i = 1; i <= num_stocks; i++) {
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
