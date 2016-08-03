#include "sv_node.h"

bool shutdown_flag = false;

int execute_cmd(int argc, char **);

// array to get rid of duplicate entries for nodes
// else the 'nam' file will go to hell
extern int *seen;
extern int add_nam;
extern int count_hello;	// number of threads for HLLO

// CTRL^C handler
void sigint_handler(int signum)
{
	printf("\n");
	signal(SIGINT, sigint_handler);	// reactivate it
	return;		// just deliever the signal
}

int execute_cmd(int argc, char **argv);
int status_cmd(char req_type, int TTL, char *exefile);
extern int parse_store(int argc, char** argv);
extern int parse_delete(int argc, char** argv);
extern int parse_search(int argc, char** argv);
extern int parse_get(int argc, char** argv);
void kill_threads();

pthread_mutex_t extfile_lock;	// lock for "nam" output file
FILE *fp_extfile;	// file pointer for "nam" output file
					// global because shared by 'connection' threads
char prv_command[80];
bool status_type_neighbors;

// this thread runs continously and does the following tasks:
//	1. wait on the command prompt for the user input
//	2. checks if the command is a recognized one, else prints an error
//	3. if it is a valid command, give the command to the proper function
//	4. if the command requires to wait for some timeout or CTRL-C,
//			it waits using 'select'
//	5. after finishing, waits again for the user's command
void *std_input(void *threaddata)
{
	signal(SIGINT, sigint_handler);
	// mimic a command line interface that of a FTP client
	// show a "SERVANT:PORT> " prompt on the screen
	char command[MAXLINE];
	char **argv;
	int argc=0;
	char token[] = " \t\n";
	char *result = NULL;

	argv = (char **)malloc(MAXARG*sizeof(char *));
	//printf("\n\t***WELCOME TO SERVANT FILE SHARING SYSTEM***\n");

	fd_set rfds;
	struct timeval tv;
	int retval = 1;
	while(1)	{
		status_type_neighbors = false;

		if(retval > 0)	{
			printf("\nSERVANT:%d> ", Port);
			fflush(stdout);
		}
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);		// 0 for stdin
		tv.tv_sec = 1;
		tv.tv_usec = 0;	// wait for 1 second
		retval = select(1, &rfds, NULL, NULL, &tv);	// 1 is the max, 0+1

		// check if need to shutdown
		if(shutdown_flag == true)
			break;

		if(retval < 0)		{	// signal delivered
			retval = 1;
			continue;
		}
		if(retval == 0)			// timeout
			continue;

		// take data from the input stream
		fgets(command, MAXLINE, stdin);

		argc = 0;
		command[strlen(command)-1] = '\0';
		result = strtok(command, token);
		while(result != NULL)	{
			argv[argc] = (char *)malloc(MAXLINE*sizeof(char));
			strcpy(argv[argc], result);
			argc++;
			result = strtok(NULL, token);
		}
		
		// parse this command
		execute_cmd(argc, argv);
	}
	
	pthread_exit(NULL);
	return 0;
}

