#ifndef PACKET_H
#define PACKET_H

#define DATA_SIZE 1024
#define PACKET_SIZE 1040

#define TYPE_REQUEST 1
#define TYPE_DATA 2
#define TYPE_ACK 3
#define TYPE_FIN 4

struct packet {
	short type; // 1: Request, 2: Data, 3: ACK, 4: FIN
	int seq;
	int ack;
	int size; 
	short corrupt;
	char data[DATA_SIZE];
};

#endif 