#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <ctype.h>
#include "index_list.h"

#ifndef MIN
#define MIN(A,B) (((A)>(B)) ? (B) : (A))
#endif /* ~MIN */

#define MAXLINE 256
extern pthread_mutex_t lockListMutex;
extern indexNode *fnameList;	// list for filename map
extern indexNode *sha1List;		// list for sha1 map
extern indexNode *kwordList;	// list for keyword map
extern char	*HomeDir;
extern char	*FilesDir;
extern long int cacheFreeSize;
extern int generateBitVector(int *bitVector, char * word);

// function declarations
unsigned char *GetUOID(
	  char *node_inst_id,
	  char *obj_type,
	  unsigned char *uoid_buf,
	  unsigned int uoid_buf_sz);

char *GetNodeInstID(
		char *hostname, int portnum, time_t start_time);

char *SetBuffer(int size);

extern pthread_mutex_t tempfile_mutex;
extern int tempfile_count;
extern char *get_tempfile_name();

//functions and structures for parsing METADATA files
typedef struct meta	{
	char			*FileName;
	long int		FileSize;
	unsigned char	SHA1[20];
	unsigned char	Nonce[20];
	int				KeywordCount;
	char			**Keywords;
	int				BitVector[256];
 } parsed_metadata;

parsed_metadata parse_metafile(char *metafile);
void XtoChar(char *tempStr, unsigned char *id);
int parse_signed(char *signed_file, char *filename, \
					unsigned char *SHA1,	\
					unsigned char *Nonce);

int add_lists(bool isCache, parsed_metadata metadata, int filenum);
	// adds a file into the index files
void print_get(parsed_metadata, unsigned char *, int);

int generateBitVector(int *bitVector, char * word);
int makeDirectory(char * filepath);
void removeDirectory(char * path);
int getFileNumberList( int searchType, char * searchValue, int ** fileNumber);
int searchKeywords(int ** fileNumbers,char * keywords);