// this function parse the command and execute the corresponding function
//	command is parsed using strtok and 'space' as delimiter
int execute_cmd(int argc, char **argv)
{
	struct timeval tv;
	int retval =0;
	if(argc == 0)
		return 0;

	if(!strcmp(argv[0], "status"))	{
		if(argc!= 4)	{
			printf("\tFormat: status neighbors/files <ttl> <extfile>\n");
			return -1;
		}

		if(strcmp(argv[1], "neighbors") && strcmp(argv[1], "files"))	{
			printf("\tFormat: status neighbors/files <ttl> <extfile>\n");
			return -1;
		}
		
		printf("\tGetting %s status... (CTRL^C to terminate)\n", argv[1]);
		// now process the command 'status'
		// put an flooding event in the dispatcher and
		// wait for 'MsgLifetime' or quit if user presses CTRL^C
		// first, get TTL
		int TTL;
		if((TTL = atoi(argv[2])) < 0)	{
			printf("\tERROR: TTL should be zero or a positive integer\n");
			return -1;
		}
		
		// now get the name of file where network status is to be written
		char *extfile;
		extfile = (char *)malloc(strlen(argv[3]));
		strcpy(extfile, argv[3]);
		// now call the status_cmd() to put this event in dispatcher
		if(!strcmp(argv[1], "neighbors"))	{
			status_type_neighbors = true;
			retval = status_cmd(NEIGHBORS, TTL, extfile);
		}
		else	{
			status_type_neighbors = false;
			retval = status_cmd(FILES, TTL, extfile);
		}
		if(retval == -1)
			return -1;

		// now wait for 1.2*MsgLifetime
		tv.tv_sec = (time_t)1.2*MsgLifetime;
		tv.tv_usec = 0;
		retval = select(0, NULL, NULL, NULL, &tv);		

		pthread_mutex_lock(&extfile_lock);
		fclose(fp_extfile);	// now close the file
		fp_extfile = NULL;
		add_nam = 0;
		pthread_mutex_unlock(&extfile_lock);
		printf("\tstatus results available in %s\n", extfile);
	}

	else if(!strcmp(argv[0], "store"))	{
		parse_store(argc, argv);
	}

	else if(!strcmp(argv[0], "delete"))	{
		parse_delete(argc, argv);
	}

	else if(!strcmp(argv[0], "search"))	{
		retval = parse_search(argc, argv);

		if(retval == 1)	{
			// now wait for 1.2*MsgLifetime
			tv.tv_sec = (time_t)1.2*MsgLifetime;
			tv.tv_usec = 0;
			select(0, NULL, NULL, NULL, &tv);
		}
		retval = 0;
	}

	else if(!strcmp(argv[0], "get"))	{
		if(strcmp(prv_command, "search") && strcmp(prv_command, "get"))
			printf("\tget: must be followed by 'serach' or 'get'\n");
		else	{
			if(parse_get(argc, argv) == 1)	{
				tv.tv_sec = (time_t)1.2*MsgLifetime;
				tv.tv_usec = 0;
				select(0, NULL, NULL, NULL, &tv);
			}
		}
	}

	else if(!strcmp(argv[0], "shutdown"))	{
		if(argc != 1)	{
			printf("\tFormat: shutdown\n");
			return -1;
		}

		printf("\tShutting down... \n");
		// now shut down the node, kill all threads, free all resources
		// and kill the process gracefully
		// send a SIGTERM signal to the 'main' thread
		shutdown_flag = true;	// set the shutdown flag for other threads
		pthread_kill(main_thread, SIGTERM);

		pthread_exit(NULL);	// shut down this thread
	}

	else		{	// invalid command
		printf("\t%s: Command not found\n", argv[0]);
		return -1;
	}

	strcpy(prv_command, argv[0]); // save this command
				// it is used for 'GET' function
	return 0;
}

