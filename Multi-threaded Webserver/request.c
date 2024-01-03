#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)

//Using Queue DS
struct fileholdinf {
	int fd;
	char filename[MAXBUF];
	int filesize;
};

// A queue buffer
//i am using a queue DS which stores all file information stated in fileholdinf above
//
struct fileholdinf buffer_q_file[DEFAULT_BUFFER_SIZE];
int front = 0, rear = -1;

// A method to check if the buffer is empty
int isEmpty() {
	return rear == -1;
}

// A method to check if the buffer is full
int isFull() {
	return rear == DEFAULT_BUFFER_SIZE-1;
}

// A method to enqueuefile to the buffer
//it inserts one fileholdinf stuctre into buffer array i.e Queue
void enqueuefile(int fd, char *filename, int filesize) {
  if(!isFull()) {
	++rear;
    buffer_q_file[rear].fd = fd;
	buffer_q_file[rear].filesize = filesize;
	strcpy(buffer_q_file[rear].filename, filename);
  }
}

// A method to dequeuefile from the buffer
//converse to above
struct fileholdinf dequeuefile() {
  if(!isEmpty()) {
    struct fileholdinf ret_val = buffer_q_file[front];
    for(int i=front; i<rear; i++) {
		buffer_q_file[i] = buffer_q_file[i+1];
    }
	rear--;
	return ret_val;
  }
}





//Mutex Locke and condition Variable Utlisation 
//we are using 2 condition variables i.e one for producer condiution and other for producer condition
//producer are the ones who are going to enques request of ques 
//consumer are the ones who are going to dequeue request from ques
//when producer is in sleep i.e mainthread the only consmer thred will wake it up and vice versa

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t producer = PTHREAD_COND_INITIALIZER, consumer = PTHREAD_COND_INITIALIZER;

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>OSTEP WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
		readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
		strcpy(filetype, "image/jpeg");
    else 
		strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread logic)
//
void* thread_request_serve_static(void* arg)
{
	// TODO: write code to actualy respond to HTTP requests
	//in server therad pool is created and no of threads is given by user and those args are passed here its a consumer
	//it tires to take request and invoke request_serve_static func
	pthread_mutex_lock(&mutex); 
	//lock the code
	while(isEmpty()) {
		//if empty wait else deque implemented below
		pthread_cond_wait(&consumer, &mutex);
	}
	struct fileholdinf fi = dequeuefile();//deque 
	pthread_cond_broadcast(&producer);//awaking producers just in case if they are slept as ques is full
	request_serve_static(fi.fd, fi.filename, fi.filesize);
	pthread_mutex_unlock(&mutex);//unlock
	printf("Request for %s is removed from the buffer.\n", fi.filename);
	return NULL;
}

//
// Initial handling of the request
//This will be the first thing going on in the code after made a reqest
//
void request_handle(int fd, int sched) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
	// get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

	// verify if the request type is GET or not
    if (strcasecmp(method, "GET")) {
		request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
		return;
    }
    request_read_headers(fd);
    
	// check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
	// get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
		request_error(fd, filename, "404", "Not found", "server could not find this file");
		return;
    }
    
	// verify if requested content is static
    if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}

		// we are cheking for .. string if it present then it is forbidden to maintain security not allowing accesing files 
		// outside the server directory (Security Implmentation )
		if(strstr(filename, "..") !=  NULL) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}
		//aftfer that we are checking fore poosible schdulking policy 0 for Fifo and 1 for SFF

	
		// FIFO Scheduling Policy
		if(sched == 0) {
			//its easy to implement cause you just have to follow the order naively no need to bother about which to be done first
			pthread_mutex_lock(&mutex);
			//you lock the thread
			while(isFull()) {
				pthread_cond_wait(&producer, &mutex);
				//if full sleep the thread
			}
			enqueuefile(fd, filename, sbuf.st_size);
			pthread_cond_broadcast(&consumer);
			//send signal to wake up al consumer therads or you can do that for only one i am sending signal to wake up all
			pthread_mutex_unlock(&mutex);	
		}
		// SFF Scheduling Policy
		else {
			//Its unlike FIFO its smallest file first
			pthread_mutex_lock(&mutex);
			while(isFull()) {
				pthread_cond_wait(&producer, &mutex);//if full wait
			}
			if(isEmpty()) {
				enqueuefile(fd, filename, sbuf.st_size);
				//if ques is empty then no compariuson is required you can enque 
			}
			else {
				//if not then apply insertion sort logic
				enqueuefile(fd, filename, sbuf.st_size);
				int i=rear-1;
				while(i>=0 && buffer_q_file[i].filesize > sbuf.st_size) {
					buffer_q_file[i+1].fd = buffer_q_file[i].fd;
					buffer_q_file[i+1].filesize = buffer_q_file[i].filesize;
					strcpy(buffer_q_file[i+1].filename, buffer_q_file[i].filename);
					i--;
				}
				buffer_q_file[i+1].fd = fd;
				buffer_q_file[i+1].filesize = sbuf.st_size;
				strcpy(buffer_q_file[i+1].filename, filename);
			}
			pthread_cond_signal(&consumer);
			//after vthis consumer is given a wake up call
			pthread_mutex_unlock(&mutex);
			//then lock is opened
		}
		printf("Request for %s is added to the buffer.\n", filename);
    } else {
		request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
