#include "sv_node.h"

extern char *node_inst_id;		// stores the instance of a node
extern char hostname[MAXLINE];

void *server_t(void *threaddata)
{
	int new_fd;
	int numbytes =0;
	thread_data *my_data;
	my_data = (thread_data *)threaddata;
	new_fd = my_data->new_fd;
	int i, thread_num = -1;
	char *buffer;
	//bool exit_flag = false;
	int his_port;
	char log_data[MAXLINE];
	memset(log_data, '\0', MAXLINE);

	msg_header Header;
	Header = my_data->Header;
	buffer = SetBuffer(Header.data_length);
	numbytes = 0;
	// then receive the remaining part

	if((numbytes = recv(new_fd, buffer, Header.data_length, 0)) == -1)	{
		//perror("recv");
		kill_threads();
		pthread_exit(NULL);
	}

	hello_data hello_msg_data;
	memcpy(&hello_msg_data.host_port, &buffer[0], 4);
	hello_msg_data.hostname = SetBuffer(Header.data_length-4);
	memcpy(&hello_msg_data.hostname[0], &buffer[4], (Header.data_length-4));
//	printf("Server: HLLO Recd: port %d, name %s\n", hello_msg_data.host_port,
//											hello_msg_data.hostname);
	free(buffer);
	his_port = hello_msg_data.host_port;

	memset(log_data, '\0', MAXLINE);
	sprintf(log_data, "%d %s", 
		hello_msg_data.host_port, hello_msg_data.hostname);
	write_log('r', his_port, Header, log_data);
	
	// make a Message header variable to store the header
	Header.msg_type = HLLO;
	GetUOID(node_inst_id, "msg", Header.UOID, sizeof Header.UOID);
	Header.TTL = 1;		// for hello messge
	Header.reserved = 0;	// always
	Header.data_length = sizeof(int) + strlen(hostname);

	buffer = SetBuffer(HEADER_LEN);
	memcpy(&buffer[0], &Header.msg_type, 1);
	memcpy(&buffer[1], &Header.UOID[0], 20);
	memcpy(&buffer[21], &Header.TTL, 1);
	memcpy(&buffer[22], &Header.reserved, 1);
	memcpy(&buffer[23], &Header.data_length, 4);
	// send the header of the HELLO message
	if((numbytes = send(new_fd, buffer, HEADER_LEN, 0)) == -1)	{
		//perror("send");
		exit(1);
	}
	free(buffer);

	// now send the remaining part of the HELLO message
	hello_msg_data.host_port = Port;
	hello_msg_data.hostname = SetBuffer(strlen(hostname));
	strcpy(hello_msg_data.hostname, hostname);
	buffer = SetBuffer(sizeof(int) + strlen(hello_msg_data.hostname));
	memcpy(&buffer[0], &hello_msg_data.host_port, 4);
	memcpy(&buffer[4], &hello_msg_data.hostname[0], strlen(hello_msg_data.hostname));
	if((numbytes = send(new_fd, buffer, Header.data_length, 0)) == -1)	{
		//perror("send");
		exit(1);
	}
	free(buffer);

	bool isHeBeacon = false;
	for(i=0; i<beacon_count; i++)
		if(his_port == beacon_port[i])	{
			isHeBeacon = true;
			break;
		}	

	if(my_node_type != BEACON || !isHeBeacon)	{
		memset(log_data, '\0', MAXLINE);
		sprintf(log_data, "%d %s", 
			hello_msg_data.host_port, hello_msg_data.hostname);
		write_log('s', his_port, Header, log_data);
	}
	//printf("server sent %s to %d\n", buf, his_port);

	// now check wether this connection will be kept or not.
	// thread has both his port number and other's port number
	// compare them and if my_port > his_port, keep the connection
	// else exit.

	// this should be done only if
	//	1. the node is a beacon &
	//	2. the connecting node is also a beacon
	if(Port < his_port && isHeBeacon == true && my_node_type == BEACON)	{
		//printf("server exits\n");
		fflush(stdout);
		close(new_fd);
		pthread_exit(NULL);
	}

	//------------------------------------------------------------------
	//	EVERYTHING BELOW THIS LINE IS COMMON TO
	//			client_part.cc AND server_part.cc
	//	COPY ALL THE CHANGES IN BOTH THE FILES
	//	sockfd should be replaced with new_fd in server_part.cc
	//------------------------------------------------------------------

	// now this thread will continue
	// A keepalive message will be sent after timeout
	int retval = 0;	// return value of the select function
	struct timeval tv;
	tv.tv_sec = 1;	// check all the timeout values after every 3 seconds
	tv.tv_usec = 0;

	// now set all the values of timeouts
	pthread_mutex_lock(&timout);
	for(i=0; i<FD_SETSIZE; i++)	{
		if(active_conn[i] == -1)	{
			active_conn[i] = his_port;
							// fill with my neighbor's port number
			break;
		}
	}
	num_neighbor++;	// total neighbors of this node (currently active)
	thread_num = i;	// this is the thread's index in the array

	timeout_join[thread_num] = JoinTimeout;
	timeout_alive[thread_num] = KeepAliveTimeout/2;
	timeout_node_dead[thread_num] = KeepAliveTimeout;
	pthread_mutex_unlock(&timout);

	int saved_timeout_alive;
	int saved_timeout_node_dead;
	int saved_check_timeout;
	event fwd_event;	// take an event to forward to the neighbor
	fd_set rfds;

	// The thread will be in an infinite loop
	while(1)	{
		FD_ZERO(&rfds);
		FD_SET(new_fd, &rfds);
		tv.tv_sec = 1;
		retval = select(new_fd+1, &rfds, NULL, NULL, &tv);
		
		// save the timeout values in local variables
		pthread_mutex_lock(&timout);
		saved_timeout_alive = timeout_alive[thread_num];
		saved_timeout_node_dead = timeout_node_dead[thread_num];
		saved_check_timeout = timeout_check;
		//int saved_timeout_join = timeout_join[thread_num];
		pthread_mutex_unlock(&timout);

		// first check if SHUTDOWN command is given by the user
		// OR check_message's timeout is over
		if(shutdown_flag == true || saved_check_timeout == 0)	{
			//printf("Sending NTFY\n");
			// send a NOTIFY message to the neighbor that
			// this node is shutting down
			Header.msg_type = NTFY;
			GetUOID(node_inst_id, "msg", Header.UOID, sizeof Header.UOID);
			Header.TTL = 1;		// for keepAlive messge
			Header.reserved = 0;	// always
			Header.data_length = 1;  // only one byte for NTFY

			buffer = SetBuffer(HEADER_LEN);
			memcpy(&buffer[0], &Header.msg_type, 1);
			memcpy(&buffer[1], &Header.UOID[0], 20);
			memcpy(&buffer[21], &Header.TTL, 1);
			memcpy(&buffer[22], &Header.reserved, 1);
			memcpy(&buffer[23], &Header.data_length, 4);
			if((numbytes = send(new_fd, buffer, HEADER_LEN, 0)) == -1)	{
				//perror("send");
				kill_threads();
				pthread_exit(NULL);
			}
			free(buffer);

			// next, send the data for this NTFY
			char error_code;
			error_code = 0x01; // '1' for user shutdown
			buffer = SetBuffer(Header.data_length);
			memcpy(&buffer[0], &error_code, 1);
			if((numbytes = send(new_fd, buffer, Header.data_length, 0)) == -1)	{
				//perror("send");
				kill_threads();
				pthread_exit(NULL);
			}
			free(buffer);
			memset(log_data, '\0', MAXLINE);
			sprintf(log_data, "%x", error_code);
			write_log('s', his_port, Header, log_data);

			memset(&Header, 0, HEADER_LEN);

			if(saved_check_timeout == 0)	{
				// if it times out, this node is disconnected from the
				// core of the beacon nodes.
				// So, gracefully shut down and rejoin the network
				printf("\tDisconnected from the core network... REJOINING...\n");
				rejoin_flag = true;		// need to rejoin the network
				kill_threads();
			}
			break;
		}

		if(retval == 0)	{
			// timer for 'select' goes down
			// Alive timeout occurs... send a keep alive message
			if(saved_timeout_alive == 0)	{
				Header.msg_type = KPAV;
				GetUOID(node_inst_id, "msg", Header.UOID, sizeof Header.UOID);
				Header.TTL = 1;		// for keepAlive messge
				Header.reserved = 0;	// always
				Header.data_length = 0;
	
				buffer = SetBuffer(HEADER_LEN);
				memcpy(&buffer[0], &Header.msg_type, 1);
				memcpy(&buffer[1], &Header.UOID[0], 20);
				memcpy(&buffer[21], &Header.TTL, 1);
				memcpy(&buffer[22], &Header.reserved, 1);
				memcpy(&buffer[23], &Header.data_length, 4);
				if((numbytes = send(new_fd, buffer, HEADER_LEN, 0)) == -1)	{
					//perror("send");
					kill_threads();
					pthread_exit(NULL);
				}
	
				memset(log_data, '\0', MAXLINE);
				sprintf(log_data, "");	// no data part
				write_log('s', his_port, Header, log_data);
				free(buffer);
				//printf("keepalive msg send successful\n");

				// reset the timeout value
				pthread_mutex_lock(&timout);
				timeout_alive[thread_num] = KeepAliveTimeout/2;
				pthread_mutex_unlock(&timout);
			}

//			if(saved_timeout_node_dead == 0)	{
//				// the connection is now down. so shut down and exit
//				exit_flag = true;
//				break;
//			}
		}

		else if(retval > 0)	{
			// data available from the connected neighbor
			buffer = SetBuffer(HEADER_LEN);
			memset(&Header, 0, HEADER_LEN);
			if((numbytes = recv(new_fd, buffer, HEADER_LEN, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				pthread_exit(NULL);
			}
			memcpy(&Header.msg_type, &buffer[0], 1);
			memcpy(&Header.UOID[0], &buffer[1], 20);
			memcpy(&Header.TTL, &buffer[21], 1);
			memcpy(&Header.reserved, &buffer[22], 1);
			memcpy(&Header.data_length, &buffer[23], 4);
			free(buffer);
			//printf("Client: type %2x, data len %d\n", Header.msg_type, Header.data_length);
			// now parse the message and do the needful

			if(msg_parse(new_fd, Header, thread_num) == -1)	{
				break;		// this link should go down now
			}
		}
		
		else	{
			// error handling of select function
			//printf("client: socket: %d\n", new_fd);
			//perror("select");
			kill_threads();
			pthread_exit(NULL);
		}

		// now check the list in which dispatcher may have put an event
		pthread_mutex_lock(&conn_list_lock[thread_num]);
		if(IsEmpty(connection_first[thread_num]))	{
			pthread_mutex_unlock(&conn_list_lock[thread_num]);
			continue;
		}
		fwd_event = Delete(connection_first[thread_num]);
		pthread_mutex_unlock(&conn_list_lock[thread_num]);
		// now forward this message using the function
		msg_forward(new_fd, fwd_event, thread_num);

		// continue with the 'select' again
	}

	close(new_fd);
	
	// this part was active during the first part, but as in part 2, a node
	// should not shutdown if it has NO neighbors, i have commented it for the 
	// final project - part 2.
	/*
	if(my_node_type == REGULAR && num_neighbor == 0 && shutdown_flag == false)	{
		rejoin_flag = true;
		kill_threads();
	}
	*/

	pthread_exit(NULL);
	return 0;
}
