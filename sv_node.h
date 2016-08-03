#define FD_SETSIZE 64
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <sys/stat.h>
#include <pthread.h>

#include "utilities.h"
#include "list.h"

#define BEACON 1
#define REGULAR 0
#define HEADER_LEN 27		// length of common header
							// for all the messages

// message types
#define HLLO	0xFA
#define KPAV	0xF8
#define STRQ	0xAC
#define STRS	0xAB
#define JNRQ	0xFC
#define JNRS	0xFB
#define NTFY	0xF7
#define CKRQ	0xF6
#define CKRS	0xF5
#define STOR	0xCC
#define DELT	0xBC
#define SHRQ	0xEC
#define SHRS	0xEB
#define GTRQ	0xDC
#define GTRS	0xDB

#define BACKLOG 10
#define MAXARG 10

#define NEIGHBORS 0x01
#define FILES 0x02

typedef struct thread_data	{
	int thread_id;
	int new_fd;
	msg_header Header;
};

extern bool			hello_flag;	// to keep only one connection between beacons
extern bool			reset_flag;
extern bool			firsttime;
extern int			my_node_type;
// extern variables needed by most of the files
extern int			Port;
extern unsigned int	Location;
extern char			*HomeDir;
extern char			*LogFilename;
extern int			AutoShutdown;
extern int			TTL;
extern int			MsgLifetime;
extern int			InitNeighbors;
extern int			JoinTimeout;
extern int			KeepAliveTimeout;
extern int			MinNeighbors;
extern int			NoCheck;
extern float		CacheProb;
extern float		StoreProb;
extern float		NeighborStoreProb;
extern int			CacheSize;
extern int			PermSize;
extern char			*FilesDir;

// entries for cache
extern long cacheFreeSize;
extern int cacheFilePiority;

extern int			Retry;
extern char			**beacon_host;
extern int			*beacon_port;
extern int			beacon_count;
extern int			failed_neighbors;

extern char			*node_inst_id;
extern char			hostname[MAXLINE];

// entries for neighbors for non beacon nodes
extern char			**neighbor_hostname;
extern int			*neighbor_port;
extern int			neighbor_count;
extern char			*neighbor_file;

extern char			*privateKeyFile;
extern char			*certificateFile;
extern char			*sha1_index_file;
extern char			*name_index_file;
extern char			*keyword_index_file;

extern unsigned char	**get_list;
extern int			get_count;
extern char			*get_file;

extern long int		fileNumber;	// filenumber at startup
extern FILE *fp_extfile;// pointer for NAM FILE
extern FILE *fp_log;	// pointer for LOG FILE
extern bool shutdown_flag;	// flag for shutting down the node
extern bool rejoin_flag;
extern void kill_threads();

extern pthread_mutex_t hello_flag_lock;
extern pthread_mutex_t timout;
extern pthread_mutex_t msg_parse_lock;
extern pthread_mutex_t dispatcher_lock;
extern pthread_cond_t  dispatcher_cond;
extern pthread_mutex_t conn_list_lock[FD_SETSIZE];
extern pthread_mutex_t extfile_lock;
extern pthread_mutex_t UOID_list_lock;
extern pthread_mutex_t FileID_list_lock;
extern pthread_mutex_t min_neighbor_lock;
extern pthread_mutex_t logfile_lock;
extern pthread_mutex_t fileNumMutex ;
extern pthread_mutex_t lockListMutex;
extern pthread_mutex_t stor_get_lock;

// threads extern declaration
extern pthread_t main_thread;
extern pthread_t *join_thread;
extern pthread_t timer;
extern pthread_t dispatcher;
extern pthread_t *send_to_beacon;
extern pthread_t *send_to_neighbors;
extern pthread_t cmd_interface;
extern pthread_t join_network;
extern pthread_t *listen_to_beacon;
extern pthread_t *join_handler;

extern int timeout_join[FD_SETSIZE];
extern int timeout_alive[FD_SETSIZE];
extern int timeout_node_dead[FD_SETSIZE];
extern int timeout_check;
extern int msg_parse(int, msg_header, int);
extern void msg_forward(int, event, int);
extern int write_nam(status_response_data, int);
extern int write_meta(status_response_data, int);
extern void write_log(char, int, msg_header, char*);
extern int getFileNumber();
extern int createKey(char * privateKey, char * publicKey);
extern int signCertificate(char * privateKey, char * publicKey, \
				char * inFilename, char* outFilename);
extern int verifyCertificate(char * filename, char * certFilename);
extern int generateBitVector(int *bitVector, char * word);
extern int deleteFile(char * filename,unsigned char *sha1,unsigned char *nonce);
extern bool coin_outcome(double prob);
extern bool insertFileInCache(indexNode* head,long);
extern bool setLRUPriority(indexNode* head, char *,unsigned char *);
extern int setLRUPriority_by_filenum(int);
extern int setNodePriority(indexNode* head, char *,unsigned char *,int);
extern int bring_to_permanent(int file_num);
extern long removeLRUNode(indexNode* head);
extern int setDeleteFlag(indexNode* head, int fileNumber);
extern int setDeleteFlagForFile(indexNode* head, char *filename,\
							unsigned char *nonce);

extern int active_conn[FD_SETSIZE];
extern int num_neighbor;

extern event *dispatcher_first;	// list for dispatcher
extern event *connection_first[FD_SETSIZE];	
								// list for connection threads
extern indexNode *fnameList;	// list for filename map
extern indexNode *sha1List;		// list for sha1 map
extern indexNode *kwordList;	// list for keyword map

#ifndef MIN
#define MIN(A,B) (((A)>(B)) ? (B) : (A))
#endif /* ~MIN */

#define ABS(A) ((A >= 0) ? A : (-A))

#define Assert(condition, err_msg)	\
	if(!(condition))	{			\
		printf(err_msg);			\
		exit(EXIT_FAILURE);			\
	}
