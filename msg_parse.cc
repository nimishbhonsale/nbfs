#include "sv_node.h"
#include <openssl/sha.h>
#define BUF_LIM	8092 // as per the spec
pthread_mutex_t UOID_list_lock;
pthread_mutex_t FileID_list_lock;
pthread_mutex_t stor_get_lock;
extern bool status_type_neighbors;

void make_log_data(char *log_data, char msg_type, char *buffer, int buf_len);

int msg_parse(int socknum, msg_header Header, int thread_num)
{
	int numbytes=0, size=0;
	int i=0, j=0, next=0, tempsize=0, temp=0;
	FILE *fp, *fp_s, *fp_t;
	int recv_len, send_len, buf_len;
	char *buffer, *header_buf;	// to keep the data to send
	int saved_neighbors;
	bool no_more_records;	// used to track status response
	char log_data[MAXLINE];
	char log_temp[MAXLINE];
	memset(log_data, '\0', MAXLINE);
	memset(log_temp, '\0', MAXLINE);

	struct stat stat_buf;
	int retval= 0;	// return value for this function
	int16_t t;
	status_data status;
	status_response_data response;
	join_response_data j_response;
	check_response_data c_response;
	store_data s_data;
	search_data srch;
	search_response_data sr_response;
	get_data g_data;
	get_response_data g_response;
	event fwd_event;
	msg_entry new_entry;
	file_entry f_entry;
	msg_value key_value;
	msg_header send_header;

	int his_port;
	pthread_mutex_lock(&timout);
	his_port = active_conn[thread_num];
	pthread_mutex_unlock(&timout);

	char MetaFileName[MAXLINE];
	char DataFileName[MAXLINE];
	char CertFileName[MAXLINE];
	//printf("%02x: %d\n", Header.msg_type, socknum);
	//sleep(1);

	// for STOR and GTRS messages, data file is transerred,
	// so we don't assign a buffer of data_length for these two messages.
	// instead, we receive them in a buffer of 8192 Bytes in a loop
	if(Header.msg_type != STOR && Header.msg_type != GTRS)
		buffer = SetBuffer(Header.data_length);

	switch(Header.msg_type)		{
		case KPAV:
			sprintf(log_data, "");	// no data part
			write_log('r', his_port, Header, log_data);

			pthread_mutex_lock(&timout);
			timeout_node_dead[thread_num] = KeepAliveTimeout;
			pthread_mutex_unlock(&timout);
					// reset the connection's timeout
			//printf("received keepalive msg\n");
			break;

		case STRQ:
			// send a STRS to the neighbor and put an event
			// in the dispatcher for flooding

			// now get the remaining part of the message
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			
			// check what is the status request type
			memcpy(&status.status_type, buffer, 1);

			if(status.status_type == NEIGHBORS)
				sprintf(log_data, "neighbors");
			else
				sprintf(log_data, "files");
			write_log('r', his_port, Header, log_data);
			//printf("I receive STRQ from %d, %d bytes\n", his_port, numbytes);
			// STRQ is a flooded message in the network
			// So it may be a duplicate message (verify this)
			pthread_mutex_lock(&UOID_list_lock);
			Print_UOID();
			if(Find_UOID(Header.UOID) == true)	{
				pthread_mutex_unlock(&UOID_list_lock);
				//printf("STRQ Message from %d is duplicate, discarding\n", his_port);
				free(buffer);
				break;		// don't do anything, its a duplicate msg
			}				// OR the TTL has reached zero!

			// put this Message UOID in the UOID list
			//printf("STRQ Message from %d is first timer, parse it\n", his_port);
			memcpy(new_entry.UOID, Header.UOID, 20);

			new_entry.port_num = his_port;	// put my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// now put this event in the dispatcher for flooding
			// only if TTL has not reached to zero
			Header.TTL--;		// decrement TTL

			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				//printf("I put this STRQ from %d for flooding in dispatcher\n", his_port);
			}

			// send reply STRS: make a header and STRS data
			// In Part 1, only 0x01 will be recieved
			send_header.msg_type = STRS;
			GetUOID(node_inst_id, "msg", send_header.UOID, 
									sizeof send_header.UOID);
			send_header.TTL = TTL;			// my TTL
			send_header.reserved = 0;		// always

			// make STRS response and send it to the neighbor
			memcpy(response.UOID, Header.UOID, 20);
			response.host_port = Port;		// my port
			response.host_info_len = sizeof(response.host_port) + 
													strlen(hostname);
			size = 0;
			size+= 20;	// size of UOID
			size+= sizeof(response.host_info_len);
			size+= response.host_info_len;
						// my hostname and port number
			response.hostname = SetBuffer(strlen(hostname));
			strcpy(response.hostname, hostname);

			if(status.status_type == NEIGHBORS)
				saved_neighbors = num_neighbor;
			else
				saved_neighbors = GetLength_index(fnameList);

			response.record_len = (int *)malloc(saved_neighbors * sizeof(int));
			response.n_host_port = (int *)malloc(saved_neighbors * sizeof(int));
			response.n_hostname = (char **)malloc(saved_neighbors * sizeof(char *));

			if(status.status_type == NEIGHBORS)	{
				pthread_mutex_lock(&timout);

				for(i=0,j=0; i<FD_SETSIZE; i++)	{
					if(active_conn[i] != -1)	{
						response.n_host_port[j] = active_conn[i];
						response.n_hostname[j] = SetBuffer(13);
							// 13 is lenght of 'nunki.usc.edu'
							// change it with hostname later on
						strcpy(response.n_hostname[j], "nunki.usc.edu");
						response.record_len[j] = sizeof(int) + 
												strlen(response.n_hostname[j]);
						size+= sizeof(response.record_len[j]);
						size+= response.record_len[j];
							// port number + hostname of the neighbor
						j++;
					}
				}
				pthread_mutex_unlock(&timout);
				// change the last record's lenght to zero (as per spec)
				response.record_len[j-1] = 0;
			}

			else	{		// request type is FILES
				// put the metadata of all the files in the response
				pthread_mutex_lock(&lockListMutex);
				int *file_nums;
				file_nums = (int *)malloc(saved_neighbors * sizeof(int));
				GetFileNumbers(fnameList, file_nums);
				
				// put the metadata of each file into the structure
				struct stat stat_buf;
				char meta_name[MAXLINE];
				//printf("records = %d\n", saved_neighbors);
				for(i=0; i<saved_neighbors; i++)	{
					response.n_host_port[i] = 0;	// not needed for FILES
					memset(meta_name, '\0', MAXLINE);
					sprintf(meta_name, "%s/%s/%d.meta", HomeDir,FilesDir, file_nums[i]);
					stat(meta_name, &stat_buf);
					
					if( (fp = fopen(meta_name, "r")) == NULL)	{
						response.n_hostname[i] = SetBuffer(0);
					}
					else	{
						response.n_hostname[i] = SetBuffer(stat_buf.st_size +1);
						for(j=0; j<stat_buf.st_size; j++)
							response.n_hostname[i][j] = fgetc(fp);
						fclose(fp);
						response.n_hostname[i][j] = '\0';
					}
					response.record_len[i] = sizeof(int) + stat_buf.st_size +1;
					size+= sizeof(response.record_len[i]);
					size+= response.record_len[i];
							// record len's size + record's length
				}	
				pthread_mutex_unlock(&lockListMutex);
				// change the last record's lenght to zero (as per spec)
				if(saved_neighbors >0)
					response.record_len[i-1] = 0;
			}

			// now copy the data to a string to send
			buffer = SetBuffer(size);
			memcpy(&buffer[0],  &response.UOID[0], 20);
			memcpy(&buffer[20], &response.host_info_len, 2);
			memcpy(&buffer[22], &response.host_port, 2);
			tempsize = strlen(response.hostname);
			memcpy(&buffer[24], &response.hostname[0], tempsize);
			next = tempsize + 24;
			for(i=0; i<saved_neighbors; i++)	{
				memcpy(&buffer[next], &response.record_len[i], 4);
				next+= 4;
				memcpy(&buffer[next], &response.n_host_port[i], 4);
				next+= 4;
				if(response.record_len[i] != 0)
					tempsize = response.record_len[i] -sizeof(int);
				else
					tempsize = size -next -sizeof(int);
				memcpy(&buffer[next], &response.n_hostname[i][0], tempsize);
				next+= tempsize;
			}

			// data ready.. now calculate the data length
			send_header.data_length = size;
			header_buf = SetBuffer(HEADER_LEN);
			memcpy(&header_buf[0], &send_header.msg_type, 1);
			memcpy(&header_buf[1], &send_header.UOID[0], 20);
			memcpy(&header_buf[21], &send_header.TTL, 1);
			memcpy(&header_buf[22], &send_header.reserved, 1);
			memcpy(&header_buf[23], &send_header.data_length, 4);
			
			// first, send the header
			if( (numbytes = send(socknum, header_buf,	\
								HEADER_LEN, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}
			free(header_buf);
			// next, send the data
			if( (numbytes = send(socknum, buffer,	\
								send_header.data_length, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}	
			free(buffer);
			
			memset(log_data, '\0', MAXLINE);
			memset(log_temp, '\0', 4);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", response.UOID[16+i]);	// last 4 bytes
				strcat(log_data, log_temp);
			}
			write_log('s', his_port, send_header, log_data);
			//printf("I sent STRS to my neighbor %d, %d bytes\n", his_port, numbytes);
			break;
			
		
		case STRS:
			// status response message
			// save it if this is the originating node
			// else, forward it to the node from which it got the
			// corresponding status request...
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			//printf("I received STRS from neighbor %d, %d bytes\n", his_port, numbytes);

			// first check if this is the originating node which has
			// requested the STRQ message
			// If NO, then find the neighbor to route this message
			// using the port_num found by the UOID in the UOID_list
			// then put this event in the dispatcher for routing
			memset(&response, 0, sizeof(response));
			memcpy(&response.UOID[0], buffer, 20);

			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", response.UOID[16+i]);	// last 4 bytes
				strcat(log_data, log_temp);
			}
			write_log('r', his_port, Header, log_data);

			pthread_mutex_lock(&UOID_list_lock);
			key_value = Get_UOID(response.UOID);	// take the port number
			pthread_mutex_unlock(&UOID_list_lock);

			if(key_value != Port)	{	// this is not the originating node
				// now put this event in the dispatcher for routing
				//printf("I am not the originating node for this STRS\n");
				fwd_event.flooding = false;
				fwd_event.port_num = key_value;
					// port number of this node's neighbor to route
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				//printf("I put this STRS from %d in dispatcher for routing to %d\n", his_port, key_value);
				t=0;
				memcpy(&t, &buffer[20], 2);
				//printf("1--before putting in dispatcher: t= %d\n", t);
				t=0;
				memcpy(&t, &fwd_event.buffer[20], 2);
				//printf("2--before putting in dispatcher: t= %d\n", t);
				Append(dispatcher_first, fwd_event);
				t=0;
				memcpy(&t, &fwd_event.buffer[20], 2);
				//printf("3--before putting in dispatcher: t= %d\n", t);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				
				free(buffer);
				break;	// that's it! no need to do anything else
			}
			
			//printf("I am the originating node, parse the STRS from %d\n", his_port);
			// parse this response message and put the output in 'nam' file		
			memset(&response, 0, sizeof(response));
			next = 0;
			memcpy(&response.UOID[0], &buffer[next], 20);
			next+= 20;
			memcpy(&response.host_info_len, &buffer[next], 2);
			next+= 2;
			memcpy(&response.host_port, &buffer[next], 2);
			next+= 2;
			//printf("STRS: host info length: %d\n", response.host_info_len);
			tempsize = response.host_info_len - 2;
			response.hostname = SetBuffer(tempsize);
			memcpy(&response.hostname[0], &buffer[next], tempsize);
			next+= tempsize;
			
			// if the sending node has NO files, stored then
			// here we will encounter the end of the message, as
			// the last two fields will be omitted from the message
			if(next == Header.data_length)	{
				temp = write_meta(response, 0);
				break;
			}

			memset(&temp, 0, sizeof(int));
			memcpy(&temp, &buffer[next], sizeof(int));
			next+= sizeof(int);
			no_more_records = false;

			// initial allocation of memory, realloc each time in the loop
			response.record_len = (int *)malloc(sizeof(int));
			response.n_host_port = (int *)malloc(sizeof(int));
			response.n_hostname = (char **)malloc(sizeof(char *));
			for(i=1; ; i++)	{
				response.record_len = 
					(int *)realloc(response.record_len, i*sizeof(int));
				response.record_len[i-1] = temp;
				response.n_host_port = 
					(int *)realloc(response.n_host_port, i*sizeof(int));
				memcpy(&response.n_host_port[i-1], &buffer[next], sizeof(int));
				next+= sizeof(int);
				response.n_hostname = 
					(char **)realloc(response.n_hostname, i*sizeof(char *));
				if(temp == 0)	{	// this is the last record
					tempsize = Header.data_length - next;
					no_more_records = true;
				} else
					tempsize = temp - sizeof(int);	// lenght is host_port + hostname
				response.n_hostname[i-1] = SetBuffer(tempsize);
				memcpy(&response.n_hostname[i-1][0], &buffer[next], tempsize);
				next+= tempsize;
				if(no_more_records == true)
					break;
				memset(&temp, 0, sizeof(int));
				memcpy(&temp, &buffer[next], sizeof(int));		
				next+= sizeof(int);
			}
			saved_neighbors = i;
			// write this information into the .out file for "nam" if NEIGHBORS
			// OR write the responses in a file if the status is for FILES
			if(status_type_neighbors)
				temp = write_nam(response, saved_neighbors);
			else
				temp = write_meta(response, saved_neighbors);
			
			break;
		
		case JNRQ:
			// first recieve the data of this JNRQ
			if((numbytes = recv(socknum, buffer, Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			// put the data in a Join data structure
			join_data join_msg_data;
			memcpy(&join_msg_data.host_location, &buffer[0], 4);
			memcpy(&join_msg_data.host_port, &buffer[4], 2);
			join_msg_data.hostname = SetBuffer(Header.data_length -6);
			memcpy(&join_msg_data.hostname[0], &buffer[6], (Header.data_length-6));

			sprintf(log_data, "%d %s",
					join_msg_data.host_port, join_msg_data.hostname);
			write_log('r', his_port, Header, log_data);
			
			// stop if this message is already seen (i.e. its UOID is there in list)
			pthread_mutex_lock(&UOID_list_lock);		
			if(Find_UOID(Header.UOID) == true || Header.TTL == 0)	{
				pthread_mutex_unlock(&UOID_list_lock);
				break;	// ignore this JNRQ
			}
			// Add this UOID to the seen UOID list
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// keep my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// decrement TTL by 1 and if it has not reached to zero, 
			// Put this JNRQ in the dispatcher for flooding
			// this JNRQ message should be flooded to the whole network
			//event fwd_event;
			Header.TTL--;

			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored for flooding (must be zero)
				fwd_event.header = Header;	// header of JNRQ
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);

				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
			}			
			free(buffer);

			// now construct the join_reponse message and send it to the neighbor
			memcpy(j_response.UOID, Header.UOID, 20);
			int temp_dist;
			temp_dist = Location - join_msg_data.host_location;
			j_response.Distance = ABS(temp_dist);
				// distance is the absolute difference between this node's
				// location and requesting node's location
			j_response.host_port = (uint16_t)Port;	// my TCP port
			j_response.hostname = SetBuffer(strlen(hostname));
			strcpy(j_response.hostname, hostname);	// my hostname

			// make the Header for this join j_response JNRS message
			Header.msg_type = JNRS;
			GetUOID(node_inst_id, "msg", Header.UOID, 
									sizeof Header.UOID);
			Header.TTL = TTL;		// my TTL
			Header.reserved = 0;	// Obviously, zero (always)
			Header.data_length = 20 + sizeof(unsigned int) + sizeof(uint16_t)
								+ strlen(j_response.hostname);
					// UOID + Distance + host_port + hostname

			// first, send the header for JNRS to the requesting node
			buffer = SetBuffer(HEADER_LEN);
			memcpy(&buffer[0], &Header.msg_type, 1);
			memcpy(&buffer[1], &Header.UOID[0], 20);
			memcpy(&buffer[21], &Header.TTL, 1);
			memcpy(&buffer[22], &Header.reserved, 1);
			memcpy(&buffer[23], &Header.data_length, 4);
			if( (numbytes = send(socknum, buffer,	\
								HEADER_LEN, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}
			free(buffer);

			// now, send the data of JNRS to the requesting node
			buffer = SetBuffer(Header.data_length);
			memcpy(&buffer[0], &j_response.UOID[0], 20);
			memcpy(&buffer[20], &j_response.Distance, 4);
			memcpy(&buffer[24], &j_response.host_port, 2);
			memcpy(&buffer[26], &j_response.hostname[0], strlen(j_response.hostname));
			if( (numbytes = send(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}
			free(buffer);

			memset(log_data, '\0', MAXLINE);
			memset(log_temp, '\0', MAXLINE);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", j_response.UOID[16+i]);
				strcat(log_data, log_temp);
			}
			fflush(stdout);
			sprintf(log_temp, " %d %d %s",
				j_response.Distance, j_response.host_port, j_response.hostname);
			strcat(log_data, log_temp);
			write_log('s', his_port, Header, log_data);
			break;

		case JNRS:
			// tasks to do when a JNRS message is received
			// first get the data of the JNRS message
			if((numbytes = recv(socknum, buffer, Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}

			// parse the data for logging purpose
			memcpy(&j_response.UOID[0], &buffer[0], 20);
			memcpy(&j_response.Distance, &buffer[20], 4);
			memcpy(&j_response.host_port, &buffer[24], 2);
			j_response.hostname = SetBuffer(Header.data_length -26);
			memcpy(&j_response.hostname[0], &buffer[26], (Header.data_length -26));

			memset(log_data, '\0', MAXLINE);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", j_response.UOID[16+i]);
				strcat(log_data, log_temp);
			}
			memset(log_temp, '\0', MAXLINE);
			sprintf(log_temp, " %d %d %s",
				j_response.Distance, j_response.host_port, j_response.hostname);
			strcat(log_data, log_temp);
			write_log('r', his_port, Header, log_data);

			// A normal node will not be the originating node for JNRQ
			// So, it will not be accepting any JNRS calls for itself
			// So, unlike STRS, it will just route this message from
			// where the JNRQ came.
			// So, just make a event and put it in the dispatcher for routing
			unsigned char temp_UOID[20];
			memcpy(&temp_UOID[0], &buffer[0], 20);
						// take the original STRQ's UOID to find the port number
			pthread_mutex_lock(&UOID_list_lock);
			fwd_event.flooding = false;	// routed, not flooded
			fwd_event.port_num = Get_UOID(temp_UOID);
			Print_UOID();
			pthread_mutex_unlock(&UOID_list_lock);
			fwd_event.header = Header;
			fwd_event.buffer = SetBuffer(Header.data_length);
			memcpy(fwd_event.buffer, buffer, Header.data_length);

			pthread_mutex_lock(&dispatcher_lock);
			Append(dispatcher_first, fwd_event);
			pthread_cond_signal(&dispatcher_cond);
					// signal the dispatcher that something is there in the list
			pthread_mutex_unlock(&dispatcher_lock);
			// done
			break;
		
		case CKRQ:
			//printf("received CKRQ from %d\n", his_port);
			// Check message received, send a CHECK RESPONSE
			if((numbytes = recv(socknum, buffer, Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			// there is no data in the CHKQ message
			free(buffer);			
			sprintf(log_data, "");	// no data to log
			write_log('r', his_port, Header, log_data);

			// stop if this message is already seen (i.e. its UOID is there in list)
			pthread_mutex_lock(&UOID_list_lock);			
			if(Find_UOID(Header.UOID) == true)	{
				//printf("duplicate CKRQ, ignore it\n");
				pthread_mutex_unlock(&UOID_list_lock);
				break;	// ignore this JNRQ
			}
			// Add this UOID to the seen UOID list
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// keep my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// if this is a BEACON node, then send a check response message
			// else, put this message in dispatcher for flooding
			Header.TTL--;	// decrement TTL by 1

			if(my_node_type == BEACON)	{
				// send a check response
				//printf("Sending a CKRS response to %d\n", his_port);
				memcpy(c_response.UOID, Header.UOID, 20);
				Header.msg_type = CKRS;
				GetUOID(node_inst_id, "msg", Header.UOID, 
										sizeof Header.UOID);
				Header.TTL = TTL;		// my TTL
				Header.reserved = 0;	// Obviously, zero (always)
				Header.data_length = 20;	// UOID only

				buffer = SetBuffer(HEADER_LEN);
				memcpy(&buffer[0], &Header.msg_type, 1);
				memcpy(&buffer[1], &Header.UOID[0], 20);
				memcpy(&buffer[21], &Header.TTL, 1);
				memcpy(&buffer[22], &Header.reserved, 1);
				memcpy(&buffer[23], &Header.data_length, 4);

				// send the header, first
				if( (numbytes = send(socknum, buffer,	\
									HEADER_LEN, 0)) == -1)	{
					//perror("send");
					kill_threads();
					return 0;
				}
				free(buffer);

				buffer = SetBuffer(Header.data_length);
				memcpy(&buffer[0], &c_response.UOID[0], 20);

				// send the data now
				if( (numbytes = send(socknum, buffer,	\
									Header.data_length, 0)) == -1)	{
					//perror("send");
					kill_threads();
					return 0;
				}

				for(i=0; i<4; i++)	{
					sprintf(log_temp, "%02x", c_response.UOID[16+i]);
					strcat(log_data, log_temp);
				}
				write_log('s', his_port, Header, log_data);
				free(buffer);
			}

			// forward only if TTL has not reached to zero
			if(my_node_type == REGULAR && Header.TTL>0)	{
				//printf("flooding this CKRQ from %d\n", his_port);
				// just put this message for flooding, and forget it!
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored for flooding (must be zero)
				fwd_event.header = Header;	// header of JNRQ
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);

				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				
				free(buffer);
			}
			break;

		case CKRS:
			// check response message
			// if it is the requesting node, then parse it
			// else, just route it to the proper node
			if((numbytes = recv(socknum, buffer, Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			
			memcpy(&c_response.UOID[0], &buffer[0], 20);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", c_response.UOID[16+i]);
				strcat(log_data, log_temp);
			}
			write_log('r', his_port, Header, log_data);

			key_value = Get_UOID(c_response.UOID);
			if(Port != key_value)	{
				//printf("Routing this CKRS to %d\n", key_value);
				// route it, it's not the originating node for this CHECk
				fwd_event.flooding = false;	// don't flood it
				fwd_event.port_num = key_value;
				fwd_event.header = Header;
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);
				
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				
				free(buffer);
			} else	{
				// parse it, and reset the check timeout
				// this node is still connected to the core of the network
				pthread_mutex_lock(&timout);
				timeout_check = -1; // reset it
				pthread_mutex_unlock(&timout);
				//printf("Hurrah!!! I'm still connected to my girl-friends!!!\n");
			}
			break;

		case NTFY:
			// if a NOTIFY message is received, then do the following:
			//	1. Don't reply anything
			//	2. Flood a 'CHECK' message and wait for 'join-timeout' time
			//	3. do these only for non-beacon nodes and if NoCheck = 0 (enabled)
			if((numbytes = recv(socknum, buffer, Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			//printf("I received NTFY from %d\n", his_port);
			char error_code;
			memcpy(&error_code, &buffer[0], Header.data_length);

			sprintf(log_data, "%x", error_code);
			write_log('r', his_port, Header, log_data);

			pthread_mutex_lock(&timout);
			active_conn[thread_num] = -1;	// reset it
			num_neighbor--;
			pthread_mutex_unlock(&timout);

			if(my_node_type == REGULAR && NoCheck == 0)	{
				// if there is only one neighbor for this node, and it send a 
				// NTFY, that means there are no more neighbors left.
				// So, there is no need to send Check message.. just shutdown
				// and rejoin the SERVANT network...	
				if(num_neighbor == 0)	{
					retval = -1;
					break;
				}

				//printf("flooding check message\n");
				Header.msg_type = CKRQ;
				GetUOID(node_inst_id, "msg", Header.UOID, 
										sizeof Header.UOID);
				Header.TTL = TTL;		// my TTL
				Header.reserved = 0;	// Obviously, zero (always)
				Header.data_length = 0;	// zero for 'Check' message
				
				fwd_event.flooding = true;
				fwd_event.port_num = Port; // my port
				fwd_event.header = Header;
				fwd_event.buffer = SetBuffer(Header.data_length);
				
				// Add this UOID in the seen UOID list
				pthread_mutex_lock(&UOID_list_lock);
				memcpy(new_entry.UOID, Header.UOID, 20);
				new_entry.port_num = Port;	// keep my port
				Add_UOID(new_entry);
				pthread_mutex_unlock(&UOID_list_lock);

				// put this event into the dispatcher
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);

				// wait for the check_timeout period
				pthread_mutex_lock(&timout);
				timeout_check = JoinTimeout;
				pthread_mutex_unlock(&timout);
			}
			retval = -1;	// indicate that this link should go down
			break;
		
		case STOR:
			char *stor_filename;
			stor_filename= get_tempfile_name();
			fp_t = fopen(stor_filename, "w");
			
			// now get the remaining part of the message
			recv_len = Header.data_length;
			int tmm;tmm=0;
			while(recv_len > 0)	{	// there is some data to receive
				buf_len = (recv_len > BUF_LIM) ? BUF_LIM : recv_len;
				buffer = SetBuffer(buf_len);
				if( (numbytes = recv(socknum, buffer,	\
									buf_len, 0)) == -1)	{
					//perror("recv");
					kill_threads();
					return 0;
				}
				tmm += numbytes;
				fwrite(buffer, buf_len, 1, fp_t);
				free(buffer);
				recv_len -= buf_len;
			}
			fclose(fp_t);
			//printf("recd stor: %d bytes\n", tmm);

			make_log_data(log_data, Header.msg_type, buffer, 0);
			write_log('r', his_port, Header, log_data);

			// add this UOID in the UOID list
			pthread_mutex_lock(&UOID_list_lock);
			Print_UOID();
			if(Find_UOID(Header.UOID))	{
				pthread_mutex_unlock(&UOID_list_lock);
				//printf("STOR Message from %d is duplicate, discarding\n", his_port);
				free(buffer);
				break;		// don't do anything, its a duplicate msg
			}				// OR the TTL has reached zero!

			// put this Message UOID in the UOID list
			//printf("STOR Message from %d is first timer, parse it\n", his_port);
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// put my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// now this file should be stored in chache probabilistically
			// use the 'StoreProb' to decide about this
			if(!coin_outcome(StoreProb))	{
				free(buffer);
				break;
			}

			// now, parse the message and fill the 'store' structure
			if( (fp_s = fopen(stor_filename, "r")) == NULL)	{
				printf("\tstore: error: file is not stored\n");
				break;
			}
			next= 0;
			fread(&s_data.metadata_len, sizeof(int), 1, fp_s);
			next+= sizeof(int);
			s_data.metadata = SetBuffer(s_data.metadata_len);
			fread(&s_data.metadata[0], s_data.metadata_len, 1, fp_s);
			next+= s_data.metadata_len;
			fread(&s_data.cert_len, sizeof(int), 1, fp_s);
			next+= sizeof(int);
			s_data.certificate = SetBuffer(s_data.cert_len);
			fread(&s_data.certificate[0], s_data.cert_len, 1, fp_s);
			next+= s_data.cert_len;
			int file_len;
			file_len = Header.data_length - next;

			// write the data of this file in a temporary file
			char ch;
			char *temp_data;
			temp_data = get_tempfile_name();
			if( (fp = fopen(temp_data, "w")) == NULL)
				printf("STORE: error opening a temp file\n");
			for(i=0; i<file_len; i++)	{
				ch = fgetc(fp_s);
				fputc(ch, fp);
			}
			fclose(fp);			
			fclose(fp_s);

			// first thing first, verify the SHA1 of this file
			// computer the SHA1 of received datafile, then match it
			// with the SHA1 received as part of the Metafile
			// if they don't match, go back...
			char *temp_meta;
			temp_meta = get_tempfile_name();
			pthread_mutex_lock(&lockListMutex);
			fp = fopen(temp_meta, "w");
			for(i=0; i<s_data.metadata_len; i++)
				fputc(s_data.metadata[i], fp);
			fclose(fp);
			// parse this temp metafile
			parsed_metadata metadata;
			metadata = parse_metafile(temp_meta);
			remove(temp_meta);
			pthread_mutex_unlock(&lockListMutex);

			// calculate the SHA1 of file_data
			SHA_CTX c;
			unsigned char temp_sha1[20];
			if( (fp = fopen(temp_data, "r")) == NULL)
				printf("STORE: error opening a temp file\n");
			SHA1_Init(&c);
			for(i=0; i<file_len; i++)	{
				ch = fgetc(fp);
				SHA1_Update(&c, &ch, 1);
			}
			SHA1_Final(temp_sha1, &c);
			fclose(fp);

			// match the calculate SHA1 with that of received SHA1
			// if they don't match, go back...
			if(memcmp(temp_sha1, metadata.SHA1, 20) != 0)	{
				printf("\tSHA1 of received file does not match, ");
				printf("File is not stored\n");

				for(i=0; i<20; i++)
					printf("%02x", temp_sha1[i]);
				printf("\n");
				for(i=0; i<20; i++)
					printf("%02x", metadata.SHA1[i]);
				printf("\n");
				break;
			}

			// now store this file in CACHE area
			// make free space if cache hasn't enough free space
			// Use LRU for this purpose
			pthread_mutex_lock(&lockListMutex);
			if(!insertFileInCache(fnameList, file_len))	{
				pthread_mutex_unlock(&lockListMutex);
				break;
			}
			pthread_mutex_unlock(&lockListMutex);

			// now put this event in the dispatcher for flooding
			// only if TTL has not reached to zero
			Header.TTL--;		// decrement TTL
			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(strlen(stor_filename));
				strcpy(fwd_event.buffer, stor_filename);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				//printf("I put this STOR from %d for flooding in dispatcher\n", his_port);
			}

			// create MetaFile, Datafile and certificate file
			int fileNum;
			fileNum= getFileNumber();
			//printf("file num: %d\n", fileNum);
			sprintf(MetaFileName, "%s/%s/%d.meta", HomeDir,FilesDir, fileNum);
			sprintf(DataFileName, "%s/%s/%d.data", HomeDir,FilesDir, fileNum);
			sprintf(CertFileName, "%s/%s/%d.pem", HomeDir,FilesDir, fileNum);

			pthread_mutex_lock(&lockListMutex);
			fp = fopen(MetaFileName, "w");
			for(i=0; i<s_data.metadata_len; i++)
				fputc(s_data.metadata[i], fp);
			fclose(fp);

			fp = fopen(CertFileName, "w");
			for(i=0; i<s_data.cert_len; i++)
				fputc(s_data.certificate[i], fp);
			fclose(fp);

			fp = fopen(DataFileName, "w");
			fp_s = fopen(temp_data, "r");
			for(i=0; i<file_len; i++)
				fputc(fgetc(fp_s), fp);
			fclose(fp);
			fclose(fp_s);
			remove(temp_data);

			// add this file into the index lists
			metadata = parse_metafile(MetaFileName);
			add_lists(true, metadata, fileNum);	// goes into cache
			
			// change LRU priority for this stored file
			setLRUPriority_by_filenum(fileNum);
			pthread_mutex_unlock(&lockListMutex);
			break;

		case DELT:
			// now get the remaining part of the message
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			make_log_data(log_data, Header.msg_type, buffer, Header.data_length);
			write_log('r', his_port, Header, log_data);

			pthread_mutex_lock(&UOID_list_lock);
			Print_UOID();
			if(Find_UOID(Header.UOID))	{
				pthread_mutex_unlock(&UOID_list_lock);
				//printf("DELT Message from %d is duplicate, discarding\n", his_port);
				free(buffer);
				break;		// don't do anything, its a duplicate msg
			}				// OR the TTL has reached zero!

			// put this Message UOID in the UOID list
			//printf("DELT Message from %d is first timer, parse it\n", his_port);
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// put my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// now put this event in the dispatcher for flooding
			// only if TTL has not reached to zero
			Header.TTL--;		// decrement TTL
			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				//printf("I put this STOR from %d for flooding in dispatcher\n", his_port);
			}

			// now, parse the message
			// put the content of buffer into a temporary file and then parse it
			pthread_mutex_lock(&lockListMutex);
			char *temp_file;
			temp_file = get_tempfile_name();
			if( (fp = fopen(temp_file, "w")) == NULL)	{
				pthread_mutex_unlock(&lockListMutex);
				break;		// error in file, just exit
			}
			for(i=0; i<Header.data_length; i++)
				fputc(buffer[i], fp);
			fclose(fp);

			char del_FileName[MAXLINE];	 // file to delete
			unsigned char del_SHA1[20];  // SHA1 of the file to delete
			unsigned char del_Nonce[20]; // Nonce of the file to delete
			if( parse_signed(temp_file, del_FileName, del_SHA1, del_Nonce) == -1)	{
				remove(temp_file);
				pthread_mutex_unlock(&lockListMutex);
				break;		// error in function, just exit
			}

			int f;
			f = getFileNumber_index(fnameList,del_FileName,del_Nonce);
			//printf("filenumber: %d\n", f);
			if(f <=  0)	{
				// sorry, I dont' have the specified file
				// better luck next time
				remove(temp_file);
				pthread_mutex_unlock(&lockListMutex);
				break;
			}
			char * pemFilename;
			pemFilename = SetBuffer(100);
			memset(pemFilename,'\0',100);
			sprintf(pemFilename,"%s/%s/%d.pem", HomeDir, FilesDir, f);
			if(verifyCertificate(temp_file,pemFilename) == -1)	{
				// sorry, I am not the owner of this file, so can't delete it
				remove(temp_file);
				pthread_mutex_unlock(&lockListMutex);
				break;
			}
			//delete the temp file
			remove(temp_file);
			pthread_mutex_unlock(&lockListMutex);
			
			//printf("deleting %d.data\n", f);
			deleteFile(del_FileName,del_SHA1,del_Nonce);	
			
			break;

		case SHRQ:
			// now get the remaining part of the message
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			make_log_data(log_data, Header.msg_type, buffer, Header.data_length);
			write_log('r', his_port, Header, log_data);

			// SHRQ is a flooded message in the network
			// So it may be a duplicate message (verify this)
			pthread_mutex_lock(&UOID_list_lock);
			Print_UOID();
			if(Find_UOID(Header.UOID) == true)	{
				pthread_mutex_unlock(&UOID_list_lock);
				//printf("SHRQ Message from %d is duplicate, discarding\n", his_port);
				free(buffer);
				break;		// don't do anything, its a duplicate msg
			}				// OR the TTL has reached zero!

			// put this Message UOID in the UOID list
			//printf("SHRQ Message from %d is first timer, parse it\n", his_port);
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// put my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// now put this event in the dispatcher for flooding
			// only if TTL has not reached to zero
			Header.TTL--;		// decrement TTL

			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				//printf("I put this SHRQ from %d for flooding in dispatcher\n", his_port);
			}

			// now, parse the message
			memset(&srch, 0, sizeof(search_data));
			next =0;
			memcpy(&srch.search_type, &buffer[next], 1);
			next += 1;
			srch.key = SetBuffer(Header.data_length -next +1);
			memset(srch.key, '\0', Header.data_length -next +1);
			memcpy(srch.key, &buffer[next], Header.data_length -next);

			//if(srch.search_type == 1 || srch.search_type == 3)
			//	printf("%s", srch.key);
			//else
			//	for(i=0; i<20; i++) printf("%02x", (unsigned char)srch.key[i]);
			//printf("\n");

			// now search for the files
			int file_count, *file_nums;
			pthread_mutex_lock(&lockListMutex);	
			if(srch.search_type == 1 || srch.search_type == 2)	{
				file_count = getFileNumberList(srch.search_type,
									srch.key, &file_nums);
			}
			else	{	// keyword search
				file_count = searchKeywords(&file_nums, srch.key);
			}

			for(i=0; i<file_count; i++)
				setLRUPriority_by_filenum(file_nums[i]);
			pthread_mutex_unlock(&lockListMutex);

			//printf("files found: ");
			//for(i=0; i<file_count; i++)
			//	printf(" %d.meta", file_nums[i]);
			//printf("\n");

			// now make the search response
			size = 0;
			memset(&sr_response, 0, sizeof(search_response_data));
			memcpy(sr_response.UOID, Header.UOID, 20);
			size += 20;
			sr_response.next_length = (int *)malloc(file_count * sizeof(int));
			sr_response.FileID = (unsigned char **)malloc(file_count * sizeof(unsigned char *));
			sr_response.metadata = (char **)malloc(file_count * sizeof(char *));

			if(file_count == 0)	{	// there are no files matching this search
				sr_response.next_length = (int *)malloc(1 * sizeof(int));
				sr_response.next_length[0] = 0;
				size += sizeof(int);
			}

			char temp[MAXLINE];
			struct stat st_buf;
			for(i=0; i<file_count; i++)	{
				sr_response.FileID[i] = (unsigned char *)malloc(20);
				GetUOID(node_inst_id, "file", sr_response.FileID[i], 20);
				size += 20;
				sprintf(temp, "%s/%s/%d.meta", HomeDir, FilesDir, file_nums[i]);

				pthread_mutex_lock(&lockListMutex);
				fp = fopen(temp, "r");
				stat(temp, &st_buf);
				// change the last length to zero (as per spec)
				sr_response.next_length[i] = 
						(i == file_count -1) ? 0 : st_buf.st_size;
				size += 4;
				sr_response.metadata[i] = SetBuffer(st_buf.st_size);
				for(j=0; j<st_buf.st_size; j++)	{
					sr_response.metadata[i][j] = fgetc(fp);
				}
				size += st_buf.st_size;
				fclose(fp);
				pthread_mutex_unlock(&lockListMutex);

				// add this FileID to the list
				pthread_mutex_lock(&FileID_list_lock);
				memcpy(f_entry.FileID, sr_response.FileID[i], 20);
				f_entry.file_num = file_nums[i];
				Add_FileID(f_entry);
				pthread_mutex_unlock(&FileID_list_lock);
			}

			// make the buffer
			buffer = SetBuffer(size);
			next = 0;
			memcpy(&buffer[next], sr_response.UOID, 20);
			next += 20;

			if(file_count == 0)	{	// there are no files matching this search
				memcpy(&buffer[next], &sr_response.next_length[0], sizeof(int));
				next += 4;
			}
			for(i=0; i<file_count; i++)	{
				memcpy(&buffer[next], &sr_response.next_length[i], sizeof(int));
				next += 4;
				memcpy(&buffer[next], sr_response.FileID[i], 20);
				next += 20;
				file_len = (sr_response.next_length[i] == 0) ? \
								size -next : sr_response.next_length[i];
				memcpy(&buffer[next], sr_response.metadata[i], file_len);
				next += file_len;
			}
			
			// now send this buffer to the neighbor
			// first, send the header
			send_header.msg_type = SHRS;
			GetUOID(node_inst_id, "msg", send_header.UOID, 
									sizeof send_header.UOID);
			send_header.TTL = TTL;			// my TTL
			send_header.reserved = 0;		// always
			send_header.data_length = size;

			header_buf = SetBuffer(HEADER_LEN);
			memcpy(&header_buf[0], &send_header.msg_type, 1);
			memcpy(&header_buf[1], &send_header.UOID[0], 20);
			memcpy(&header_buf[21], &send_header.TTL, 1);
			memcpy(&header_buf[22], &send_header.reserved, 1);
			memcpy(&header_buf[23], &send_header.data_length, 4);

			if( (numbytes = send(socknum, header_buf,	\
								HEADER_LEN, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}
			free(header_buf);
			// next, send the data
			if( (numbytes = send(socknum, buffer,	\
								send_header.data_length, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}	
			make_log_data(log_data, send_header.msg_type, buffer, 
									send_header.data_length);
			write_log('s', his_port, send_header, log_data);

			free(buffer);
			break;
			

		case SHRS:
			// now get the remaining part of the message
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			make_log_data(log_data, Header.msg_type, buffer, Header.data_length);
			write_log('r', his_port, Header, log_data);
			
			// parse this message
			next = 0;
			memset(&sr_response, 0, sizeof(search_response_data));
			memcpy(sr_response.UOID, &buffer[next], 20);
			next += 20;

			// if this node has not originated this request,
			// just put it in dispatcher for routing
			pthread_mutex_lock(&UOID_list_lock);
			key_value = Get_UOID(sr_response.UOID);	// take the port number
			pthread_mutex_unlock(&UOID_list_lock);

			if(key_value != Port)	{	// this is not the originating node
				// now put this event in the dispatcher for routing
				//printf("I am not the originating node for this SHRS\n");
				fwd_event.flooding = false;
				fwd_event.port_num = key_value;
					// port number of this node's neighbor to route
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				
				free(buffer);
				break;	// that's it! no need to do anything else
			}

			// now this is the originator node of SHRQ request, parse it
			sr_response.next_length = (int *)malloc(1 * sizeof(int));
			sr_response.FileID = (unsigned char **)malloc(1 * sizeof(unsigned char *));
			sr_response.metadata = (char **)malloc(1 * sizeof(char *));
			no_more_records = false;
			for(i=0; ;i++)	{
				memcpy(&sr_response.next_length[i], &buffer[next], sizeof(int));
				next += sizeof(int);

				// if there are zero records in the response,
				// then we need to break down this loop
				if(next == Header.data_length)	// end of message reached
					break;

				sr_response.FileID[i] = (unsigned char *)malloc(20);
				memcpy(sr_response.FileID[i], &buffer[next], 20);
				next += 20;
				if(sr_response.next_length[i] == 0)	{	// this is the last record
					sr_response.next_length[i] = Header.data_length -next;
					no_more_records = true;
				}
					
				sr_response.metadata[i] = SetBuffer(sr_response.next_length[i]);
				memcpy(sr_response.metadata[i], &buffer[next], 
								sr_response.next_length[i]);
				next += sr_response.next_length[i];
				file_len = sr_response.next_length[i];

				pthread_mutex_lock(&lockListMutex);
				fp = fopen("temp_search", "w");
				for(j=0; j<file_len; j++)	{
					fputc(sr_response.metadata[i][j], fp);
				}
				fclose(fp);
				parsed_metadata meta;
				meta = parse_metafile("temp_search");
				remove("temp_search");

				// add this in the get_list
				get_count++;
				get_list = (unsigned char **)realloc(get_list, 
										get_count * sizeof(unsigned char *));
				get_list[get_count -1] = (unsigned char *)malloc(20);
				memcpy(get_list[get_count -1], sr_response.FileID[i], 20);

				// print this response to the user
				print_get(meta, sr_response.FileID[i], get_count);
				pthread_mutex_unlock(&lockListMutex);

				// break, if no more records
				if(no_more_records)
					break;
				sr_response.next_length = (int *)realloc(
									sr_response.next_length, (i+2)*sizeof(int));
				sr_response.FileID = (unsigned char **)realloc(
									sr_response.FileID, (i+2)*sizeof(unsigned char *));
				sr_response.metadata = (char **)realloc(
									sr_response.metadata, (i+2)*sizeof(char *));
			}
			free(buffer);
			break;			

		case GTRQ:
			// now get the remaining part of the message
			if( (numbytes = recv(socknum, buffer,	\
								Header.data_length, 0)) == -1)	{
				//perror("recv");
				kill_threads();
				return 0;
			}
			make_log_data(log_data, Header.msg_type, buffer, Header.data_length);
			write_log('r', his_port, Header, log_data);
			
			//printf("GTRQ: recd %d bytes\n", numbytes);
			// GTRQ is a flooded message in the network
			// So it may be a duplicate message (verify this)
			pthread_mutex_lock(&UOID_list_lock);
			Print_UOID();
			if(Find_UOID(Header.UOID) == true)	{
				pthread_mutex_unlock(&UOID_list_lock);
				//printf("GTRQ Message from %d is duplicate, discarding\n", his_port);
				free(buffer);
				break;		// don't do anything, its a duplicate msg
			}				// OR the TTL has reached zero!

			// put this Message UOID in the UOID list
			//printf("GTRQ Message from %d is first timer, parse it\n", his_port);
			memcpy(new_entry.UOID, Header.UOID, 20);
			new_entry.port_num = his_port;	// put my neighbor's port
			Add_UOID(new_entry);
			pthread_mutex_unlock(&UOID_list_lock);

			// now put this event in the dispatcher for flooding
			// only if TTL has not reached to zero
			Header.TTL--;		// decrement TTL

			if(Header.TTL > 0)	{
				fwd_event.flooding = true;
				fwd_event.port_num = 0;	// ignored
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(Header.data_length);
				memcpy(fwd_event.buffer, buffer, Header.data_length);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
				//printf("I put this GTRQ from %d for flooding in dispatcher\n", his_port);
			}
			memcpy(g_data.FileID, buffer, 20);
			free(buffer);

			pthread_mutex_lock(&FileID_list_lock);
			int file_num;
			if(Find_FileID(g_data.FileID) == false)	{
				pthread_mutex_unlock(&FileID_list_lock);
				break;
			}
			file_num = Get_FileID(g_data.FileID);
			pthread_mutex_unlock(&FileID_list_lock);
			
			// now make a GET RESPONSE message and send to the neighbor
			size = 0;
			memset(&g_response, 0, sizeof(get_response_data));
			memcpy(g_response.UOID, Header.UOID, 20);
			size += 20;
			char get_name[MAXLINE];
			
			// let's go one by one
			// first, it's METADATA
			pthread_mutex_lock(&lockListMutex);
			sprintf(get_name, "%s/%s/%d.meta", HomeDir, FilesDir, file_num);
			if( (fp = fopen(get_name, "r")) == NULL)	{
				printf("\tget: cannot fetch requested file\n");
				pthread_mutex_unlock(&lockListMutex);
				break;
			}
			stat(get_name, &stat_buf);
			g_response.metadata_len = stat_buf.st_size;
			size += sizeof(int);
			g_response.metadata = SetBuffer(stat_buf.st_size);
			for(i=0; i<stat_buf.st_size; i++)
				g_response.metadata[i] = fgetc(fp);
			size += stat_buf.st_size;
			fclose(fp);

			// now, it's turn of PUBLIC CERTIFICATE
			sprintf(get_name, "%s/%s/%d.pem", HomeDir, FilesDir, file_num);
			if( (fp = fopen(get_name, "r")) == NULL)	{
				printf("\tget: cannot fetch requested file\n");
				pthread_mutex_unlock(&lockListMutex);
				break;
			}
			stat(get_name, &stat_buf);
			g_response.cert_len = stat_buf.st_size;
			size += sizeof(int);
			g_response.certificate = SetBuffer(stat_buf.st_size);
			for(i=0; i<stat_buf.st_size; i++)
				g_response.certificate[i] = fgetc(fp);
			size += stat_buf.st_size;
			fclose(fp);
			pthread_mutex_unlock(&lockListMutex);

			// now, make a buffer to send to the neighbor
			char *gtrq_filename;
			gtrq_filename= get_tempfile_name();
			fp_t = fopen(gtrq_filename, "w");
			next = 0;
			fwrite(g_response.UOID, 20, 1, fp_t);
			next += 20;

			fwrite(&g_response.metadata_len, sizeof(int), 1, fp_t);
			next += sizeof(int);	
			fwrite(g_response.metadata, g_response.metadata_len, 1, fp_t);
			next += g_response.metadata_len;

			fwrite(&g_response.cert_len, sizeof(int), 1, fp_t);
			next += sizeof(int);
			fwrite(g_response.certificate, g_response.cert_len, 1, fp_t);
			next += g_response.cert_len;

			// and, here we are, finally, it's FILE DATA
			sprintf(get_name, "%s/%s/%d.data", HomeDir, FilesDir, file_num);
			pthread_mutex_lock(&lockListMutex);
			stat(get_name, &stat_buf);
			if( (fp = fopen(get_name, "r")) == NULL)	{
				printf("\tget: cannot send requested file\n");
				fclose(fp_t);
				pthread_mutex_lock(&lockListMutex);
				remove(gtrq_filename);
				free(gtrq_filename);
				break;
			}
			for(i=0; i<stat_buf.st_size; i++)
				fputc(fgetc(fp), fp_t);
			size += stat_buf.st_size;
			fclose(fp);
			fclose(fp_t);

			// file is accessed, change its LRU priority
			setLRUPriority_by_filenum(file_num);
			pthread_mutex_unlock(&lockListMutex);

			// so now "gtrq_filename" has all the data to send			
			// first, send the header
			send_header.msg_type = GTRS;
			GetUOID(node_inst_id, "msg", send_header.UOID, 
									sizeof send_header.UOID);
			send_header.TTL = TTL;			// my TTL
			send_header.reserved = 0;		// always
			send_header.data_length = size;

			header_buf = SetBuffer(HEADER_LEN);
			memcpy(&header_buf[0], &send_header.msg_type, 1);
			memcpy(&header_buf[1], &send_header.UOID[0], 20);
			memcpy(&header_buf[21], &send_header.TTL, 1);
			memcpy(&header_buf[22], &send_header.reserved, 1);
			memcpy(&header_buf[23], &send_header.data_length, 4);

			if( (numbytes = send(socknum, header_buf,	\
								HEADER_LEN, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return 0;
			}
			free(header_buf);

			// next, send the data
			fp_s = fopen(gtrq_filename, "r");
			send_len = send_header.data_length;
			tmm = 0;
			while(send_len > 0)	{
				buf_len = (send_len > BUF_LIM) ? BUF_LIM : send_len;
				buffer = SetBuffer(buf_len);
				fread(buffer, buf_len, 1, fp_s);
				if( (numbytes = send(socknum, buffer,	\
									buf_len, 0)) == -1)	{
					//perror("send");
					kill_threads();
					return 0;
				}
				tmm += numbytes;
				free(buffer);
				send_len -= buf_len;
			}
			fclose(fp_s);
			remove(gtrq_filename);
			free(gtrq_filename);
			
			buffer = SetBuffer(20);
			memcpy(buffer, g_response.UOID, 20);
			make_log_data(log_data, send_header.msg_type, buffer, 20);
			write_log('s', his_port, send_header, log_data);
			free(buffer);

			//printf("GTRQ: sent %d bytes as GTRS\n", tmm);

			break;

		case GTRS:
			// now get the remaining part of the message
			char *gtrs_filename;
			gtrs_filename= get_tempfile_name();
			fp_t = fopen(gtrs_filename, "w");
			
			// now get the remaining part of the message
			recv_len = Header.data_length;
			tmm=0;
			while(recv_len > 0)	{	// there is some data to receive
				buf_len = (recv_len > BUF_LIM) ? BUF_LIM : recv_len;
				buffer = SetBuffer(buf_len);
				if( (numbytes = recv(socknum, buffer,	\
									buf_len, 0)) == -1)	{
					//perror("recv");
					kill_threads();
					return 0;
				}
				tmm += numbytes;
				fwrite(buffer, buf_len, 1, fp_t);
				free(buffer);
				recv_len -= buf_len;
			}
			fclose(fp_t);
			//printf("GTRS: %d bytes recd\n", tmm);
			
			// parse this message
			fp_s = fopen(gtrs_filename, "r");

			next = 0;
			memset(&g_response, 0, sizeof(get_response_data));
			fread(g_response.UOID, 20, 1, fp_s);
			next += 20;

			// logging of this message
			buffer = SetBuffer(20);
			memcpy(buffer, g_response.UOID, 20);
			make_log_data(log_data, Header.msg_type, buffer, 20);
			write_log('r', his_port, Header, log_data);
			free(buffer);

			// if this node has not originated this request,
			// just put it in dispatcher for routing
			pthread_mutex_lock(&UOID_list_lock);
			key_value = Get_UOID(g_response.UOID);	// take the port number
			pthread_mutex_unlock(&UOID_list_lock);

			bool originator;
			originator = (key_value == Port);
			if(!originator)	{	// this is not the originating node
				// now put this event in the dispatcher for routing
				//printf("I am not the originating node for this GTRS\n");
				fwd_event.flooding = false;
				fwd_event.port_num = key_value;
					// port number of this node's neighbor to route
				Header.TTL--;
				fwd_event.header = Header;	//copy it with modified TTL
				fwd_event.buffer = SetBuffer(strlen(gtrs_filename));
				strcpy(fwd_event.buffer, gtrs_filename);	// copy as it is
				pthread_mutex_lock(&dispatcher_lock);
				Append(dispatcher_first, fwd_event);
				pthread_cond_signal(&dispatcher_cond);
						// signal the dispatcher that something is there in the list
				pthread_mutex_unlock(&dispatcher_lock);
			}

			// now this is the originator node of GTRQ request, parse it
			// now this file should be stored in chache probabilistically
			// use the 'CacheProb' to decide about this
			if(!originator && !coin_outcome(CacheProb))	{
				fclose(fp_s);
				break;
			}

			// now, parse the message and fill the 'get_response' structure
			fread(&g_response.metadata_len, sizeof(int), 1, fp_s);
			next+= sizeof(int);
			g_response.metadata = SetBuffer(g_response.metadata_len);
			fread(&g_response.metadata[0], g_response.metadata_len, 1, fp_s);
			next+= g_response.metadata_len;
			fread(&g_response.cert_len, sizeof(int), 1, fp_s);
			next+= sizeof(int);
			g_response.certificate = SetBuffer(g_response.cert_len);
			fread(&g_response.certificate[0], g_response.cert_len, 1, fp_s);
			next+= g_response.cert_len;
			file_len = Header.data_length - next;

			// write the data of this file in a temporary file
			temp_data = get_tempfile_name();
			if( (fp = fopen(temp_data, "w")) == NULL)
				printf("GET RESPONSE: error opening a temp file\n");
			for(i=0; i<file_len; i++)	{
				ch = fgetc(fp_s);
				fputc(ch, fp);
			}
			fclose(fp);
			fclose(fp_s);

			// first thing first, verify the SHA1 of this file
			// computer the SHA1 of received datafile, then match it
			// with the SHA1 received as part of the Metafile
			// if they don't match, go back...
			pthread_mutex_lock(&lockListMutex);
			temp_meta = get_tempfile_name();
			fp = fopen(temp_meta, "w");
			for(i=0; i<g_response.metadata_len; i++)	{
				fputc(g_response.metadata[i], fp);
			}
			fclose(fp);
			// parse this temp metafile
			metadata = parse_metafile(temp_meta);
			remove(temp_meta);
			pthread_mutex_unlock(&lockListMutex);

			// calculate the SHA1 of file_data
			if( (fp = fopen(temp_data, "r")) == NULL)
				printf("STORE: error opening a temp file\n");
			SHA1_Init(&c);
			for(i=0; i<file_len; i++)	{
				ch = fgetc(fp);
				SHA1_Update(&c, &ch, 1);
			}
			SHA1_Final(temp_sha1, &c);
			fclose(fp);

			// match the calculated SHA1 with that of received SHA1
			// if they don't match, go back...
			if(memcmp(temp_sha1, metadata.SHA1, 20) != 0)	{
				printf("\tSHA1 of received file does not match, ");
				printf("File is not stored\n");
				remove(temp_data);
				break;
			}
				
			// if this is not the originator node, then
			// store this file in CACHE area
			// make free space if cache hasn't enough free space
			// Use LRU for this purpose
			pthread_mutex_lock(&lockListMutex);
			if(!originator)	{
				if(!insertFileInCache(fnameList, file_len))	{
					pthread_mutex_unlock(&lockListMutex);
					remove(temp_data);
					break;
				}
			}
			pthread_mutex_unlock(&lockListMutex);

			// now check wether this file is already stored at this node
			// means the filename, SHA1 and Nonce should match with a stored
			// file. If yes, then don't store this file
			pthread_mutex_lock(&lockListMutex);
			f = getFileNumber_index(fnameList,metadata.FileName,metadata.Nonce);
			if(f < 0)	{				// file is not already stored
				// create MetaFile, Datafile and certificate file
				fileNum= getFileNumber();
				//printf("file num: %d\n", fileNum);
				sprintf(MetaFileName, "%s/%s/%d.meta", HomeDir,FilesDir, fileNum);
				sprintf(DataFileName, "%s/%s/%d.data", HomeDir,FilesDir, fileNum);
				sprintf(CertFileName, "%s/%s/%d.pem", HomeDir,FilesDir, fileNum);

				fp = fopen(MetaFileName, "w");
				for(i=0; i<g_response.metadata_len; i++)
					fputc(g_response.metadata[i], fp);
				fclose(fp);

				fp = fopen(CertFileName, "w");
				for(i=0; i<g_response.cert_len; i++)
					fputc(g_response.certificate[i], fp);
				fclose(fp);

				fp = fopen(DataFileName, "w");
				fp_s = fopen(temp_data, "r");
				for(i=0; i<file_len; i++)
					fputc(fgetc(fp_s), fp);
				fclose(fp);
				fclose(fp_s);

				// add this file into the index lists
				metadata = parse_metafile(MetaFileName);
				add_lists(!originator, metadata, fileNum);
				
				// change LRU priority for this stored file
				if(!originator)
					setLRUPriority_by_filenum(fileNum);
				pthread_mutex_unlock(&lockListMutex);
				
			}
			else
				pthread_mutex_unlock(&lockListMutex);

			if(!originator)	{
				remove(temp_data);
				break;
			}
			// if this is the originator node, it will store this file
			// in the current working dicrectory specified by the user
			char write_getfile[MAXLINE];
			get_file == NULL ? strcpy(write_getfile, metadata.FileName) :
									strcpy(write_getfile, get_file);
			fp = fopen(write_getfile, "r");

			if(fp != NULL)	{
				printf("\tget: file %s exists, Replace [Y/N]: ",
														write_getfile);
				fflush(stdout);
				if(toupper(getchar()) != 'Y')	{
					printf("\tget: file not stored\n");
					fflush(stdout);
					pthread_kill(cmd_interface, SIGINT);
					fclose(fp);
					break;	// don't replace the existing file
				}
			}
			fclose(fp);

			// now, either the file is not there or the user wants to replace it
			if( (fp = fopen(write_getfile, "w")) == NULL)	{
				printf("\tget: unable to write the file at current directory\n");
				pthread_kill(cmd_interface, SIGINT);
				break;
			}
			fp_s = fopen(temp_data, "r");
			for(i=0; i<file_len; i++)
				fputc(fgetc(fp_s), fp);
			fclose(fp);
			fclose(fp_s);
			remove(temp_data);

			printf("\tget: file %s stored at current working directory.\n", write_getfile);
			fflush(stdout);

			// now, finally, signal the command line thread that get is over
			pthread_kill(cmd_interface, SIGINT);
			break;
		
//		default:
//			break;
			//printf("%02x : unrecognized message received\n", Header.msg_type);

	}

	return retval;
}

// MSG FORWARDING
//	just forwards a message to the destination
//	socket descriptor is given as a parameter of this function
//	decrement TTL of routed messages
void msg_forward(int socknum, event fwd_event, int thread_num)
{
	int numbytes = 0;
	int his_port;
	pthread_mutex_lock(&timout);
	his_port = active_conn[thread_num];
	pthread_mutex_unlock(&timout);
	
	// first, a flooded message should not be sent back to the node
	// from which this node has received the message
	// UOID list stores the port number of the sending node.
	// find this port number, and return without sending if the port
	// number matches that of my neighbor's port number
	if(his_port == Get_UOID(fwd_event.header.UOID))
		return;

	// now, change the TTL to the minimum of this node's TTL
	// and the TTL already there in the message.
	// decrease the TTL by 1 before forwarding only if it is a routed message
	if(fwd_event.flooding)
		fwd_event.header.TTL = MIN(fwd_event.header.TTL, TTL);
	else
		fwd_event.header.TTL--;			// decrement by 1
	
	// if the TTL is zero then don't forward
	if(fwd_event.header.TTL == 0)
		return;

	// if this is a STOR message, then this should be flooded
	//	probabilistically. Use 'NeighborStoreProb' to decide about this
	if(fwd_event.header.msg_type == STOR && !coin_outcome(NeighborStoreProb))
		return;

	char *buffer;
	buffer = SetBuffer(HEADER_LEN);
	memcpy(&buffer[0], &fwd_event.header.msg_type, 1);
	memcpy(&buffer[1], &fwd_event.header.UOID[0], 20);
	memcpy(&buffer[21], &fwd_event.header.TTL, 1);
	memcpy(&buffer[22], &fwd_event.header.reserved, 1);
	memcpy(&buffer[23], &fwd_event.header.data_length, 4);
	if( (numbytes = send(socknum, buffer, \
						HEADER_LEN, 0)) == -1)	{
		//perror("send");
		kill_threads();
		return;
	}
	free(buffer);
	
	// if the msg is STOR or GET Response, then the buffer is available in a
	// temporary file in HomeDir, whose name is stored in the buffer
	unsigned char temp_UOID[20];
	FILE *fp_s;
	char *filename;
	int send_len, buf_len, tmm=0;
	if(fwd_event.header.msg_type == STOR ||
			fwd_event.header.msg_type == GTRS)	{

		// we need a mutex for this part of program as multiple thread
		// can try to access the same 'filename' file at the same time
		pthread_mutex_lock(&stor_get_lock);
		filename= SetBuffer(strlen(fwd_event.buffer));
		strcpy(filename, fwd_event.buffer);
		if( (fp_s = fopen(filename, "r")) == NULL)	{
			printf("\tstore/get: %s\n", filename);
			printf("\tstore/get: error forwarding the message\n");
			return;
		}
		free(fwd_event.buffer);

		// for a GTRS message, we need the UOID for logging purpose...
		// save it somewhere
		if(fwd_event.header.msg_type == GTRS)	{
			fread(temp_UOID, 20, 1, fp_s);
			rewind(fp_s);	// back to the beginning of the file
		}

		send_len = fwd_event.header.data_length;
		while(send_len > 0)	{
			buf_len = (send_len > BUF_LIM) ? BUF_LIM : send_len;
			fwd_event.buffer = SetBuffer(buf_len);
			fread(fwd_event.buffer, buf_len, 1, fp_s);
			if( (numbytes = send(socknum, fwd_event.buffer,	\
								buf_len, 0)) == -1)	{
				//perror("send");
				kill_threads();
				return;
			}
			tmm += numbytes;
			free(fwd_event.buffer);
			send_len -= buf_len;
		}
		
		//printf("get sent: %d bytes\n", tmm);
		fclose(fp_s);
		if(fwd_event.header.msg_type == GTRS)	{
			fwd_event.buffer = SetBuffer(20);	// logging purpose
			memcpy(fwd_event.buffer, temp_UOID, 20);
			remove(filename);
		}
		free(filename);
		pthread_mutex_unlock(&stor_get_lock);
	}

	// now check the msg_type from the header and decide what to do
	//unsigned char msg_type = fwd_event.header.msg_type;
	// send it to the connected neighbor
	else	{
		if( (numbytes = send(socknum, fwd_event.buffer,	\
							fwd_event.header.data_length, 0)) == -1)	{
			//perror("send");
			kill_threads();
			return;
		}
	}

	// make the data part of this message for logging purpose
	char log_data[MAXLINE];
	make_log_data(log_data, fwd_event.header.msg_type, 
					fwd_event.buffer, fwd_event.header.data_length);

	// if this node is the originator of a flooding message than
	// this message is logged as a 'sent' message
	if(fwd_event.flooding && fwd_event.port_num == Port)
		write_log('s', his_port, fwd_event.header, log_data);
	// if this node is not the originator of a flooding message then
	// this message is logged as a 'forwarded' message
	// otherwise, this message is a 'routed' message
	// this message is logged as a 'forwarded' message
	else
		write_log('f', his_port, fwd_event.header, log_data);

	//free(fwd_event.buffer);
}
