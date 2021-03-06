/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "csapp.h"
#include "worker.h"

/*************************************************************************************************
* Function Prototypes
*************************************************************************************************/
void thread_newcreate(int i);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
extern void delay(int spins);

/*************************************************************************************************
* Global Variable Initializations
* Static Initialization of variables of type pthread_mutex_t and pthread_cond_t  
*************************************************************************************************/
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fill,empty =PTHREAD_COND_INITIALIZER;

/*************************************************************************************************
* int main() Function
*************************************************************************************************/

int main(int argc, char **argv)
{
    //Initializations
	int listenfd, *connfd, port, clientlen,main_thread_priority,main_policy;
	int i;
    struct sockaddr_in clientaddr;
    struct sched_param main_thread_param;	
    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }
    port = atoi(argv[1]); //convert string to interger
    /* Set priority of the main thread to one more than the half of the maximum priority with FIFO scheduling */
    main_thread_priority = sched_get_priority_max(SCHED_FIFO); 
    main_thread_param.sched_priority= ((int)(main_thread_priority/2) + 1 ); 	
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &main_thread_param);
    pthread_getschedparam (pthread_self(), &main_policy, &main_thread_param);
    printf(" Main Thread running with priority %d \n", main_thread_param.sched_priority);	

	/* Create three worker threads by calling a user-defined function thread_newcreate() */
    thread_ptr=Calloc(3,sizeof(workers_t));
	for(i=0;i<3;i++)
		thread_newcreate(i);   // main thread returns

    listenfd = Open_listenfd(port);
    while (1) 
	{
		clientlen = sizeof(clientaddr);
		connfd =(int *)Malloc(sizeof(int));	
		*connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
		pthread_mutex_lock(&mutex);           
		printf(" Main Thread running with priority %d \n", main_thread_param.sched_priority);	
		while (count == MAX)                   
			pthread_cond_wait(&empty, &mutex); 
		put(*connfd);
		pthread_cond_signal(&fill);            
		pthread_mutex_unlock(&mutex);   
		free(connfd);
                                          
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);                   //line:netp:doit:readrequest
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);       //line:netp:doit:staticcheck
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    //line:netp:doit:endnotfound

    if (is_static) { /* Serve static content */          
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
}
/* $end doit */

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
	strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
	strcat(filename, uri);                           //line:netp:parseuri:endconvert1
	if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
	    strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
	return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
	ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
	strcat(filename, uri);                           //line:netp:parseuri:endconvert2
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
    Close(srcfd);                           //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
    Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

/* Function to fill the buffer when a new worker thread is created */
void put(int value) {
	buffer[buf_fill] = value;
	buf_fill = (buf_fill + 1) % MAX;
	count++;
}

/* Function to remove a worker thread from buffer when it is processed */
int get() {
	int tmp = buffer[use];
	use = (use + 1) % MAX;
	count--;
	return tmp;
}

/*******************************************************************************
Function Name:  thread_consumer
Description:    Create a set of new worker threads with a lower priority than the main thread.
Arguments:      arg
Return value:   -
*******************************************************************************/
void *thread_consumer(void *arg){
 	int connfd,policy;
	pthread_t w_priority = pthread_self();
	struct sched_param param;
	pthread_getschedparam (w_priority, &policy, &param); //Obtain the scheduling parameters of thread w_priority(self)

    printf("thread %d starting\n with priority %d \n", (int) arg, param.sched_priority);
     	 
		 while(1) 
		 {
			delay(30);
			pthread_mutex_lock(&mutex);  // Acquire lock
         	while (count==0)
            	pthread_cond_wait(&fill,&mutex); // wait for buffer to be filled
				
            printf("thread %d running\n with priority %d \n", (int) arg, param.sched_priority); // show running thread and priority
	        connfd = get();   /* connected socket to service */
            pthread_cond_signal(&empty); // signal manager of consumption	 
       	    pthread_mutex_unlock(&mutex); // Release lock

            doit(connfd);      /* process request */
            Close(connfd);	  // close connection		 line:netp:tiny:close

	     }	
}

/*******************************************************************************
Function Name:  thread_newcreate
Description:    Set scheduling policies and priorities for a set of new worker threads.
Arguments:      i
Return value:   -
*******************************************************************************/
void thread_newcreate(int i) {

	void   *thread_consumer(void *);
	struct sched_param my_param;
	/* Initialize thread attributes object pointed to by worker_attr with default values */
	pthread_attr_t worker_attr;
	pthread_attr_init(&worker_attr);
	
	/* Set the inherit-scheduler, detach state and scope attributes of the worker thread */
	pthread_attr_setinheritsched(&worker_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setdetachstate(&worker_attr,PTHREAD_CREATE_DETACHED);
	pthread_attr_setscope(&worker_attr, PTHREAD_SCOPE_PROCESS);
	
	/* SCHEDULING POLICY AND PRIORITY FOR OTHER THREADS */
	pthread_attr_setschedpolicy(&worker_attr, SCHED_FIFO);        
	my_param.sched_priority = ((int)(sched_get_priority_max(SCHED_FIFO)/2 ) - 1) ; // get max priorty & set priorty to one less than half
	pthread_attr_setschedparam(&worker_attr, &my_param);  
	/* create new consumer threads with attributes defined by worker_attr */		
	Pthread_create(&thread_ptr[i].thread_id, &worker_attr, &thread_consumer, (void *) i);
	
	pthread_attr_destroy(&worker_attr);
	return;                    /* main thread returns */
	}


