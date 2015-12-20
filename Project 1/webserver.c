/*	A simple web server
	Used the CS118's demo server as skeleton code
	Authors: Aaron Cheng and Guan Zhou
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/wait.h>	// for the waitpid() system call
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <signal.h>	// signal name macros, and the kill() prototype

#define ERROR_404_MESSAGE "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H1>404 Not Found</H1><P>The requested file was not found on this server.</P></BODY></HTML>"

typedef enum { code_200, code_404 } status_code;

/* Function Prototypes */
void handle_connection(int);
char *parse_request(char *);
void send_response(int, char *);
void send_header(int, char *, int, status_code);

void error(char *msg)
{
    perror(msg);
    exit(1);
}

void sigchld_handler(int s)
{
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;          // for signal SIGCHLD

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) & serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5);

    clilen = sizeof(cli_addr);

    /****** Kill Zombie Processes ******/
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    /*********************************/

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0)
            error("ERROR on accept");

        pid = fork(); //create a new process
        if (pid < 0)
            error("ERROR on fork");

        if (pid == 0)  { // fork() returns a value of 0 to the child process
            close(sockfd);
            handle_connection(newsockfd);
            exit(0);
        }
        else //returns the process ID of the child process to the parent
            close(newsockfd); // parent doesn't need this 
    } /* end of while */
    return 0; /* we never get here */
}

/******** HANDLE_CONNECTION() *********************
 There is a separate instance of this function
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/
void handle_connection(int sock)
{
    int n;
    char buffer[1024];
    bzero(buffer, 1024);

    // Dump HTTP Request Message to console
    n = read(sock, buffer, 1023);
    if (n < 0)
        error("ERROR reading from socket");
    printf("HTTP Request Message:\n%s\n", buffer);

    char *file_name = parse_request(buffer);

    // Send HTTP Response Message
    send_response(sock, file_name);
}

//parse request message to retrieve file name
char *parse_request(char *message)
{
    char *file_name;
    const char delim[1] = " ";

    //call strtok() twice to get the second token, which is the file name
    file_name = strtok(message, delim);
    file_name = strtok(NULL, delim);

    //to remove the leading '/'
    file_name++;

    return file_name;
}

//request message referred from textbook page 105-106
void send_response(int socket, char *file_name)
{
    //if file name is empty, return
    if (*file_name == '\0')
    {
        send_header(socket, file_name, 0, code_404);
        send(socket, ERROR_404_MESSAGE, strlen(ERROR_404_MESSAGE), 0);
        return;
    }

    FILE *fp = fopen(file_name, "r");

    //if file not found, return
    if (!fp)
    {
        send_header(socket, file_name, 0, code_404);
        send(socket, ERROR_404_MESSAGE, strlen(ERROR_404_MESSAGE), 0);
        return;
    }

    if (!fseek(fp, 0, SEEK_END))
    {
        long file_len = ftell(fp);

        //if file length error, return
        if (file_len < 0)
        {
            send_header(socket, file_name, 0, code_404);
            send(socket, ERROR_404_MESSAGE, strlen(ERROR_404_MESSAGE), 0);
            return;
        }

        char *file_buffer = malloc(sizeof(char) * (file_len + 1));
        fseek(fp, 0, SEEK_SET);

        size_t num_bytes = fread(file_buffer, sizeof(char), file_len, fp);
        file_buffer[num_bytes] = '\0';

        send_header(socket, file_name, num_bytes, code_200);

        //send file to socket
        send(socket, file_buffer, num_bytes, 0);

        free(file_buffer);
    }

    fclose(fp);
}

void send_header(int socket, char *file_name, int file_length, status_code sc)
{

    //status section
    char *status;
    if (sc == code_200)
        status = "HTTP/1.1 200 OK\r\n";
    else if (sc == code_404)
        status = "HTTP/1.1 404 Not Found\r\n";

    //connection section
    char *connection = "Connection: close\r\n";

    //date section
    time_t raw_time;
    time(&raw_time);
    struct tm *time_info = localtime(&raw_time);
    char current_time[32];
    strftime(current_time, 32, "%a, %d %b %Y %T %Z", time_info);
    char date[64] = "Date: ";
    strcat(date, current_time);
    strcat(date, "\r\n");

    //server section
    char *server = "Server: Aaron/1.0\r\n";

    //last-modified section
    struct stat attr;
    stat(file_name, &attr);
    //st_mtime gives the modification time
    struct tm* lm_info = localtime(&(attr.st_mtime));
    char lm_time[32];
    strftime(lm_time, 32, "%a, %d %b %Y %T %Z", lm_info);
    char last_modified[64] = "Last-Modified: ";
    strcat(last_modified, lm_time);
    strcat(last_modified, "\r\n");

    //content-length section
    char content_length[50] = "Content-Length: ";
    char len_buffer[16];
    sprintf(len_buffer, "%d", file_length);
    strcat(content_length, len_buffer);
    strcat(content_length, "\r\n");

    //content-type section
    char *dot;
    char *content_type = "Content-Type: text/html\r\n";
    if ((dot = strrchr(file_name, '.')) != NULL)
        if (strcasecmp(dot, ".jpeg") == 0)
            content_type = "Content-Type: image/jpeg\r\n";
    if ((dot = strrchr(file_name, '.')) != NULL)
        if (strcasecmp(dot, ".gif") == 0)
            content_type = "Content-Type: image/gif\r\n";

    //copy header sections to accumulator array
    char hdr_section[6][50];
    strcpy(hdr_section[0], connection);
    strcpy(hdr_section[1], date);
    strcpy(hdr_section[2], server);
    strcpy(hdr_section[3], last_modified);
    strcpy(hdr_section[4], content_length);
    strcpy(hdr_section[5], content_type);

    //construct the message
    char msg[1024];

    //copy status section
    int position = strlen(status);
    memcpy(msg, status, position);

    //copy other sections
    int i;
    for (i = 0; i < 6; i++)
    {
        if (sc == code_200 || i == 1 || i == 2 || i == 5)
        {
            memcpy(msg + position, hdr_section[i], strlen(hdr_section[i]));
            position += strlen(hdr_section[i]);
        }
    }

    memcpy(msg + position, "\r\n\0", 3);

    //send and print completed response message
    send(socket, msg, strlen(msg), 0);
    printf("HTTP Response Message:\n%s\n", msg);
}
