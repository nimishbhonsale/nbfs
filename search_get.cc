// this file has all the client side functions related to command 'search' or 'get'
// the command is given as it is to this file and the following is done:
//	1. the command is parsed properly and the details are stored in variables.
//	2. then the command is performed for this node for itself.
//	3. then the command is flooded properly to the whole network (i.e. an event is
//			is put in the 'dispatcher' for flooding
//	4. the UOID of the flooded message is saved in the UOID list
//	the functions return to the commandline thread again
#include "sv_node.h"

// data needed for get
char *get_file;
unsigned char **get_list;
int get_count;
extern void get_storefile(int file_num, char *get_file);

// search:
//		search filename=foo
//		search sha1hash=bar
//		search keywords="key1 key2 key3" ... (any number of keywords)
int parse_search(int argc, char **argv)
{
	int i, search_type = 0;
	if(argc < 2)	{
		printf("\tsearch: too few arguments\n");
		return -1;
	}

	char *result = NULL;
	result = strtok(argv[1], "=");
	if(result == NULL)	{
		printf("\tsearch: invalid arguments\n");
		return -1;
	}
	
	// determine the type of search
	if(!strcmp(result, "filename"))
		search_type = 1;
	else if(!strcmp(result, "sha1hash"))
		search_type = 2;
	else if(!strcmp(result, "keywords"))
		search_type = 3;
	else	{	// nothing
		printf("\tsearch: invalid arguments\n");
		return -1;
	}

	// if searched using filename or exact hash
	char *filename;
	unsigned char sha1hash[20];
	if(search_type == 1 || search_type == 2)	{
		if(argc != 2)	{
			printf("\tsearch filename=foo\n");
			return -1;
		}
		result = strtok(NULL, "=");
		if(result == NULL)	{
			printf("\tsearch: filename=foo\n");
			return -1;
		}

		if(search_type == 1)	{
			filename = SetBuffer(strlen(result));
			strcpy(filename, result);
		}
		else	{
			if(strlen(result) != 40)	{
				printf("\tsearch: sha1hash lenght must be 40 characters\n");
				return -1;
			}
			XtoChar(result, sha1hash);
		}
	}

	// keywords parsing
	char **keywords;
	int keyword_count;
	if(search_type == 3)	{
		// there can be any number of arguments
		// argv[2] to argv[n-1] contain the keywords
		// if there are more than 1 keyword then they must be in quotations
		result = strtok(NULL, "=");
		if(result == NULL)	{
			printf("\tsearch: keywords=\"key1 key2 key3\"\n");
			return -1;
		}
		strcpy(argv[1], result);	// copy it back to argv[1]

		// now parse each keyword and store them
		keywords = (char **)malloc(sizeof(char *));
		for(i=1; i<argc; i++)	{
			keywords = (char **)realloc(keywords, i*sizeof(char *));
			keywords[i-1] = SetBuffer(strlen(argv[i]));
			strcpy(keywords[i-1], argv[i]);
		}
		keyword_count = i-1;

		char *temp_word;
		unsigned int j;
		bool isFirstQuote = false, isLastQuote = false;

		// if there are quoatations, remove them now
		for(i=0; i<keyword_count; i++)	{
			temp_word = SetBuffer(strlen(keywords[i]));

			// only the first and last keywords can have quotations
			if(i==keyword_count -1 && 
					keywords[i][strlen(keywords[i])-1] == '"')	{
				keywords[i][strlen(keywords[i])-1] = '\0';
				isLastQuote = true;
			}

			if(i== 0 &&	keywords[i][0] == '"')	{
				for(j=0; j<strlen(keywords[i]) -1; j++)
					temp_word[j] = keywords[i][j+1];
				temp_word[j] = '\0';
				//keywords[i] = (char *)realloc(keywords[i], strlen(temp_word));
				strcpy(keywords[i], temp_word);
				isFirstQuote = true;
			}
			free(temp_word);
			
			// else, there are misplaced quotation marks
			if((i!= 0 && keywords[i][0] == '"') ||
				(i!= keyword_count -1 && keywords[i][strlen(keywords[i])-1] == '"')){
				printf("\tsearch: misplaced quotation marks\n");
				return -1;
			}
		}

		// check if starting and ending quotation marks are proper
		if(keyword_count >1 && (!isFirstQuote || !isLastQuote))	{
			printf("\tsearch: misplaced quotation marks\n");
			return -1;
		}
	}

	// parsing done!!! now, filename, sha1hash or keywords are available
	// Now, make a message and flood it on the whole network
	// Also, search this node's filesystem also for the same parameters
	// So, the final response should be a combination of this node and other
	// node's responses.

	// first, get this node's response directly
	// (WILL BE DONE LATER)

	// second, flood a search query to the whole network
	printf("\tsearching for files... (CTRL-C to terminate)\n\n");
	if(get_count != 0)
		free(get_list);

	get_count = 0;
	get_list = (unsigned char **)malloc((get_count +1) * sizeof(unsigned char *));
	search_data data;
	data.search_type = (uint8_t)search_type;

	switch(data.search_type)	{
		case 1: // filename search
			data.key = SetBuffer(strlen(filename));
			strcpy(data.key, filename);
			break;

		case 2:	// sha1hash search
			data.key = SetBuffer(20);
			memcpy(data.key, sha1hash, 20);
			break;

		case 3: // keyword search
			// put all the keywords separated by a space
			char temp[MAXLINE];
			strcpy(temp, keywords[0]);	// first keyword
			for(i=1; i<keyword_count; i++)	{
				strcat(temp, " ");
				strcat(temp, keywords[i]);
			}
			data.key = SetBuffer(strlen(temp));
			strcpy(data.key, temp);
			break;
	}	

	// now, first, apply the search query at this node and give the output
	// then, flood this query to the whole network and give the output
	// the flooding part will be done by msg-parse function

	// first, fetching the results from this node
	int file_count, *file_nums;
	pthread_mutex_lock(&lockListMutex);	
	if(data.search_type == 1 || data.search_type == 2)	{
		file_count = getFileNumberList(data.search_type,
							data.key, &file_nums);
	}
	else
		file_count = searchKeywords(&file_nums, data.key);

	for(i=0; i<file_count; i++)
		setLRUPriority_by_filenum(file_nums[i]);

	parsed_metadata meta;
	file_entry f_entry;
	unsigned char **FileID;
	FileID = (unsigned char **)malloc(file_count * sizeof(unsigned char *));
	char metaname[MAXLINE];
	for(i=0; i<file_count; i++)	{
		sprintf(metaname, "%s/%s/%d.meta", HomeDir, FilesDir, file_nums[i]);
		meta = parse_metafile(metaname);

		// add this in the get_list
		get_count++;
		get_list = (unsigned char **)realloc(get_list, 
								get_count * sizeof(unsigned char *));
		get_list[get_count -1] = (unsigned char *)malloc(20);

		// add this FileID to the list
		FileID[i] = (unsigned char *)malloc(20);
		GetUOID(node_inst_id, "file", FileID[i], 20);
		memcpy(get_list[get_count -1], FileID[i], 20);
		
		pthread_mutex_lock(&FileID_list_lock);
		memcpy(f_entry.FileID, FileID[i], 20);
		f_entry.file_num = file_nums[i];
		Add_FileID(f_entry);
		pthread_mutex_unlock(&FileID_list_lock);
		
		memcpy(get_list[get_count -1], FileID[i], 20);

		// print this response to the user
		print_get(meta, FileID[i], get_count);
	}	
	pthread_mutex_unlock(&lockListMutex);

	// flooding the query to the whole network
	event search_event;
	search_event.flooding = true;			// status is to be flooded
	search_event.port_num = Port;			// used for logging purpose
	search_event.header.msg_type = SHRQ;	// search request
	GetUOID(node_inst_id, "msg", search_event.header.UOID, 
					sizeof search_event.header.UOID);
	search_event.header.TTL = (int8_t)TTL;
	search_event.header.reserved = 0;		// always
	search_event.header.data_length = sizeof(int) + strlen(data.key);

	int len = search_event.header.data_length;
	search_event.buffer = SetBuffer(len);
	memcpy(&search_event.buffer[0], &data.search_type, 1);
	memcpy(&search_event.buffer[1], &data.key[0], len -1);

	// put in the dipatcher and save the UOID in the list
	// Add this UOID in the UOID_list
	pthread_mutex_lock(&UOID_list_lock);
	msg_entry new_entry;
	memcpy(new_entry.UOID, search_event.header.UOID, 20);
	new_entry.port_num = Port;	// Put my port
	Add_UOID(new_entry);
	pthread_mutex_unlock(&UOID_list_lock);

	// now put this event into the dispatcher
	pthread_mutex_lock(&dispatcher_lock);
	Append(dispatcher_first, search_event);
	pthread_cond_signal(&dispatcher_cond);
			// signal the dispatcher that something is there in the list
	pthread_mutex_unlock(&dispatcher_lock);

	return 1;
}


