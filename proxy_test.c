//gcc -g -o proxy_test proxy_test.c -I/usr/local/include/mysql -L/usr/local/lib/mysql -lmysqlclient -lpthread
//./proxy_test host port username password databse base_file threads
//./proxy_test 10.232.64.74 5300 rcmd_tmall 123456 rcmd_tmall rcmd 19
/*
 *host:where connect mysql server ip
 *port:mysql server port
 *username:connect mysql server uesr
 *password:connect mysql server password for username
 *database:use database
 *base_file:the prefix of all data file that will insert to mysql;
 *          such as:all data file rcmd_0,rcmd_1,rcmd_2,...then the base_file is rcmd
 *          and request all data file postfix must from 0 start, and sequential
 *threads:thread num to connect to mysql and parallel query mysql;
 *        each thread oneself read one datafile that postfix is the thread be create order
 *        example:the first thread read rcmd_0 datafile
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mysql.h"

#define BUFF_SIZE 10485760

void * process_conn(void *);

char host[16];
int port;
char username[256];
char passwd[256];
char database[256];
char basefile[256];

/*return:
 * -1: error
 *  other: read bytes, while < len, say read to EOF
 * */
block_comp_read(int fd, char *buf, int len){
    int  ret;
    int temp = len;
    while(len != 0 && (ret = read(fd, buf, len)) != 0) {
        if (ret == -1){
            if(errno == EINTR)
                continue;
            fprintf(stderr, "read file failed for:%s\n", strerror(errno));
            break;
        }
        
        len -= ret;
        buf += ret;
    }

    if(ret == -1)
        return -1;

    return temp - len; 
}

/*
 * this may be have a question, when one record more than buff half
 * */
int move_left_to_head(char *buff, int start, int end){
    if(end-start)
    {
        memcpy(buff, buff+start, end-start);
    } 
    
    return end-start;

} 

int send_sql(MYSQL *conn, char *str){
    /* send SQL query */
    if (mysql_query(conn, str)) {
        fprintf(stderr, "send sql(%s) failed %s\n", str, mysql_error(conn));
        return -1;
    }

    //res = mysql_use_result(conn);

    ///* output table name */
    //printf("MySQL Tables in mysql database:\n");
    //while ((row = mysql_fetch_row(res)) != NULL)
    //   printf("%s \n", row[0]);

    //mysql_free_result(res);
    //
    //sleep(2);

    //if(mysql_ping(conn)){
    //    fprintf(stderr, "%s\n", mysql_error(conn));
    //    mysql_close(conn);
    //    exit(1);
    //}

    return 0;
}

//./proxy_test 10.232.64.75 5300 sample test sample filename 10
int main(int argc, char *argv[])
{
    unsigned int serverport;
    unsigned short threads;
    pthread_t *tids;
    int *sqls = NULL; 
    int sum_sqls=0;
    long i;

    pthread_t test;

    if (argc < 8)
    {  
        fprintf(stderr,"Please enter the server's hostname port username passwd dbname basefile threads\n");
        exit(1);
    }

    if((serverport = atoi(argv[2])) <= 0){
        perror("server port error!");
        exit(1);
    }

    threads=atoi(argv[7]);
    if(threads <= 0)
    {
        printf("threads num must more than 0\n");
        return -3;
    }

    tids = (pthread_t*)malloc(sizeof(pthread_t)*threads);
    if(tids == NULL)
        exit(1);

    strcpy(host, argv[1]);
    port = serverport;
    strcpy(username, argv[3]);
    strcpy(passwd, argv[4]);
    strcpy(database, argv[5]);
    strcpy(basefile, argv[6]);

    printf("connect to:host(%s) port(%d) username(%s) passwd(%s) database(%s) basefile(%s)\n", \
            host, port, username, passwd, database, basefile);

    for(i = 0; i < threads; i++)
    {  
        if(pthread_create(&tids[i], NULL, process_conn, (void *)i) !=0 )
        {  
            tids[i] = (pthread_t) NULL;
            fprintf(stderr, "Create Thread[%d] Error:%s/n/a", i, strerror(errno));
        }
    }

    printf("create %d threads\n", i);    

    for(i = 0; i < threads; i++)
    {  
        if(tids[i] != (pthread_t)NULL && pthread_join(tids[i], (void **)&sqls)!=0)
            fprintf(stderr,"Thread[%d] Join Error:%s/n/a", i, strerror(errno));
        else
        {  
            if(sqls == NULL)
                continue;
            printf("Thread[%d] send %d sqls\n", i, *sqls);                                                                                                                       
            sum_sqls += *sqls;
            free(sqls);
        }
    }

    printf("sum sqls %d\n", sum_sqls);

    free(tids);
    
    return 0;
}  

void * process_conn(void *argv)
{
    long index;
    int num=0;
    int fd, *sqls;
    char filename[256];
    char *buff=NULL;
    int len;
    int ret;
    int offset;
    char sql_str[1024];

    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    buff = (char *)malloc(sizeof(char) * BUFF_SIZE);
    if(buff == NULL){
        fprintf(stderr, "not enough memory for buff\n");
        pthread_exit(NULL);
    }

    index = (long) argv;
    sprintf(filename, "%s_%ld.sql", basefile, index);

    fd=open(filename, O_RDONLY);
    if(fd < 0)
    {  
        printf("open file failed and fd=%d\n", fd);
        pthread_exit(NULL);
    }

    sqls = (int *)malloc(sizeof(int));
    if(sqls == NULL)
    {   
        close(fd);
        free(buff);
        pthread_exit(NULL);
    }

    printf("open file %s ok\n", filename);
    conn = mysql_init(NULL); 

    // Connect to database 
    if (!mysql_real_connect(conn, host, username, passwd, database, port, NULL, 0)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        close(fd);
        free(sqls);
        free(buff);
        pthread_exit(NULL);
    }

    printf("thread[%ld] connect to mysql ok\n", index); 

    len = BUFF_SIZE;
    offset = 0;
    while((ret=block_comp_read(fd, buff+offset, len)) != -1) {
        int start = 0;
        int j;
        int all = ret + offset; //add last rest

        if(ret == 0) break;

        for(j = start; j < all; j++){
            if (buff[j] != '\n') continue;

            buff[j] = '\0'; // a line with '\0' replace end, and can direct send to mysql

            gen_sql(sql_str, buff+start); // generate sql string, if the line is not mysql string
            if( send_sql(conn, buff+start) == 0) // send sql string to mysql
                num++;

            start = j+1;
        }    
        
        if(ret < len)
            break;

        // move rest non one line to buff head
        offset = move_left_to_head(buff, start, all);        

        len = BUFF_SIZE - offset; //reduce read buff size: sub rest length
    }

    *sqls=num;
    close(fd);
    free(buff);
    mysql_close(conn);
    
    if(ret == -1)
        pthread_exit(NULL);
         
    pthread_exit(sqls); 
}

/*
 * if you datafile is sql string, then direct return;
 * else you must code to construct sql string from data record
 * buff: a line string, in datafile with '\0' end 
 */
int gen_sql(char *dest, char *buff){
    return 0;    
}

