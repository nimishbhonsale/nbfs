#include "sv_node.h"

bool		reset_flag;
int			my_node_type;

// variables to store the configuration parameters
int				Port;
unsigned int	Location;
char			*FilesDir;
char			*HomeDir;
char			*LogFilename;
int				AutoShutdown;
int				TTL;
int				MsgLifetime;
int				InitNeighbors;
int				JoinTimeout;
int				KeepAliveTimeout;
int				MinNeighbors;
int				NoCheck;
float			CacheProb;
float			StoreProb;
float			NeighborStoreProb;
int				CacheSize;
int				PermSize;

int				Retry;
char			**beacon_host;
int				*beacon_port;
int				beacon_count;
int				failed_neighbors;

// file needed to write the neighbor's details for non-beacon nodes
char *neighbor_file;
char *privateKeyFile;
char *certificateFile;
char *sha1_index_file;
char *name_index_file;
char *keyword_index_file;

FILE *fp_log = NULL;	// pointer for LOG file

// extern thread declaration
extern void *server_t(void *);
extern void *client_t(void *);
extern void *dispatch(void *);
extern void *countdown(void *);
extern void *std_input(void *);
extern void *join_t(void *);
extern void *join_ask(void *);
extern void *dump(void *);

// structure to pass parameters to the thread
thread_data *thread_param;

char *node_inst_id;		// stores the instance of a node
char hostname[MAXLINE];
int active_conn[FD_SETSIZE];
int num_neighbor = 0;
int count_hello = 0;	// number of threads for HLLO
int count_join = 0;		// number of threads for JNRQ
bool rejoin_flag = false;	// should the node rejoin?
bool firsttime = true;	// is the node making a soft reboot?

// extern functions declaration
extern void node_init(int argc, char **argv);
extern bool parse_neighbors();

// Variables for LRU.
long cacheFreeSize;
int cacheFilePiority;

// list headers
event *dispatcher_first;	// list for dispatcher
event *connection_first[FD_SETSIZE];	// list for connection threads
indexNode *fnameList;		// list for filename map
indexNode *sha1List;		// list for sha1 map
indexNode *kwordList;		// list for keyword map

// pthread declaration (used for the project)
pthread_t main_thread;
pthread_t *join_thread;
pthread_t timer;
pthread_t dispatcher;
pthread_t *send_to_beacon;
pthread_t *send_to_neighbors;
pthread_t cmd_interface;
pthread_t join_network;
pthread_t *listen_to_beacon;
pthread_t *join_handler;
pthread_t dumper;

sigset_t new_set;		// signal masking for command line thread
void sigterm_handler(int signum)
{
	signal(SIGTERM, SIG_IGN);		// do nothing, just deliver the signal
}
void alarm_handler(int signum)
{
	kill_threads();		// alarm goes off, AutoShutDown the node
	signal(SIGALRM, SIG_IGN);
}

