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
	struct sockaddr_in srv_addr, cli_addr;
	int sockfd, port_num, cwnd, clen = sizeof(cli_addr);
	double pl, pc;
	char *filename;
	struct packet packin, packout;
	
	// get arguments from command line
	if (argc != 5)
		diep("Usage: sender portnumber cwnd ploss pcorrupt");
	port_num = atoi(argv[1]);
	cwnd = atoi(argv[2]);
	pl = atof(argv[3]);
	pc = atof(argv[4]);
	
	// check arguments for errors
	if (cwnd <= 0)
		diep("cwnd must be positive");
	if (port_num < 0)
		diep("portnum must be non-negative");
	if (pl < 0 || pl > 1 || pc < 0 || pc > 1)
		diep("ploss and pcorrupt must be between 0 and 1, inclusive");
	
	// create socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		diep("Error opening socket");

	memset((char *)&srv_addr, 0, clen);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port_num);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	// bind socket to port
	if (bind(sockfd, (struct sockaddr *) &srv_addr, clen) == -1)
		diep("Error binding socket");
	
	srand(time(NULL));
	
	while (1) {
		// process file request
		printf("\nWaiting for file request\n");
		if (recvfrom(sockfd, &packin, sizeof(packin), 0, (struct sockaddr*) &cli_addr, &clen) == -1) {
			printf("Error receiving file request\n");
			continue;
		}
		else if (packin.type != TYPE_REQUEST) {
			printf("Received non-request packet. Ignored\n");
			continue;
		}
		
		filename = packin.data;
		printf("Received file request for \"%s\"\n", filename);
		
		FILE *fp = fopen(filename, "r");
		if (fp == NULL) { // file does not exist
			memset((char*)&packout, 0, sizeof(packout));
			packout.type = TYPE_FIN;
			packout.seq = 0;
			packout.corrupt = 0;
			if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr*)&cli_addr, clen) == -1)
				diep("Error sending FIN packet");
			printf("No such file. Sent FIN\n");
			continue;
		}
		
		// read file into memory buffer
		fseek(fp, 0L, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);
		char *file_buf = malloc(sizeof(char) * file_size);
		fread(file_buf, sizeof(char), file_size, fp);
		
		fclose(fp);

		// total packets needed to be sent
		int total_packets = file_size/DATA_SIZE + (file_size % DATA_SIZE != 0);
		int cur_packet = 0;
		int next_ack = 0;
		int cur_seq = 0;
		int cur_pos = 0;

		while (cur_packet < total_packets) {
			// number of sent packets this cycle
			int sent_packets = 0;
			int cur_cycle_seq = cur_seq;
			int cur_cycle_pos =  cur_pos;
			
			while (sent_packets < cwnd && cur_cycle_pos < file_size) {
				memset((char*)&packout, 0, sizeof(packout));
				packout.type = TYPE_DATA;
				packout.seq = cur_cycle_seq;
				packout.ack = 0;
				packout.corrupt = 0; //(double)rand() / (double)RAND_MAX < pc ? 1 : 0;
				packout.size = (file_size - cur_cycle_pos < DATA_SIZE) ? file_size - cur_cycle_pos : DATA_SIZE;
				memcpy(packout.data, file_buf + cur_cycle_pos, packout.size);
				
				if ( (double)rand() / (double)RAND_MAX < pl)
					printf("Sent packet lost\n");
				else {
					if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr *)&cli_addr, clen) == -1)
						diep("Error sending data packet");
					printf("Sent  (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
							packout.type, packout.seq, packout.ack, packout.size, packout.corrupt);
				}
				
				sent_packets++;
				cur_cycle_seq++;
				cur_cycle_pos += packout.size;
			}
			
			fd_set set;
			struct timeval timeout = {1, 0}; // 1 sec timeout
			int acked_packets = 0;
			
			while (acked_packets < sent_packets) {
				FD_ZERO(&set);
				FD_SET(sockfd, &set);
				if (select(sockfd+1, &set, NULL, NULL, &timeout) < 1) {
					printf("Timed out. Resending packets\n");
					break;
				}
				
				if (recvfrom(sockfd, &packin, sizeof(packin), 0, (struct sockaddr *)&cli_addr, &clen) == -1)
					diep("Error receiving ACK packet");
				else if (packin.corrupt) {
					printf("Received corrupt packet. Ignored\n");
					continue;
				}
				else if (packin.type != TYPE_ACK) {
					printf("Received non-ack packet. Ignored\n");
					continue;
				}
				else if (packin.ack >= next_ack) {
					// cur_packet += (packin.ack - next_ack + 1);
					// cur_seq += (packin.ack - next_ack + 1);
					// cur_pos += ((packin.ack - next_ack + 1)*DATA_SIZE);
					// acked_packets += (packin.ack - next_ack + 1);
					// next_ack = packin.ack + 1;
					cur_packet++;
					next_ack++;
					cur_seq++;
					cur_pos += DATA_SIZE;
					acked_packets++;
					printf("Recvd (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
							packin.type, packin.seq, packin.ack, packin.size, packin.corrupt);
				}
				else
					printf("Received packet with ack %d. Expected %d. Ignored\n", packin.ack, next_ack);
				// did not receive next ACK, keep waiting
			}
		}
		// all data packets sent, send FIN
		memset((char*)&packout, 0, sizeof(packout));
		packout.type = TYPE_FIN;
		packout.seq = cur_seq;
		packout.ack = 0;
		packout.corrupt = 0;
		packout.size = 0;
		if (sendto(sockfd, &packout, sizeof(packout), 0, (struct sockaddr *)&cli_addr, clen) == -1)
			diep("Error sending FIN packet");
		printf("Sent  (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
				packout.type, packout.seq, packout.ack, packout.size, packout.corrupt);
		
		// wait for FIN ACK
		if (recvfrom(sockfd, &packin, sizeof(packin), 0, (struct sockaddr *)&cli_addr, &clen) == -1)
			diep("Error receiving ACK packet");
		printf("Recvd (type: %d, seq: %d, ack: %d, size: %d, corrupt: %d)\n",
				packin.type, packin.seq, packin.ack, packin.size, packin.corrupt);
				
		printf("Connection closed\n");
		free(file_buf);
	}

	close(sockfd);
	return 0;
}