// this funciton is used for the 'status' command
//	1. first, the command is performed for this node for itself.
//	2. then the command is flooded properly to the whole network (i.e. an event is
//			is put in the 'dispatcher' for flooding
//	3. the UOID of the flooded message is saved in the UOID list
int status_cmd(char req_type, int TTL, char *extfile)
{
	// for PART 1, req_type will always be NEIGHBORS
	// for PART 2, it can be either NEIGHBORS or FILES

	// make a event and put it in the dispatcher
	int size = 0, i, j;
	event status_event;
	status_event.flooding = true;			// status is to be flooded
	status_event.port_num = Port;			// used for logging purpose
	status_event.header.msg_type = STRQ;	// status request
	GetUOID(node_inst_id, "msg", status_event.header.UOID, 
					sizeof status_event.header.UOID);
	status_event.header.TTL = (int8_t)TTL;
	status_event.header.reserved = 0;		// always
	unsigned char status_type = req_type;
	size+= sizeof(status_type);
	status_event.buffer = SetBuffer(size);
	memcpy(&status_event.buffer[0], &status_type, size);
	
	status_event.header.data_length = size;

	// Add this UOID in the UOID_list
	pthread_mutex_lock(&UOID_list_lock);
	msg_entry new_entry;
	memcpy(new_entry.UOID, status_event.header.UOID, 20);
	new_entry.port_num = Port;	// Put my port
	Add_UOID(new_entry);
	pthread_mutex_unlock(&UOID_list_lock);

	// open the file extfile and put the first line in it
	pthread_mutex_lock(&extfile_lock);
	fp_extfile = fopen(extfile, "w");
	if(fp_extfile == NULL)	{
		printf("\tSTATUS: unable to write %s\n", extfile);
		return -1;
	}
	status_response_data response;
	int num_records;

	if(req_type == NEIGHBORS)	{
		fputs("V -t * -v 1.0a5\n", fp_extfile);
		fflush(fp_extfile);

		// also put the data on stdout
		// so that the user can see the data on the fly
		add_nam = 1;
		seen = (int *)realloc(seen, add_nam*sizeof(int));
		if(seen == NULL)	{
			printf("realloc for seen failed\n");
			kill_threads();
			return 0;
		}
		seen[add_nam -1] = Port;
		//fputs("V -t * -v 1.0a5\n", stdout);
		char my_node[MAXLINE];

		// if this node is beacon, then show in red color
		// else, show in brown color
		if(my_node_type == BEACON)
			sprintf(my_node, "n -t * -s %d -c red -i black\n", Port);
		else
			sprintf(my_node, "n -t * -s %d -c brown -i black\n", Port);
		fputs(my_node, fp_extfile);
		fflush(fp_extfile);

		//fputs(my_node, stdout);
		//fflush(stdout);	
	}
	else	{
		memset(response.UOID, '\0', 20);
		response.host_info_len = 0;
		response.host_port = Port;	// my port
		response.hostname = SetBuffer(strlen(hostname));
		strcpy(response.hostname, hostname);	// my hostname

		pthread_mutex_lock(&lockListMutex);		
		num_records = GetLength_index(fnameList);
		//printf("records = %d\n", num_records);
		response.record_len = (int *)malloc(num_records * sizeof(int));
		response.n_host_port = (int *)malloc(num_records * sizeof(int));
		response.n_hostname = (char **)malloc(num_records * sizeof(char *));

		// put the metadata of each file into the structure
		struct stat stat_buf;
		FILE *fp;
		char meta_name[MAXLINE], c;
		int *file_nums;
		file_nums = (int *)malloc(num_records * sizeof(int));
		GetFileNumbers(fnameList, file_nums);

		for(i=0; i<num_records; i++)	{
			response.n_host_port[i] = 0;	// not needed for FILES
			memset(meta_name, '\0', MAXLINE);
			sprintf(meta_name, "%s/%s/%d.meta", HomeDir,FilesDir,file_nums[i]);
			stat(meta_name, &stat_buf);
			
			if( (fp = fopen(meta_name, "r")) == NULL)	{
				response.n_hostname[i] = SetBuffer(0);
			}
			else	{
				response.n_hostname[i] = SetBuffer(stat_buf.st_size +1);
				j = 0;
				c = fgetc(fp);
				do{
					response.n_hostname[i][j++] = c;
					c = fgetc(fp);
				}while(c!= EOF);
				response.n_hostname[i][j] = '\0';
				fclose(fp);
			}
			response.record_len[i] = 0;	// not needed
		}
		pthread_mutex_unlock(&lockListMutex);
	}
	pthread_mutex_unlock(&extfile_lock);
	if(req_type == FILES)
		write_meta(response, num_records);

	// put this event into the dispatcher
	pthread_mutex_lock(&dispatcher_lock);
	Append(dispatcher_first, status_event);
	pthread_cond_signal(&dispatcher_cond);
			// signal the dispatcher that something is there in the list
	pthread_mutex_unlock(&dispatcher_lock);
	//printf("I put it in dispatcher for flooding, port num %d\n", Port);
	return 0;
}