int main(int argc, char *argv[])
{

	FilesDir = SetBuffer(5);
	strcpy(FilesDir,"files");

start:
	int i=0;

	if(rejoin_flag)	{
		// delete all the files in the home directory
		// this is a soft restart as the node is no longer
		// connected to the core of the SERVANT
		//printf("deleting file: %s\n", neighbor_file);
		remove(neighbor_file);

		// also remove the created files if -reset is given
		msg_header header;
		memset(&header, 0, HEADER_LEN);
		char temp_name[MAXLINE];
		if(reset_flag && firsttime)	{
			remove(privateKeyFile);
			remove(certificateFile);
			remove(sha1_index_file);
			remove(name_index_file);
			remove(keyword_index_file);
			remove(LogFilename);
			memset(temp_name,'\0',MAXLINE);
			sprintf(temp_name,"%s/%s",HomeDir,FilesDir);
			makeDirectory(temp_name);
			for(i=1; i<=fileNumber; i++)	{
				sprintf(temp_name, "%s/%s/%d.data", HomeDir,FilesDir, i);
				remove(temp_name);
				sprintf(temp_name, "%s/%s/%d.meta", HomeDir,FilesDir, i);
				remove(temp_name);
				sprintf(temp_name, "%s/%s/%d.pem", HomeDir,FilesDir, i);
				remove(temp_name);
			}
			sprintf(temp_name, "%s/filenum.txt", HomeDir);
			remove(temp_name);
		}
		else
			write_log('c', 0, header, "node restarting...\n");

		//printf("file deleted\n");
		rejoin_flag = false;
		shutdown_flag = false;
		firsttime = false;	// this node is rejoining, so firsttime is false now
							// useful for checking -reset flag
	}

	signal(SIGTERM, sigterm_handler);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGALRM, alarm_handler);
	//signal(SIGINT, SIG_IGN);
	sigemptyset(&new_set);
	sigaddset(&new_set, SIGINT);	// unmask the SIGINT for command line thread
	pthread_sigmask(SIG_BLOCK, &new_set, NULL);	// mask SIGINT

	// first calculate the node_instance_id for this startup of node
	time_t start_time = time(NULL);
	gethostname(hostname, sizeof hostname);
	main_thread = pthread_self();	// thread ID of the main() thread
										// used for 'Shutdown' command
	node_inst_id = GetNodeInstID(hostname, Port, start_time);

	// now check the command line arguments and parse the ini file
	node_init(argc, argv);
	alarm(AutoShutdown);	// activate the AutoShutdown alarm

	// create one client thread for each beacon node
	int rc=0;

	// initialize all the timeout values
	for(i=0; i<FD_SETSIZE; i++)	{
		active_conn[i] = -1;
		timeout_join[i] = -1;
		timeout_alive[i] = -1;
		timeout_node_dead[i] = -1;
	}
	timeout_check = -1;

	// initialize all the mutex and CVs
	pthread_mutex_init(&timout, NULL);
	pthread_mutex_init(&msg_parse_lock, NULL);
	pthread_mutex_init(&dispatcher_lock, NULL);
	pthread_cond_init(&dispatcher_cond, NULL);
	pthread_mutex_init(&extfile_lock, NULL);
	pthread_mutex_init(&UOID_list_lock, NULL);
	pthread_mutex_init(&FileID_list_lock, NULL);
	for(i=0; i<FD_SETSIZE; i++)
		pthread_mutex_init(&conn_list_lock[i], NULL);
	pthread_mutex_init(&min_neighbor_lock, NULL);
	pthread_mutex_init(&logfile_lock, NULL);
	pthread_mutex_init(&fileNumMutex, NULL);
	pthread_mutex_init(&lockListMutex, NULL);
	pthread_mutex_init(&tempfile_mutex, NULL);
	pthread_mutex_init(&stor_get_lock, NULL);

	// initialize the lists
	dispatcher_first = NULL;
	for(i=0; i<FD_SETSIZE; i++)
		connection_first[i] = NULL;
	fnameList = NULL;
	sha1List = NULL;
	kwordList = NULL;

	// initialize the file number (take the saved value)
	FILE *fp;
	char line[MAXLINE];
	char filenum_name[MAXLINE];
	char *result = NULL;
	sprintf(filenum_name, "%s/filenum.txt", HomeDir);
	if( (fp = fopen(filenum_name, "r")) == NULL)	{
		fileNumber = 0;
		cacheFilePiority = 0;
	}
	else	{
		fgets(line, MAXLINE, fp);
		result = strtok(line, " ");
		fileNumber = atol(result);
		result = strtok(NULL, " ");
		cacheFilePiority = atol(result);
		fclose(fp);
	}
	tempfile_count = 1;

	// initialize the random number generator with time seed
	time_t time_now = (time_t)0;
	time(&time_now);
	srand48((long)time_now);

	// create private key and public key for this node
	privateKeyFile = SetBuffer(MAXLINE);
	certificateFile = SetBuffer(MAXLINE);
	sha1_index_file = SetBuffer(MAXLINE);
	name_index_file = SetBuffer(MAXLINE);
	keyword_index_file = SetBuffer(MAXLINE);

	sprintf(privateKeyFile, "%s/private.pem", HomeDir);
	sprintf(certificateFile, "%s/cert.pem", HomeDir);
	sprintf(sha1_index_file, "%s/sha1_index", HomeDir);
	sprintf(name_index_file, "%s/name_index", HomeDir);
	sprintf(keyword_index_file, "%s/kwrd_index", HomeDir);

	// if -reset option is given, then make a soft reboot
	// and join the network again
	// do this only if this is the first start, not a soft reboot
	if(reset_flag && firsttime)	{
		parse_neighbors();	// get the ini_neighbor_list filename
		rejoin_flag = true;
		goto start;			// make a soft restart
	}

	// create signature files
	//struct stat statFile;
	pthread_mutex_lock(&lockListMutex);
	FILE * certFileExists = fopen(certificateFile,"r");
	if(certFileExists == NULL)
	{
		cacheFreeSize = CacheSize * 1024;
		if(createKey(privateKeyFile,certificateFile) == -1)	{
			printf("\tError creating public and private keys\n");
			exit(1);
		}
	}
	else
	{
		fclose(certFileExists);
	}
	pthread_mutex_unlock(&lockListMutex);

	// create index lists from disk images
	createIndexData(&fnameList, &sha1List, \
								&kwordList, HomeDir);

	// Create files directory under Home Directory.
	char * temp_name;
	temp_name = SetBuffer(MAXLINE);
	memset(temp_name,'\0',MAXLINE);
	sprintf(temp_name,"%s/%s",HomeDir,FilesDir);
	makeDirectory(temp_name);

	// create the 'tmp' director for temp files
	// if it is already there, remove it
	char temp_path[MAXLINE];
	sprintf(temp_path, "%s/tmp", HomeDir);
	removeDirectory(temp_path);
	// then create it
	memset(temp_name,'\0',MAXLINE);
	sprintf(temp_name,"%s/tmp",HomeDir);
	makeDirectory(temp_name);



	rc = pthread_create(&timer, NULL, countdown, (void *)0);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(-1);
	}

	rc = pthread_create(&dispatcher, NULL, dispatch, (void *)0);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(-1);
	}

	rc = pthread_create(&dumper, NULL, dump, (void *)0);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(-1);
	}

	// check wether this node is a non beacon node or not
	// if no, then continue with the next part
	// else, check if init_neighbor_list file in its home directory
	// if yes, then skip the join part and go to the next part
	// else, do the join process and write the init_neighbor_list file
	// then, using this file, say 'hello' to neighbors as usual
	bool fileFound = false;
	do{
		if(my_node_type == REGULAR)
			fileFound = parse_neighbors();
		else fileFound = true;
		void *stat;	// to take the return value of this thread
						// indicates if joining is successful or not
		if(my_node_type == REGULAR && fileFound == false)	{
			rc = pthread_create(&join_network, NULL,
							join_ask, (void *)0);
			if (rc){
				printf("ERROR; return code from pthread_create() is %d\n", rc);
				exit(-1);
			}
			pthread_join(join_network, &stat);
				// wait till joining is completed
			if(shutdown_flag == true)	{
				//printf("OHHHHH\n");
				// either there is no beacon node OR
				// Home-directory specified in ini file doesn't exist
				// first, signal the dispatcher so that it can come out of WAIT
				pthread_mutex_lock(&dispatcher_lock);
				pthread_cond_signal(&dispatcher_cond);
				pthread_mutex_unlock(&dispatcher_lock);
				pthread_join(timer, NULL);

				pthread_join(dispatcher, NULL);
				return -1;	// shutdown
			}
		}
	}while(!fileFound);

	// if the init_neighbor_list file contains less than 'MinNeighbors'
	// number of neighbors, than shut down
	if(my_node_type == REGULAR && neighbor_count < MinNeighbors)	{
		shutdown_flag = true;
		pthread_mutex_lock(&dispatcher_lock);
		pthread_cond_signal(&dispatcher_cond);
		pthread_mutex_unlock(&dispatcher_lock);
		pthread_join(timer, NULL);

		pthread_join(dispatcher, NULL);
		printf("\tNot enought neighbors... shutting down\n");
		return -1;	// shut down
	}

	// if status == 0, then 'join' was not completed successfully
	// so, shutdown this node gracefully

	if(my_node_type == BEACON)	{		// do this for beacon nodes
		send_to_beacon = (pthread_t *)malloc(beacon_count*sizeof(pthread_t));
		int temp_port;
		for(i=0; i<beacon_count; i++)	{
			temp_port = beacon_port[i];
			if(Port == temp_port)
				continue;
			rc = pthread_create(&send_to_beacon[i], NULL,
							client_t, (void *)temp_port);
			if (rc){
				printf("ERROR; return code from pthread_create() is %d\n", rc);
				exit(-1);
			}
		}
	}
	else	{		// do this for non beacon nodes
		pthread_mutex_lock(&min_neighbor_lock);
		failed_neighbors = neighbor_count; // to check that atleast MinNeighbors
										   // number of neighbors are up
		pthread_mutex_unlock(&min_neighbor_lock);

		send_to_neighbors = (pthread_t *)malloc(neighbor_count*sizeof(pthread_t));
		for(i=0; i<neighbor_count; i++)	{
			//printf("a client forked\n");
			rc = pthread_create(&send_to_neighbors[i], NULL,
								client_t, (void *)neighbor_port[i]);
			if (rc){
				printf("ERROR; return code from pthread_create() is %d\n", rc);
				exit(-1);
			}
		}
	}

	// make a thread to handle command line interface with user
	// it will mimic a FTP client program
	// UNBLOCK SIGINT before creating this thread
	pthread_sigmask(SIG_UNBLOCK, &new_set, NULL);	// mask SIGINT
	rc = pthread_create(&cmd_interface, NULL, std_input,(void *)0);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(-1);
	}
	// BLOCK SIGINT again after thread is created
	pthread_sigmask(SIG_BLOCK, &new_set, NULL);	// mask SIGINT

	// listen on my port for incoming connections
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct sockaddr_in my_addr;	// my address information
	struct sockaddr_in their_addr; // connector's address information
	socklen_t sin_size;
	int yes=1;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);
	}

	my_addr.sin_family = AF_INET;		 // host byte order
	my_addr.sin_port = htons(Port);	 // short, network byte order
	my_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
	memset(my_addr.sin_zero, '\0', sizeof my_addr.sin_zero);

	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof my_addr) == -1) {
		perror("bind");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");		// keep listening
		exit(1);
	}

	rc=0;	// to check the error code for pthread_create
	count_hello = 0;	// number of threads for HLLO
	count_join = 0;		// number of threads for JNRQ

	char *buffer;
	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
			if(errno == EINTR)	{
				//printf("accept: got the signal\n");
				break;		// shutdown command given, break from the loop
			}
			exit(1);
		}
		msg_header Header;
		// recieve a reply HELLO msg from the remote node
		// first receive the header
		memset(&Header, 0, HEADER_LEN);
		buffer = SetBuffer(HEADER_LEN);
		int numbytes=0;
		if((numbytes = recv(new_fd, buffer, HEADER_LEN, 0)) == -1)	{
			perror("recv");
			exit(1);
		}

		memcpy(&Header.msg_type, &buffer[0], 1);
		memcpy(&Header.UOID[0], &buffer[1], 20);
		memcpy(&Header.TTL, &buffer[21], 1);
		memcpy(&Header.reserved, &buffer[22], 1);
		memcpy(&Header.data_length, &buffer[23], 4);
		free(buffer);

		thread_param = (thread_data *) malloc(sizeof(thread_data));
		thread_param->new_fd = new_fd;
		thread_param->Header = Header;

		listen_to_beacon = (pthread_t *) malloc(sizeof(pthread_t));
		join_handler = (pthread_t *) malloc(sizeof(pthread_t));

		if(Header.msg_type == HLLO)	{
			thread_param->thread_id = count_hello;
			//printf("HLLO: %d\n", new_fd);
			listen_to_beacon =
				(pthread_t *) realloc(listen_to_beacon, (count_hello+1)*sizeof(pthread_t));
			rc = pthread_create(&listen_to_beacon[count_hello],
							NULL, server_t, (void *)thread_param);
			if (rc){
				printf("ERROR; return code from pthread_create() is %d\n", rc);
				exit(-1);
			}
			count_hello++;
		}

		else if(Header.msg_type == JNRQ)	{
			thread_param->thread_id = count_join;
			//printf("JNRQ: %d\n", new_fd);
			join_handler =
				(pthread_t *) realloc(join_handler, (count_join+1)*sizeof(pthread_t));
			rc = pthread_create(&join_handler[count_join],
							NULL, join_t, (void *)thread_param);
			if (rc){
				printf("ERROR; return code from pthread_create() is %d\n", rc);
				exit(-1);
			}
			count_join++;
		}
	}

	//printf("finishing\n");
	fflush(stdout);
	close(sockfd);	// close the listening socket

	// wait till all the threads are finished
	pthread_join(timer, NULL);

	// first, signal the dispatcher so that it can come out of WAIT
	pthread_mutex_lock(&dispatcher_lock);
	pthread_cond_signal(&dispatcher_cond);
	pthread_mutex_unlock(&dispatcher_lock);
	pthread_join(dispatcher, NULL);

	for(i=0; i<count_hello; i++)
		pthread_join(listen_to_beacon[i], NULL);
	for(i=0; i<count_join; i++)
		pthread_join(join_handler[i], NULL);

	if(my_node_type == BEACON)	{
		for(i=0; i<beacon_count; i++)
			if(beacon_port[i] != Port)	{
				pthread_join(send_to_beacon[i], NULL);
			}
	}
	else	{
		for(i=0; i<neighbor_count; i++)
			pthread_join(send_to_neighbors[i], NULL);
	}

	pthread_join(cmd_interface, NULL);


	// free all the rosources and mutex and CVS
	// first, delete the 'tmp' directory
	sprintf(temp_path, "%s/tmp", HomeDir);
	removeDirectory(temp_path);
	fclose(fp_log);
	free(LogFilename);
	free(neighbor_port);
	free(beacon_host);
	free(beacon_port);

	pthread_mutex_destroy(&timout);
	pthread_mutex_destroy(&msg_parse_lock);
	pthread_mutex_destroy(&dispatcher_lock);
	pthread_cond_destroy(&dispatcher_cond);
	pthread_mutex_destroy(&extfile_lock);
	pthread_mutex_destroy(&UOID_list_lock);
	pthread_mutex_destroy(&FileID_list_lock);
	for(i=0; i<FD_SETSIZE; i++)
		pthread_mutex_destroy(&conn_list_lock[i]);
	pthread_mutex_destroy(&min_neighbor_lock);
	pthread_mutex_destroy(&logfile_lock);
	pthread_mutex_destroy(&fileNumMutex);
	pthread_mutex_destroy(&lockListMutex);
	pthread_mutex_destroy(&tempfile_mutex);
	pthread_mutex_destroy(&stor_get_lock);

	// if the node is to rejoin the network, then go back to start
	if(rejoin_flag)
		goto start;	// sorry for using GOTO :(

	return 0;
}