// get:
//		get <number> [<extfile>]
int parse_get(int argc, char **argv)
{
	int get_num;
	if(argc < 2 || argc > 3)	{
		printf("\tget: get <number> [<extfile>]\n");
		return -1;
	}
	if( (get_num = atoi(argv[1])) == 0)	{
		printf("\tget: get <number> [<extfile>]\n");
		return -1;
	}

	get_file = NULL;
	if(argc == 3)	{	// extfile is given
		get_file = SetBuffer(strlen(argv[2]));
		strcpy(get_file, argv[2]);
	}

	// first check that given number is a valid one
	if(get_num <= 0 || get_num > get_count)	{
		printf("\tget: please enter a valid number\n");
		return -1;
	}

	// find the FileID corresponding to the get_num
	get_data data;
	memcpy(data.FileID, get_list[get_num-1], 20);

	// check if this node has the required file
	pthread_mutex_lock(&FileID_list_lock);
	int file_num;
	if(Find_FileID(data.FileID))	{
		file_num = Get_FileID(data.FileID);	
		// put this file in the node's current directory
		// if the file is in cache, then bring it to the permanent space
		get_storefile(file_num, get_file);
		pthread_mutex_lock(&lockListMutex);
		bring_to_permanent(file_num);	// bring to permanent space from cache
		pthread_mutex_unlock(&lockListMutex);

		setLRUPriority_by_filenum(file_num);
		pthread_mutex_unlock(&FileID_list_lock);
		return -1;	// no need to wait for other 'get' replies
	}	
	pthread_mutex_unlock(&FileID_list_lock);

	// now put this event for flooding
	event get_event;
	get_event.flooding = true;			// status is to be flooded
	get_event.port_num = Port;			// used for logging purpose
	get_event.header.msg_type = GTRQ;	// get request
	GetUOID(node_inst_id, "msg", get_event.header.UOID, 
					sizeof get_event.header.UOID);
	get_event.header.TTL = (int8_t)TTL;
	get_event.header.reserved = 0;		// always
	get_event.header.data_length = 20;	// only the size of FileID

	int len = get_event.header.data_length;
	get_event.buffer = SetBuffer(len);
	memcpy(&get_event.buffer[0], data.FileID, 20);

	// put in the dipatcher and save the UOID in the list
	// Add this UOID in the UOID_list
	pthread_mutex_lock(&UOID_list_lock);
	msg_entry new_entry;
	memcpy(new_entry.UOID, get_event.header.UOID, 20);
	new_entry.port_num = Port;	// Put my port
	Add_UOID(new_entry);
	pthread_mutex_unlock(&UOID_list_lock);

	// now put this event into the dispatcher
	pthread_mutex_lock(&dispatcher_lock);
	Append(dispatcher_first, get_event);
	pthread_cond_signal(&dispatcher_cond);
			// signal the dispatcher that something is there in the list
	pthread_mutex_unlock(&dispatcher_lock);

	return 1;
}


