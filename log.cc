// includes funtions for logging the messages
#include "sv_node.h"

// the log file is guarded by the logfile_lock
// the calling function doesn't need to acquire this lock
// as it it internal to the functions
pthread_mutex_t logfile_lock;

// function to write an entry into the log file
// Complete logging of the system

// type is one of the following:
//	'r': Received message from 'port'
//	'f': Forwarded message to 'port'
//	's': Sent message to 'port'
void write_log(char type, int port, msg_header Header, char *data)
{
	pthread_mutex_lock(&logfile_lock);

	// format of a line in the log file is: 
	//	r <time> <from> <msgtype> <size> <ttl> <msgid> <data>
	//	f <time> <to> <msgtype> <size> <ttl> <msgid> <data>
	//	s <time> <to> <msgtype> <size> <ttl> <msgid> <data>

	// if the line begins with the comment character, then ignore all
	// the parameters except the log_data
	// just dump the log_data after the comment symbol //
	if(type == 'c')	{
		fputs("// ", fp_log);
		fputs(data, fp_log);
		fflush(fp_log);
		return;
	}

	// first, find the current time to make this log entry
	struct timeval tv;
	char *line; // one line in the log file
	line = (char *)malloc(MAXLINE);
	char temp[MAXLINE];
	memset(line, '\0', MAXLINE);
	memset(temp, '\0', MAXLINE);

	gettimeofday(&tv, NULL);
	sprintf(temp, "%c", type);
	strcat(line, temp);

	// time
	sprintf(temp, " %10ld.%03d ", tv.tv_sec, tv.tv_usec/1000);
	strcat(line, temp);

	// port number
	strcat(line, hostname);
	strcat(line, "_");
	sprintf(temp, "%5d", port);
	strcat(line, temp);

	// msg_type
	switch(Header.msg_type)	{
		case HLLO:	strcat(line, " HLLO"); break;
		case KPAV:	strcat(line, " KPAV"); break;
		case STRQ:	strcat(line, " STRQ"); break;
		case STRS:	strcat(line, " STRS"); break;
		case JNRQ:	strcat(line, " JNRQ"); break;
		case JNRS:	strcat(line, " JNRS"); break;
		case NTFY:	strcat(line, " NTFY"); break;
		case CKRQ:	strcat(line, " CKRQ"); break;
		case CKRS:	strcat(line, " CKRS"); break;
		case STOR:	strcat(line, " STOR"); break;
		case DELT:	strcat(line, " DELT"); break;
		case SHRQ:	strcat(line, " SHRQ"); break;
		case SHRS:	strcat(line, " SHRS"); break;
		case GTRQ:	strcat(line, " GTRQ"); break;
		case GTRS:	strcat(line, " GTRS"); break;
		default:	strcat(line, " UNKOWN"); break;
	}

	sprintf(temp, " %6d %3d", (HEADER_LEN + Header.data_length), Header.TTL);
	strcat(line, temp);

	// msgid
	int i=0;
	strcat(line, " ");
	for(i=0; i<4; i++)	{
		sprintf(temp, "%02x", Header.UOID[16+i]);
		strcat(line, temp);
	}

	// data (depends on msg_type)
	strcat(line, " ");
	strcat(line, data);
	strcat(line, "\n");

	fputs(line, fp_log);
	//fputs(line, stdout);
	fflush(fp_log);
	//fflush(stdout);

	pthread_mutex_unlock(&logfile_lock);
	return;
}

// make the data part of a msg_type for logging purpose
// analyse the msg_type and parse the buffer
// see the table of relationships in the spec for data going for
// each msg_type.
// Only the flooded and forwarded messages are transferrd here
void make_log_data(char *log_data, char msg_type, char *buffer, int buf_len)
{
	char log_temp[MAXLINE];
	unsigned char temp_UOID[20];
	char temp_c;
	int i;
	
	memset(log_data, '\0', MAXLINE);
	memset(log_temp, '\0', MAXLINE);

	// check the msg_type to decide what to write in the data part
	// the data part is made as per the relationship given in the spec
	switch(msg_type)	{
		case STRQ:
			memcpy(&temp_c, buffer, 1);
			if(temp_c == NEIGHBORS)
				sprintf(log_data, "neighbors");
			else
				sprintf(log_data, "files");
			break;

		case STRS:
			memcpy(temp_UOID, buffer, 20);	// take the UOID
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", temp_UOID[16+i]);	// last 4 bytes
				strcat(log_data, log_temp);
			}
			break;

		case JNRQ:
			join_data j_data;
			memcpy(&j_data.host_port, &buffer[4], 2);
			j_data.hostname = SetBuffer(buf_len -6);
			memcpy(&j_data.hostname[0], &buffer[6], buf_len -6);
			sprintf(log_data, "%d %s", j_data.host_port, j_data.hostname);
			break;
		
		case JNRS:
			join_response_data j_response;
			memcpy(&j_response.UOID[0], &buffer[0], 20);
			memcpy(&j_response.Distance, &buffer[20], 4);
			memcpy(&j_response.host_port, &buffer[24], 2);
			j_response.hostname = SetBuffer( buf_len -26);
			memcpy(&j_response.hostname[0], &buffer[26], buf_len -26);			
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", j_response.UOID[16+i]);
				strcat(log_data, log_temp);
			}
			memset(log_temp, '\0', MAXLINE);
			sprintf(log_temp, " %d %d %s",
				j_response.Distance, j_response.host_port, j_response.hostname);
			strcat(log_data, log_temp);
			break;
		
		case CKRQ:
			strcpy(log_data, "");	// no data
			break;
		
		case CKRS:
			memcpy(temp_UOID, buffer, 20);	// take the UOID
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", temp_UOID[16+i]);	// last 4 bytes
				strcat(log_data, log_temp);
			}
			break;

		// other cases	(for part 2)
		case SHRQ:
			search_data sr_data;
			memcpy(&sr_data.search_type, &buffer[0], 1);
			sr_data.key = SetBuffer(buf_len -1 +1);
			memcpy(sr_data.key, &buffer[1], buf_len -1);
			sr_data.key[buf_len -1 +1] = '\0';
			
			if(sr_data.search_type == 1)		// 'filename' search
				sprintf(log_data, "filename %s", sr_data.key);
			else if(sr_data.search_type == 3)	// 'keywords' search
				sprintf(log_data, "keywords %s", sr_data.key);
			else	{							// 'sha1hash' search
				sprintf(log_data, "sha1hash ");
				for(i=0; i<4; i++)	{
					sprintf(log_temp, "%02x", (unsigned char)sr_data.key[16+i]);
					strcat(log_data, log_temp);
				}
			}
			break;

		case SHRS:		// only UOID
			memcpy(temp_UOID, buffer, 20);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", temp_UOID[16+i]);
				strcat(log_data, log_temp);
			}
			break;

		case GTRQ:		// only FILE-ID
			memcpy(temp_UOID, buffer, 20);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", temp_UOID[16+i]);
				strcat(log_data, log_temp);
			}
			break;

		case GTRS:		// only UOID
			memcpy(temp_UOID, buffer, 20);
			for(i=0; i<4; i++)	{
				sprintf(log_temp, "%02x", temp_UOID[16+i]);
				strcat(log_data, log_temp);
			}
			break;

		case STOR:
			strcpy(log_data, "");	// nothing
			break;

		case DELT:
			strcpy(log_data, "");	// nothing
			break;

		default:
			strcpy(log_data, "");	// don't do anything for other messages
			break;
	}

	return;
}
