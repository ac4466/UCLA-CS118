#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <time.h>

#include "packet.h"

void diep(char *s)
{
	fprintf(stderr, s);
	fprintf(stderr, "\n");
	exit(1);
}

int main(int argc, char* argv[])
{
	struct sockaddr_in srv_addr;
	struct hostent *srvent;
	int sockfd, port_num, slen = sizeof(srv_addr);
	char *hostname, *filename;
	double pl, pc;
	struct packet packin, packout;

	// get arguments from command line
	if (argc != 6)
		diep("Usage: receiver hostname portnumber filename ploss pcorrupt");
	hostname = argv[1];
	port_num = atoi(argv[2]);
	filename = argv[3];
	pl = atof(argv[4]);
	pc = atof(argv[5]);

	// check arguments for errors
	srvent = (struct hostent*)gethostbyname(hostname);
	if (!srvent)
		diep("Invalid hostname");
	if (port_num < 0)
		diep("portnum must be non-negative");
	if (pl < 0 || pl > 1 || pc < 0 || pc > 1)
		diep("ploss and pcorrupt must be between 0 and 1, inclusive");
	
	// create socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		diep("Error opening socket");

	memset((char*)&srv_addr, 0, slen);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port_num);
	memcpy((char*) &srv_addr.sin_addr.s_addr, (char*) srvent->h_addr, srvent->h_length);
	
	// construct request message
	memset((char*)&packout, 0, sizeof(packout));
	packout.type = TYPE_REQUEST;
	packout.seq = 0;
	packout.ack = 0;
	packout.corrupt = 0;
	packout.size = strlen(filename);
	memcpy(packout.data, filename, packout.size);
	
	// send request message
	if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr*)&srv_addr, slen) == -1)
		diep("Error sending file request message");
	printf("Request sent... Awaiting file\n");
	
	// open file for writing
	char n_filename[DATA_SIZE];
	strcpy(n_filename, "n_");
	strcat(n_filename, filename);
	FILE *fp = fopen(n_filename, "w+");
	if (fp == NULL)
		diep("Error opening file for writing");
	
	// prepare to send ACKs
	srand(time(NULL));
	
	memset((char*)&packout, 0, sizeof(packout));
	packout.type = TYPE_ACK;
	packout.seq = 0;
	packout.ack = 0;
	packout.size = 0;
	packout.corrupt = 0;
	
	int next_seq = 0;
	
	while (1) {
		memset((char*)&packin, 0, sizeof(packin));
		if (recvfrom(sockfd, &packin, sizeof(packin), 0, (struct sockaddr *)&srv_addr, &slen) == -1) {
			fclose(fp);
			diep("Error receiving file");
		}
		else if (packin.corrupt) { // corrupt packet
			printf("Received corrupt packet. Ignored\n");
			continue;
		}
		else if (packin.seq == 0 && packin.type == TYPE_FIN) { // file does not exist
			fclose(fp);
			diep("No such file");
		}
		else if (packin.seq > next_seq) {
			printf("Received packet with seq %d. Expected %d. Ignored\n", packin.seq, next_seq);
			continue;
		}
		else if (packin.seq < next_seq) {
			packout.ack = packin.seq;
			printf("Received packet with seq %d. Expected %d. Resent ACK %d\n",
					packin.seq, next_seq, packout.ack);
		}
		// at this point, we know packin.seq is correct next_seq
		// checking validity
		else if (packin.type != TYPE_DATA) {
			if (packin.type == TYPE_FIN) {
				printf("Recvd (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
					packin.type, packin.seq, packin.ack, packin.size, packin.corrupt);
				break;
			}
			else {
				printf("Received non-data packet. Ignored\n");
				continue;
			}
		}
		// received valid data packet
		else {
			fwrite(packin.data, sizeof(char), packin.size, fp);
			packout.ack = packin.seq;
			packout.corrupt = 0 ;//(double)rand() / (double)RAND_MAX < pc ? 1 : 0;
			next_seq++;
			printf("Recvd (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
				packin.type, packin.seq, packin.ack, packin.size, packin.corrupt);
		}
		
		if ( (double)rand() / (double)RAND_MAX < pl) {
			printf("Sent packet lost\n");
			continue;
		}
		
		if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr *)&srv_addr, slen) == -1)
			diep("Error sending ACK packet");
		printf("Sent  (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
				packout.type, packout.seq, packout.ack, packout.size, packout.corrupt);
	}

	// send FIN ACK
	packout.type = TYPE_FIN;
	packout.seq = 0;
	packout.ack = packin.seq;
	packout.size = 0;
	packout.corrupt = 0;
	if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr *)&srv_addr, slen) == -1)
		diep("Error sending FIN ACK packet");
	printf("Sent  (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
			packout.type, packout.seq, packout.ack, packout.size, packout.corrupt);
	
	printf("Connection closed\n");
	
	fclose(fp);
	close(sockfd);
	return 0;
}