// function get_storefile()
// this will put a file from the home directory to the
// current directory for the user in response of a get command
void get_storefile(int file_num, char *get_file)
{
	// first find the metadata of the given file
	char MetaFileName[MAXLINE], DataFileName[MAXLINE];
	sprintf(MetaFileName, "%s/%s/%d.meta", HomeDir,FilesDir, file_num);
	sprintf(DataFileName, "%s/%s/%d.data", HomeDir,FilesDir, file_num);

	// parse the metafile to take the file parameteres
	parsed_metadata metadata;
	metadata = parse_metafile(MetaFileName);

	char write_getfile[MAXLINE];
	get_file == NULL ? strcpy(write_getfile, metadata.FileName) :
							strcpy(write_getfile, get_file);
	FILE *fp;
	fp = fopen(write_getfile, "r");

	if(fp != NULL)	{
		printf("\tget: file %s exists, Replace [Y/N]: ",
												write_getfile);
		fflush(stdout);
		if(toupper(getchar()) != 'Y')	{
			printf("\tget: file not stored\n");
			fflush(stdout);
			fclose(fp);
			return;	// don't replace the existing file
		}
	}
	fclose(fp);

	// now, either the file is not there or the user wants to replace it
	if( (fp = fopen(write_getfile, "w")) == NULL)	{
		printf("\tget: unable to write the file at current directory\n");
		return;
	}
	
	FILE *fp_s, *fp_t;
	struct stat buf;
	int i;
	stat(DataFileName, &buf);
	fp_s = fopen(DataFileName, "r");
	fp_t = fopen(write_getfile, "w");
	for(i=0; i<buf.st_size; i++)
		fputc(fgetc(fp_s), fp_t);
	fclose(fp_s);
	fclose(fp_t);

	printf("\tget: file %s stored at current working directory.\n", write_getfile);
	fflush(stdout);
}
