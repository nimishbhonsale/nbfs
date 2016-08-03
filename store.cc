// this file is used for the 'store' command
//	1. first, the command is performed for this node for itself.
//	2. then the command is flooded properly to the whole network (i.e. an event is
//			is put in the 'dispatcher' for flooding
//	3. the UOID of the flooded message is saved in the UOID list

// also, the following is done for storing a file
//	-> a unique file number is taken from the getFileNumber() function.
//	-> Meta, Data and Pem files are created for the file being stored
#include "sv_node.h"
#include <openssl/sha.h>

long int fileNumber;
int parse_store(int argc, char **argv);

// getFileNumber()
//	there is a counter which gets incremented each time a new file is stored
//	this function is called to get the next available number
//	even if a numbered file is deleted, the counter doesn't go back.
int getFileNumber()
{
	pthread_mutex_lock(&fileNumMutex);
	fileNumber++;
	if(fileNumber <= 0)
	{
		return -1;
	}
	
	pthread_mutex_unlock(&fileNumMutex);
	return fileNumber;
}

// createMetaFile()
//	this function creates a metafile based on the following details:
//	-> original file name (specified by the user)
//	-> keywords (specified by the user)
//	-> a unique filename x.meta (x is a number got using getFileNumber())
//  it also calculates the SHA1 of this file and a unique NonceID of the file
int createMetaFile(char * filename,char* newFilename,char* keywords)
{
	FILE *fp_meta = NULL;
	int i=0;
	char ch;
	struct stat buf;
	fp_meta = fopen(newFilename,"w");

	if(fp_meta)
	{
		// [metadata]
		char * line = (char *) malloc(600);
		memset(line,'\0',600);	
		memcpy(line,"[metadata]\r\n",12);
		fputs(line, fp_meta);

		// FileName= 
		memset(line,'\0',600);	
		sprintf(line,"FileName=%s\r\n",filename);
		fputs(line, fp_meta);

		// FileSize=
		FILE *fp_orig = fopen(filename,"rb");
		memset(line,'\0',600);	
		
		if(fp_orig)
		{
			// Filesize
			stat(filename,&buf);
			sprintf(line,"FileSize=%ld\r\n",buf.st_size);		
			fputs(line, fp_meta);
			memset(line,'\0',600);	
			
			// SHA1=
			SHA_CTX c;
			unsigned char key[SHA_DIGEST_LENGTH];
			SHA1_Init(&c);		
			for(i=0;i<buf.st_size;i++)
			{
				ch=fgetc(fp_orig);
				SHA1_Update(&c,&ch,1);			
			}
			SHA1_Final(key,&c);
			fputs("SHA1=", fp_meta);
			for (i = 0; i < SHA_DIGEST_LENGTH; i++) 
			{	
				//printf("%02x",key[i]);
				fprintf(fp_meta,"%02x",key[i]);
			}
			fputs("\r\n",fp_meta);		
		}
		else
		{
			memcpy(line,"FileSize=\r\n",11);
			fputs(line, fp_meta);
			memset(line,'\0',600);	
			memcpy(line,"SHA1=\r\n",7);
		}
	
		// Nonce=
		memset(line,'\0',600);	
		memcpy(line,"Nonce=",6);
		fputs(line, fp_meta);
		unsigned char NonceID[20];
		GetUOID(node_inst_id, "nonce", NonceID, 
						sizeof NonceID);
		int i;
		for(i=0; i<20; i++)
			fprintf(fp_meta, "%02x", NonceID[i]);
		//fputs(templine, fp_meta);
		fputs("\r\n", fp_meta);

		// Keywords=
		memset(line,'\0',600);	
		if(strlen(keywords) >0)
		{
			sprintf(line,"Keywords=%s\r\n",keywords);
		}
		else
		{
			memcpy(line,"Keywords=\r\n",11);
		}
		fputs(line, fp_meta);
		
		// Bit-vector
		int bitVector[256]={0},j=0;
		char * word = (char *) malloc(80);
		if(word)
		{
			memset(word,'\0',80);	
			for(i=0;i<(int)strlen(keywords);i++)
			{
				if(keywords[i] == ' ')
				{
					generateBitVector(bitVector,word);
					memset(word,'\0',80);	
					j=0;					
				}
				else
				{
					word[j++]=tolower(keywords[i]);
				}
			}
			generateBitVector(bitVector,word);			
		}
		memset(line,'\0',600);	
		memcpy(line,"Bit-vector=",12);
		fputs(line, fp_meta);		
		for(i=0;i<256;i++)
		{			
			fprintf(fp_meta,"%d",bitVector[i]);
		}
		fputs("\r\n",fp_meta);
		fclose(fp_orig);
		fflush(fp_meta);
		fclose(fp_meta);
	}
	else
	{
		printf("Cannot open the file\n");
		return -1;
	}
	return 1;

}

