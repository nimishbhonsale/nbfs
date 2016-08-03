#include "sv_node.h"

extern char *node_inst_id;		// stores the instance of a node
extern char hostname[MAXLINE];

void *join_t(void *threaddata)
{
	int new_fd;
	int numbytes =0;
	thread_data *my_data;
	int i, thread_num = -1;
	char *buffer;
	int his_port;

	char log_data[MAXLINE];
	char log_temp[MAXLINE];
	memset(log_data, '\0', MAXLINE);
	memset(log_temp, '\0', MAXLINE);

	my_data = (thread_data *)threaddata;
	new_fd = my_data->new_fd;

	msg_header Header;
	Header = my_data->Header;
	buffer = SetBuffer(Header.data_length);

	numbytes = 0;
	// now receive the remaining part
	if((numbytes = recv(new_fd, buffer, Header.data_length, 0)) == -1)	{
		perror("recv");
		exit(1);
	}

	join_data join_msg_data;
	memcpy(&join_msg_data.host_location, &buffer[0], 4);
	memcpy(&join_msg_data.host_port, &buffer[4], 2);
	join_msg_data.hostname = SetBuffer(Header.data_length -6);
	memcpy(&join_msg_data.hostname[0], &buffer[6], (Header.data_length-6));

	his_port = join_msg_data.host_port;
	memset(log_data, '\0', MAXLINE);
	sprintf(log_data, "%d %s", 
			join_msg_data.host_port, join_msg_data.hostname);
	write_log('r', his_port, Header, log_data);

	// put details of this requesting node into active connection list
	// timeouts will be ignored for this temporary connection (set to zeros)
	// no keepalive kind of messages will be sent on this connection
	his_port = join_msg_data.host_port; // requesting node's TCP port
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

	timeout_join[thread_num] = 0;
	timeout_alive[thread_num] = 0;
	timeout_node_dead[thread_num] = 0;
			// no need for this temporary connection
	pthread_mutex_unlock(&timout);

	// put this Message UOID in the UOID list
	pthread_mutex_lock(&UOID_list_lock);
	msg_entry new_entry;
	memcpy(new_entry.UOID, Header.UOID, 20);
	new_entry.port_num = his_port;	// keep my neighbor's port
	Add_UOID(new_entry);
	Print_UOID();
	pthread_mutex_unlock(&UOID_list_lock);

	// put an event in the dispatcher
	// this JNRQ message should be flooded to the whole network

	event new_event;
	new_event.flooding = true;
	new_event.port_num = 0;	// ignored for flooding (must be zero)
	new_event.header = Header;	// header of JNRQ
	new_event.buffer = SetBuffer(Header.data_length);
	memcpy(new_event.buffer, buffer, Header.data_length);

	pthread_mutex_lock(&dispatcher_lock);
	Append(dispatcher_first, new_event);
	pthread_cond_signal(&dispatcher_cond);
			// signal the dispatcher that something is there in the list
	pthread_mutex_unlock(&dispatcher_lock);
	
	free(buffer);

	// Now the thread will send a join response to this message and
	join_response_data response;
	memcpy(response.UOID, Header.UOID, 20);
	int temp_dist = Location - join_msg_data.host_location;
	response.Distance = ABS(temp_dist);

		// distance is the absolute difference between this node's
		// location and requesting node's location
	response.host_port = (uint16_t)Port;	// my TCP port
	response.hostname = SetBuffer(strlen(hostname));
	strcpy(response.hostname, hostname);	// my hostname

	// make the Header for this join response JNRS message
	Header.msg_type = JNRS;
	GetUOID(node_inst_id, "msg", Header.UOID, 
							sizeof Header.UOID);
	Header.TTL = TTL;		// my TTL
	Header.reserved = 0;	// Obviously, zero (always)
	Header.data_length = 20 + sizeof(unsigned int) + sizeof(uint16_t)
							+ strlen(response.hostname);
				// UOID + Distance + host_port + hostname
	
	// first, send the header for JNRS to the requesting node
	buffer = SetBuffer(HEADER_LEN);
	memcpy(&buffer[0], &Header.msg_type, 1);
	memcpy(&buffer[1], &Header.UOID[0], 20);
	memcpy(&buffer[21], &Header.TTL, 1);
	memcpy(&buffer[22], &Header.reserved, 1);
	memcpy(&buffer[23], &Header.data_length, 4);
	if( (numbytes = send(new_fd, buffer,	\
						HEADER_LEN, 0)) == -1)	{
		perror("send");
		exit(1);
	}
	free(buffer);

	// now, send the data of JNRS to the requesting node
	buffer = SetBuffer(Header.data_length);
	memcpy(&buffer[0], &response.UOID[0], 20);
	memcpy(&buffer[20], &response.Distance, 4);
	memcpy(&buffer[24], &response.host_port, 2);
	memcpy(&buffer[26], &response.hostname[0], strlen(response.hostname));
	if( (numbytes = send(new_fd, buffer,	\
						Header.data_length, 0)) == -1)	{
		perror("send");
		exit(1);
	}
	free(buffer);

	memset(log_data, '\0', MAXLINE);
	memset(log_temp, '\0', MAXLINE);
	for(i=0; i<4; i++)	{
		sprintf(log_temp, "%02x", response.UOID[16+i]);
		strcat(log_data, log_temp);
	}
	sprintf(log_temp, " %d %d %s", response.Distance,
			response.host_port, response.hostname);
	strcat(log_data, log_temp);
	write_log('s', his_port, Header, log_data);
	
	// first phase is over
	// Now put this thread in an infinite loop and do the following:
	// 1. wait for 1-3 seconds on select
	// 2. As the neighbour is not going to send anything from now on,
	//		it will come out of select only due to timeout
	// 3. on timeout, check if there is anything in the routed message list
	// 4. if yes, forward it to the connected node and repeat the following
	int retval = 0;	// return value of the select function
	struct timeval tv;
	tv.tv_sec = 1;	// check all the timeout values after these seconds
	tv.tv_usec = 0;

	event fwd_event;
	fd_set rfds;
	while(1)	{
		FD_ZERO(&rfds);
		FD_SET(new_fd, &rfds);
		retval = select(new_fd+1, &rfds, NULL, NULL, &tv);
		if(retval > 0)	{
			close(new_fd);
			break;
		}
		// now check the list in which dispatcher may have put an event
		pthread_mutex_lock(&conn_list_lock[thread_num]);
		if(IsEmpty(connection_first[thread_num]))	{
			pthread_mutex_unlock(&conn_list_lock[thread_num]);
			continue;
		}
		fwd_event = Delete(connection_first[thread_num]);
		pthread_mutex_unlock(&conn_list_lock[thread_num]);
		// now forward this message using the function (in file msg_parse.cc)
		msg_forward(new_fd, fwd_event, thread_num);

		// continue with the 'select' again
	}

	// free up the resources
	pthread_mutex_lock(&timout);
	active_conn[thread_num] = -1;	// reset it
	num_neighbor--;
	pthread_mutex_unlock(&timout);

	return 0;
}	