// this funciton is used for the 'delete' command
//	1. first, the command is performed for this node for itself.
//	2. then the command is flooded properly to the whole network (i.e. an event is
//			is put in the 'dispatcher' for flooding
//	3. the UOID of the flooded message is saved in the UOID list
int parse_delete(int argc, char** argv)
{
	if(argc != 4)	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}

	// put the data into a temp in_temp file
	FILE *fp;
	char in_temp[] = "in_temp";
	char out_temp[] = "out_temp";
	fp = fopen(in_temp, "w");
	int i;
	for(i=1; i<=3; i++)	{
		fputs(argv[i], fp);
		fputs("\r\n", fp);
	}
	fclose(fp);

	char *result =NULL;
	unsigned char SHA1[20], Nonce[20];
	char *filename;

	// parse the 3 parameters of user command
	// first, the filename
	result = strtok(argv[1], "=");
	if(result == NULL || strcmp(result, "FileName"))	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}
	result = strtok(NULL, "=");
	filename = SetBuffer(strlen(result));
	strcpy(filename, result);

	// second, the SHA1
	result = strtok(argv[2], "=");
	if(result == NULL || strcmp(result, "SHA1"))	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}
	result = strtok(NULL, "=");
	if(result == NULL || strlen(result) != 40)	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}
	XtoChar(result, SHA1);	// converts the 40 bytes string into SHA1 20 byte string

	// third and finally, the Nonce
	result = strtok(argv[3], "=");
	if(result == NULL || strcmp(result, "Nonce"))	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}
	result = strtok(NULL, "=");
	if(result == NULL || strlen(result) != 40)	{
		printf("\tFormat: delete FileName=foo SHA1=6b6c... Nonce=fe18...\n");
		return -1;
	}
	XtoChar(result, Nonce);

	// command line parsing done... now
	// 1. delete this file from this node
	// 2. sign this message with this node's private key and
	//		flood this message to the whole network

	// 2. signing this message and flooding it
	int retval;
	retval = signCertificate(certificateFile, privateKeyFile, in_temp, out_temp);
	if(!retval)	return -1;
	remove(in_temp);
	
	// 1. delete this file from this node, if it is the owner
	pthread_mutex_lock(&lockListMutex);
	int f = getFileNumber_index(fnameList, filename, Nonce);
	char pemFilename[MAXLINE];
	sprintf(pemFilename,"%s/%s/%d.pem", HomeDir, FilesDir, f);
	if(verifyCertificate(out_temp, pemFilename) != -1)	{
		pthread_mutex_unlock(&lockListMutex);
		deleteFile(filename,SHA1,Nonce);
	}
	else	{
		pthread_mutex_unlock(&lockListMutex);
		//printf("file not deleted\n");
	}

	// now out_temp file contains the data to be sent for flooding
	event del_event;
	del_event.flooding = true;			// status is to be flooded
	del_event.port_num = Port;			// used for logging purpose
	del_event.header.msg_type = DELT;	// status request
	GetUOID(node_inst_id, "msg", del_event.header.UOID, 
					sizeof del_event.header.UOID);
	del_event.header.TTL = (int8_t)TTL;
	del_event.header.reserved = 0;		// always

	struct stat stat_buf;
	stat(out_temp, &stat_buf);	//get the details of this file
	// we just need the size of this file
	del_event.header.data_length = stat_buf.st_size;
	del_event.buffer = SetBuffer(stat_buf.st_size);

	// now read the file and copy it into buffer
	fp = fopen(out_temp, "r");
	for(i=0; i<del_event.header.data_length; i++)
		del_event.buffer[i] = fgetc(fp);
	fclose(fp);
	remove(out_temp);		// no longer used

	// Add this UOID in the UOID_list
	pthread_mutex_lock(&UOID_list_lock);
	msg_entry new_entry;
	memcpy(new_entry.UOID, del_event.header.UOID, 20);
	new_entry.port_num = Port;	// Put my port
	Add_UOID(new_entry);
	pthread_mutex_unlock(&UOID_list_lock);

	// now put this event into the dispatcher
	pthread_mutex_lock(&dispatcher_lock);
	Append(dispatcher_first, del_event);
	pthread_cond_signal(&dispatcher_cond);
			// signal the dispatcher that something is there in the list
	pthread_mutex_unlock(&dispatcher_lock);

	return 0;
}


// kill all the threads and shut down the node
// informs all the nodes that it's time to go back home and have dinner
void kill_threads()
{
	// killing all the threads
	shutdown_flag = true;	// set the shutdown flag for other threads

	pthread_kill(main_thread, SIGTERM);
}
