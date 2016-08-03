#include "sv_node.h"

struct neighbors	{
	unsigned int	Distance;
	uint16_t		host_port;
	char			*hostname;
};
struct neighbors *node_details;	// details got from the JNRS message
void parse_jnrs(char *, int, struct neighbors);

void *join_ask(void *threaddata)
{
	// make a temporary connection with the first beacon
	// if unsuccessful, try for all the beacons one after one
	int conn_try = 0, i;

	int sockfd;
	struct hostent *he;
	struct sockaddr_in their_addr; // connector's address information
	int his_port;
	char log_temp[MAXLINE];
	char log_data[MAXLINE];

	while(conn_try != beacon_count)	{

		if ((he=gethostbyname(beacon_host[conn_try])) == NULL) {  // get the host info 
			perror("gethostbyname");
			exit(1);
		}

		their_addr.sin_family = AF_INET;    // host byte order 
		their_addr.sin_port = htons(beacon_port[conn_try]);  // short, network byte order 
		their_addr.sin_addr = *((struct in_addr *)he->h_addr);
		memset(their_addr.sin_zero, '\0', sizeof their_addr.sin_zero);

		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			perror("socket");			// make a socket
			exit(1);
		}

		if (connect(sockfd, (struct sockaddr *)&their_addr, sizeof their_addr) == -1) {
			// connection refused by this beacon, try with the next one
			conn_try++;
			close(sockfd);
			continue;
		}
		break;		// connection successful, continue with the join process
	}
	if(conn_try == beacon_count)	{
		printf("\tNo beacon node is up, see \'ini\' file\n");
		shutdown_flag = true;		// needs to shut down, no beacon available
		pthread_exit((void *)0);	// '0' for error, '1' for success
	}
	his_port = beacon_port[conn_try];	// port of connected neighbor

	// now send a join message to the connected beacon node
	msg_header Header;
	join_data join_msg_data;
	char *buffer;
	int numbytes = 0;

	Header.msg_type = JNRQ;
	GetUOID(node_inst_id, "msg", Header.UOID, sizeof Header.UOID);
	Header.TTL = TTL;	// my TTL
	Header.reserved = 0; // once again, it's zero
	
	join_msg_data.host_location = Location; // my location
	join_msg_data.host_port = Port;			// my port number
	join_msg_data.hostname = SetBuffer(strlen(hostname));
	strcpy(join_msg_data.hostname, hostname); // my hostname

	Header.data_length = sizeof(int) + sizeof(uint16_t)
							+ strlen(join_msg_data.hostname);

	buffer = SetBuffer(HEADER_LEN);
	memcpy(&buffer[0], &Header.msg_type, 1);
	memcpy(&buffer[1], &Header.UOID[0], 20);
	memcpy(&buffer[21], &Header.TTL, 1);
	memcpy(&buffer[22], &Header.reserved, 1);
	memcpy(&buffer[23], &Header.data_length, 4);	

	// send the header of the JNRQ message
	if((numbytes = send(sockfd, buffer, HEADER_LEN, 0)) == -1)	{
		perror("send");
		exit(1);
	}
	free(buffer);

	// now send the data part of this JNRQ message
	buffer = SetBuffer(Header.data_length);
	memcpy(&buffer[0], &join_msg_data.host_location, 4);
	memcpy(&buffer[4], &join_msg_data.host_port, 2);
	memcpy(&buffer[6], &join_msg_data.hostname[0],
								strlen(join_msg_data.hostname));

	if((numbytes = send(sockfd, buffer, Header.data_length, 0)) == -1)	{
		perror("send");
		exit(1);
	}
	free(buffer);

	// logging
	memset(log_data, '\0', MAXLINE);
	sprintf(log_data, "%d %s", 
			join_msg_data.host_port, join_msg_data.hostname);
	write_log('s', his_port, Header, log_data);

	// now get all the Join Responses (JNRS) till JoinTimeout expires
	int expire = 0, num = 0;
	int retval = 0;	// return value of the select function
	struct timeval tv;
	tv.tv_sec = 1;	// check all the timeout values after every 1 second
	tv.tv_usec = 0;
	node_details = (struct neighbors *)malloc(sizeof(struct neighbors));

	fd_set rfds;
	while(expire < JoinTimeout)	{
		FD_ZERO(&rfds);
		FD_SET(sockfd, &rfds);
		retval = select(sockfd+1, &rfds, NULL, NULL, &tv);
		
		if(retval == 0)	{// timer goes down, go back
			expire++;
			continue;
		}

		else if(retval > 0)	{
			// something available on socket descriptor
			// first read the header
			buffer = SetBuffer(HEADER_LEN);
			if((numbytes = recv(sockfd, buffer, HEADER_LEN, 0)) == -1)	{
				perror("recv");
				exit(1);
			}
			memcpy(&Header.msg_type, &buffer[0], 1);
			memcpy(&Header.UOID[0], &buffer[1], 20);
			memcpy(&Header.TTL, &buffer[21], 1);
			memcpy(&Header.reserved, &buffer[22], 1);
			memcpy(&Header.data_length, &buffer[23], 4);
			free(buffer);

			// now receive the remaining part
			buffer = SetBuffer(Header.data_length);
			if((numbytes = recv(sockfd, buffer, 
									Header.data_length, 0)) == -1)	{
				perror("recv");
				exit(1);
			}
			// discard everything except 'Join Response' messages
			if(Header.msg_type != JNRS)
				continue;

			// now parse the message and write the 'init_neighbor_file'
			node_details = (struct neighbors *)realloc(node_details,
												(num+1)*sizeof(struct neighbors));
			memcpy(&node_details[num].Distance, &buffer[20], 4);
						// skip the first 20 bytes of UOID, not needed
			memcpy(&node_details[num].host_port, &buffer[24], 2);
			node_details[num].hostname = SetBuffer(Header.data_length - 26);
			memcpy(&node_details[num].hostname[0], &buffer[26],
										Header.data_length - 26);
				// 26 = 20 + 4 + 2, (UOID + Distance + host_port)
			free(buffer);
	
			unsigned char log_UOID[20];
			memcpy(&log_UOID[0], &buffer[0], 20);
			memset(log_data, '\0', MAXLINE);
			memset(log_temp, '\0', MAXLINE);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", log_UOID[16+i]);
				strcat(log_data, log_temp);
			}
			sprintf(log_temp, " %d %d %s", node_details[num].Distance,
					node_details[num].host_port, node_details[num].hostname);
			strcat(log_data, log_temp);
			write_log('r', his_port, Header, log_data);
		
//			printf("%s %d -> %d\n" ,node_details[num].hostname,
//					node_details[num].host_port, node_details[num].Distance);
		}
		num++;
	}

	close(sockfd);
	// join process complete, now sort the data according to the Distance
	// then choose the top 'InitNeighbors' number of nodes
	// and write them into the 'init_neighbors_file' in the home-directory
	
	// first, sort the entries in the structures based on the Distance
	int j;
	int num_responses = num;	// number of JNRS received
	struct neighbors temp;		// used for swapping (while sorting)
	
	// bubble sort is used, as n is usually small for Join process
	// even though the efficiency is O(n^2);
	for(i=0; i<num_responses; i++)	{
		for(j=i; j<num_responses; j++)	{
			if(node_details[i].Distance > node_details[j].Distance)	{
				temp = node_details[i];
				node_details[i] = node_details[j];
				node_details[j] = temp;
			}
		}
	}

	// the number of responses received must be atleast MinNeighbors
	// if not, exit.. This join process is unsuccessful
	int write_num = MIN(InitNeighbors, num_responses);
	// there should be atleast InitNeighbors responses
	// for this node to continue working
	if(write_num < InitNeighbors)	{
		printf("\tJOIN FAILED: Not enough neighbors, check 'ini' file\n");
		shutdown_flag = true;
		pthread_exit((void *)0); // '0' for fail, '1' for success
	}

	FILE *fp;
	char line[MAXLINE], t[10];
	if( (fp = fopen(neighbor_file, "w")) == NULL)	{
		//printf("%s>> file not opened\n", __FILE__);
		pthread_exit((void *)0); 
	}

	for(i=0; i<write_num; i++)	{
		strcpy(line, node_details[i].hostname);
		strcat(line, ":");
		sprintf(t, "%d", node_details[i].host_port);
		strcat(line, t);
		strcat(line, "\n");
		fputs(line, fp);
		fflush(fp);
	}
	fclose(fp);
	for(i=0; i<num_responses; i++)
		free(node_details[i].hostname);	// free up the memory
	free(node_details);
	
	pthread_exit((void *)1);	// '0' for fail, '1' for success
	return 0;
}