// createDataFile()
//	this functions just copy the user specified file into x.data file
//	here 'x' is the number got using getFileNumber()
int createDataFile(char * filename,char * dataFilename)
{
	// copy the file line by line
	FILE *fp_s, *fp_t;
	struct stat stat_buf;
	stat(filename, &stat_buf);

	// if the user specifed file doesn't exist, go back;
	if( (fp_s = fopen(filename, "r")) == NULL)	{
		printf("\tstore: error reading file %s\n", filename);
		return -1;
	}

	if( (fp_t = fopen(dataFilename, "w")) == NULL)	{
		printf("\tstore: unexpected error, file not stored\n");
		return -1;
	}

	for(int i=0; i<stat_buf.st_size; i++)
		fputc(fgetc(fp_s), fp_t);

	fclose(fp_s);
	fclose(fp_t);
	return 1;
}

// this function do the actual part of storing the file and flooding the request
//	1. first, the command is performed for this node for itself.
//	2. then the command is flooded properly to the whole network (i.e. an event is
//			is put in the 'dispatcher' for flooding
//	3. the UOID of the flooded message is saved in the UOID list
int parse_store(int argc, char** argv)
{
	int i;
	//createMetaFile("cate","test.txt","a b c");
	//return 1;

	// Commandline argument parsing goes here..
	if(argc < 3)	{
		printf("\tFormat: store filename <ttl> [key=\"value\" ...]\n");
		return -1;
	}
	// Retrieve the TTL value here..
	int ttl=-1;
	if((ttl = atoi(argv[2])) < 0)	{
		printf("\tERROR: TTL should be 0 or a positive integer\n");
		return -1;
	}
	// Retrieve the filename here.. 
	char *filename;
	filename = (char *)malloc(strlen(argv[1]));
	strcpy(filename, argv[1]);

	char *keyword = (char*)(malloc(600));
	memset(keyword,'\0',600);
	int count=0,foundQuot=0,j=0,retval = -1,foundEqual=0,valueFound=0;

	// Logic for the keyword parsing goes here..
	for(i=3;i<=argc-1;i++)	
	{			
		if(i>3)
		{
			keyword[count++] = ' ' ;			
		}
		if(!foundQuot)
			valueFound =0;
		for(j=0;j<(int)strlen(argv[i]);j++)
		{
			if(	!foundQuot && j >0 && argv[i][j-1] == '=' && argv[i][j] == '=')
				{
					printf("\tFormat: store filename <ttl> [key=\"value\" ...]\n");
					return -1;	
				}
			if(foundEqual)
				foundEqual = 0;			
			
			if(argv[i][j] == '=' && !foundQuot)	
			{
				if(!foundEqual)
				{
					foundEqual=1;
					valueFound = 1;
				}
			
				keyword[count++] = ' ';
				continue;
			}
			if(argv[i][j] == '"')
			{
				foundQuot = !foundQuot;
				continue;
			}
			keyword[count++] = argv[i][j];
		}
	}
	if(foundQuot || foundEqual || !valueFound)
	{
		printf("\tFormat: store filename <ttl> [key=\"value\" ...]\n");
		return -1;
	}	
	
	// Initialize the .meta and .data filenames.
	char * metaFilename = SetBuffer(strlen(HomeDir)+7+15);
	char * dataFilename = SetBuffer(strlen(HomeDir)+7+15);
	char * certFilename = SetBuffer(strlen(HomeDir)+7+15);
	memset(metaFilename,'\0',strlen(HomeDir)+7+15);
	memset(dataFilename,'\0',strlen(HomeDir)+7+15);
	memset(certFilename,'\0',strlen(HomeDir)+7+15);

	// Get the file number.
	int fileNum = getFileNumber();

	// Check if the number returned is a valid number;
	if(fileNum > 0)
	{
		// Check if the private key file exists. If not then create one.
		FILE * fp = fopen(privateKeyFile,"r");
		if(!fp)
			return -1;
		else
			fclose(fp);

		//  Check if the certificate file exists. If not then create one.
		fp = fopen(certificateFile,"r");
		if(!fp)
			return -1;
		else
			fclose(fp);
		

		// Set the filenames.
		sprintf(metaFilename,"%s/%s/%d.meta",HomeDir,FilesDir, fileNum);
		sprintf(dataFilename,"%s/%s/%d.data",HomeDir,FilesDir, fileNum);
		sprintf(certFilename,"%s/%s/%d.pem",HomeDir,FilesDir, fileNum);

		// Create and populate the .meta file here..	
		pthread_mutex_lock(&lockListMutex);
		retval = createMetaFile(filename,metaFilename,keyword);
		if(retval == -1)
			return -1;

		// Make the .data file here..
		retval = createDataFile(filename,dataFilename);
		if(retval == -1)
			return -1;
		
		// Make the .pem file here...
		retval = createDataFile(certificateFile, certFilename);
		// add this file in the index lists
		parsed_metadata metadata;
		metadata = parse_metafile(metaFilename);
		add_lists(false, metadata, fileNum);	// goes into permaent storage

		// Get the neighbor store probability and decide the neighbors 
		//		that you want to flood the message to here..
		
		// now put an event in the dispatcher for flooding
		store_data data;
		struct stat stat_buf;
		
		// first, metadata
		stat(metaFilename, &stat_buf);
		data.metadata_len = stat_buf.st_size;
		data.metadata = SetBuffer(data.metadata_len);
		memset(data.metadata, '\0', data.metadata_len);

		fp = fopen(metaFilename, "r");
		for(i=0; i<data.metadata_len; i++)
			data.metadata[i] = fgetc(fp);
		fclose(fp);
		
		// append public key certificate
		stat(certificateFile, &stat_buf);
		data.cert_len = stat_buf.st_size;
		data.certificate = SetBuffer(data.cert_len);
		memset(data.certificate, '\0', data.cert_len);

		fp = fopen(certificateFile, "r");
		for(i=0; i<data.cert_len; i++)
			data.certificate[i] = fgetc(fp);
		fclose(fp);

		// file data
		char *tempfile_name = get_tempfile_name();
		FILE *fp_t;
		if( (fp_t = fopen(tempfile_name, "w")) == NULL)	{
			printf("\tstore: error flooding store messge\n");
			pthread_mutex_unlock(&lockListMutex);
			return -1;
		}

		int next = 0;
		fwrite(&data.metadata_len, sizeof(int), 1, fp_t);
		next+= sizeof(int);
		fwrite(data.metadata, data.metadata_len, 1, fp_t);
		next+= data.metadata_len;
		fwrite(&data.cert_len, sizeof(int), 1, fp_t);
		next+= sizeof(int);
		fwrite(data.certificate, data.cert_len, 1, fp_t);
		next+= data.cert_len;

		// finally, put the data file
		// for store file, don't make a buffer of filesize
		// instead, just dump the whole buffer in a temporary file
		// then, give this file's name to the dispatcher by putting it in
		// the buffer. So the dispatcher will read this file and will forward
		// it in pieces of 8192 bytes.
		stat(dataFilename, &stat_buf);
		fp = fopen(dataFilename, "r");
		for(i=0; i<stat_buf.st_size; i++)
			fputc(fgetc(fp), fp_t);
		fclose(fp);
		pthread_mutex_unlock(&lockListMutex);
		next+= stat_buf.st_size;

		fclose(fp_t);
		//data.file_data = SetBuffer(stat_buf.st_size);
		//memset(data.file_data, '\0', stat_buf.st_size);
		
		// now put this event into dispatcher for flooding
		event fwd_event;
		fwd_event.flooding = true;
		fwd_event.port_num = Port;	// used only for loggin purpose
		fwd_event.header.msg_type = STOR;
		GetUOID(node_inst_id, "msg", fwd_event.header.UOID, 
				sizeof fwd_event.header.UOID);
		fwd_event.header.TTL = ttl;	// given TTL
		fwd_event.header.reserved = 0;		// as we know
		fwd_event.header.data_length = next;

		fwd_event.buffer = SetBuffer(strlen(tempfile_name));
		strcpy(fwd_event.buffer, tempfile_name);

		// Add this UOID in the UOID_list
		pthread_mutex_lock(&UOID_list_lock);
		msg_entry new_entry;
		memcpy(new_entry.UOID, fwd_event.header.UOID, 20);
		new_entry.port_num = Port;	// Put my port
		Add_UOID(new_entry);
		pthread_mutex_unlock(&UOID_list_lock);

		// now put this event into the dispatcher
		pthread_mutex_lock(&dispatcher_lock);
		Append(dispatcher_first, fwd_event);
		pthread_cond_signal(&dispatcher_cond);
				// signal the dispatcher that something is there in the list
		pthread_mutex_unlock(&dispatcher_lock);
		//	return -1;		
	}
	return 0;
}
