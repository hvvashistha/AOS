#ifndef __PROTOCOLS__

#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define __PROTOCOLS__

#define NUL (char)0		//Null
#define SOH (char)1		//Start of Header
#define STX (char)2		//Start of Text
#define ETX (char)3		//End of Text
#define EOT (char)4		//End of Transmission
#define ENQ (char)5		//Enquiry
#define ACK (char)6		//Acknowledge
#define NAK (char)21	//Negative Acknowledge
#define SYN (char)22	//Synchronous Idle
#define ETB (char)23	//End of Transmission Block

/**
Transmission packet protocol:

[ SOH(1) | <message length>(3) | STX(1) | message(<message length>) | ETX(1) | ETB(1) ]
*/

char *pRecv(int socket, char *msg);
int cFormat(char msgCtrl, char buffer[]);
int mFormat(const char *msg, int length, char buffer[]);
int pSend(int socket, const char *msg, int length);

char *sRecv(int socket, char *msg) {
	char buffer[4];
	int msgLength = -1;
	int bytesRecv;

	//Decrypt message packet according to predefined protocol above
	do {
		bytesRecv = recv(socket, buffer, 1, 0);
		if (bytesRecv != -1 && buffer[0] == EOT) {
			*msg = EOT;
			*(msg + 1) = '\0';
		} else if (bytesRecv != -1 && buffer[0] == SOH) {
			if ((bytesRecv = recv(socket, buffer, 3, 0)) != -1) {
				buffer[3] = '\0';
				msgLength = atoi(buffer);
			}
		} else if (bytesRecv != -1 && buffer[0] == STX && msgLength > -1) {
			if ((bytesRecv = recv(socket, msg, msgLength, 0)) != -1) {
				*(msg + msgLength) = '\0';
			}
		} else if (bytesRecv == -1) {
			perror(strerror(errno));
			*msg = '\0';
			break;
		}
		printf("%s\n", msg);
	} while (buffer[0] != ETB);
	printf("f\n");
	return msg;
}

char *uRecv(int socket, char *msg, struct sockaddr *addr) {
	char buffer[255];
	char length[4];
	int len;
	unsigned int sockSize = sizeof(*addr);
	if ((addr ? recvfrom(socket, buffer, 255, 0, addr, &sockSize) : recv(socket, buffer, 255, 0)) > 0) {
		strncpy(length, &buffer[1], 3);
		length[3] = '\0';
		len = atoi(length);
		strncpy(msg, &buffer[5], len);
		*(msg + len) = '\0';
	} else {
		return NULL;
	}
	return msg;
}

//Proxy function for single character send
int cFormat(char msgCtrl, char buffer[]) {
	char temp[2];
	temp[0] = msgCtrl;
	temp[1] = '\0';
	return mFormat(temp, 1, buffer);
}

//Format message into predefined protocol format before sending
int mFormat(const char *msg, int length, char buffer[]) {
	snprintf(buffer, 252, "%c%03d%c%s", SOH, length, STX, msg);
	buffer[length + 5] = ETX;
	buffer[length + 6] = ETB;
	buffer[length + 7] = '\0';
	return length + 7;
}

int sSend(int socket, const char *msg, int length) {
	return send(socket, msg, length + 7, 0);
}

int uSend(int socket, const char *msg, int length, struct sockaddr *addr) {
	return sendto(socket, msg, length, 0, addr, sizeof(*addr));
}

#endif