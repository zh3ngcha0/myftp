#include "server.h"


int main(int argc, char *argv[])
{	
	int sock_listen, sock_control, port, pid;

	if (argc != 2) {
		printf("usage: ./ftserve port\n");
		exit(0);
	}

	port = atoi(argv[1]);

	// create socket
	if ((sock_listen = socket_create(port)) < 0 ) {
		perror("Error creating socket");
		exit(1);
	}		
	
	while(1) {	// wait for client request

		// create new socket for control connection
		if ((sock_control = socket_accept(sock_listen))	< 0 )
			break;			
		
		// create child process to do actual file transfer
		if ((pid = fork()) < 0) { 
			perror("Error forking child process");
		} else if (pid == 0) { 
			close(sock_listen);
			ftserve_process(sock_control);		
			close(sock_control);
			exit(0);
		}
			
		close(sock_control);
	}

	close(sock_listen);	

	return 0;
}



/**
 * Send file specified in filename over data connection, sending
 * control message over control connection
 * Handles case of null or invalid filename
 */
void ftserve_retr(int sock_control, int sock_data, char* filename)
{	
	FILE* fd = NULL;
	char data[MAXSIZE];
	size_t num_read;							
		
	fd = fopen(filename, "r");
	
	if (!fd) {	
		// send error code (550 Requested action not taken)
		send_response(sock_control, 550);
		
	} else {	
		// send okay (150 File status okay)
		send_response(sock_control, 150);
	
		do {
			num_read = fread(data, 1, MAXSIZE, fd);

			if (num_read < 0) {
				printf("error in fread()\n");
			}

			// send block
			if (send(sock_data, data, num_read, 0) < 0)
				perror("error sending file\n");

		} while (num_read > 0);													
			
		// send message: 226: closing conn, file transfer successful
		send_response(sock_control, 226);

		fclose(fd);
	}
}





/**
 * Send list of files in current directory
 * over data connection
 * Return -1 on error, 0 on success
 */
int ftserve_list(int sock_data, int sock_control)
{
	char data[MAXSIZE];
	size_t num_read;									
	FILE* fd;

	int rs = system("ls -l > tmp.txt");
	if ( rs < 0) {
		exit(1);
	}
	
	fd = fopen("tmp.txt", "r");	
	if (!fd) {
		exit(1);
	}

	/* Seek to the beginning of the file */
	fseek(fd, SEEK_SET, 0);

	send_response(sock_control, MSG_DATA_START); //starting

	memset(data, 0, MAXSIZE);
	while ((num_read = fread(data, 1, MAXSIZE, fd)) > 0) {
		if (send(sock_data, data, num_read, 0) < 0) {
			perror("err");
		}
		memset(data, 0, MAXSIZE);
	}

	fclose(fd);

	send_response(sock_control, MSG_DATA_END);	// send 226

	return 0;	
}






/**
 * Open data connection to client 
 * Returns: socket for data connection
 * or -1 on error
 */
int ftserve_start_data_conn(int sock_control)
{
	char buf[1024];	
	int wait, sock_data;

	// Wait for go-ahead on control conn
	if (recv(sock_control, &wait, sizeof wait, 0) < 0 ) {
		perror("Error while waiting");
		return -1;
	}

	// Get client address
	struct sockaddr_in client_addr;
	socklen_t len = sizeof client_addr;
	getpeername(sock_control, (struct sockaddr*)&client_addr, &len);
	inet_ntop(AF_INET, &client_addr.sin_addr, buf, sizeof(buf));

	// Initiate data connection with client
	if ((sock_data = socket_connect(CLIENT_PORT_ID, buf)) < 0)
		return -1;

	return sock_data;		
}


/**
 * Wait for command from client and
 * send response
 * Returns response code
 */
int ftserve_recv_cmd(int sock_control, char*cmd, char*arg)
{	
	int rc = 200;
    data_msg_type_t data_msg;
	char buffer[MAXSIZE];
 
    memset(&data_msg, 0, sizeof(data_msg));
	memset(buffer, 0, MAXSIZE);
	memset(cmd, 0, 5);
	memset(arg, 0, MAXSIZE);
		
	// Wait to recieve command
	if ((recv_data(sock_control, (void*)&data_msg, sizeof(data_msg)) ) == -1) {
		perror("recv error\n"); 
		return -1;
	}

    memcpy(buffer, &data_msg.cmd_data, ntohl(data_msg.len));  // get payload  
	strncpy(cmd, buffer, 4);
	char *tmp = buffer + 5;
	strcpy(arg, tmp);
	
	if (strcmp(cmd, "QUIT")==0) {
		rc = MSG_QUIT;
	} else if((strcmp(cmd, "LIST")==0) || (strcmp(cmd, "RETR")==0)) {
		rc = MSG_CMD;
	} else { //invalid command
		rc = MSG_INVALID;
	}

	send_response(sock_control, rc);	
	return rc;
}






/** 
 * Child process handles connection to client
 */
void ftserve_process(int sock_control)
{
	int sock_data;
	char cmd[5];
	char arg[MAXSIZE];

	// Send welcome message
	send_response(sock_control, CONNECT_OK);
	
	while (1) {
		// Wait for command
		int rc = ftserve_recv_cmd(sock_control, cmd, arg);
		
		if ((rc <= MIN_TYPE) || (rc == MSG_INVALID)) {
			break;
		}
		
		if (rc == MSG_CMD ) {
			// Open data connection with client
			if ((sock_data = ftserve_start_data_conn(sock_control)) < 0) {
				close(sock_control);
				exit(1); 
			}

			// Execute command
			if (strcmp(cmd, "LIST")==0) { // Do list
				ftserve_list(sock_data, sock_control);
			} else if (strcmp(cmd, "RETR")==0) { // Do get <filename>
				ftserve_retr(sock_control, sock_data, arg);
			}
		
			// Close data connection
			close(sock_data);
		} 
	}
}


