// this file includes some miscellaneous utilities requried by the program
//	only those funcitons which are not separated logically are included here
//	these functions are requried all over the program and can't be specified
//			as any perticular type of functions
//	so they are included here at one place
#include "utilities.h"
#include <openssl/sha.h>
#include "iniparser.h"

// GetUOID()
//	this function generates a unique UOID
unsigned char *GetUOID(
	  char *node_inst_id,
	  char *obj_type,
	  unsigned char *uoid_buf,
	  unsigned int uoid_buf_sz)
{
  static unsigned long seq_no=(unsigned long)1;
  char sha1_buf[SHA_DIGEST_LENGTH], str_buf[104];

  snprintf(str_buf, sizeof(str_buf), "%s_%s_%1ld",
		  node_inst_id, obj_type, (long)seq_no++);
  //printf("str: %s\n", str_buf);
  SHA1((const unsigned char *)str_buf, strlen(str_buf), (unsigned char *)sha1_buf);
  memset(uoid_buf, 0, uoid_buf_sz);
  memcpy(uoid_buf, sha1_buf,
		  MIN(uoid_buf_sz,sizeof(sha1_buf)));
  return uoid_buf;
}

// GetNodeInstID()
//	this function generates the 'node instance ID' whenver the node starts
//	this is called excatly once when the node starts (see sv_node.cc)
char *GetNodeInstID(
		char *hostname, int portnum, time_t start_time)
{
	char *buf;

	// both portnum and time_t are 32 bit integers.
	// they can have a maximum length of 10 (4 billion in string)
	buf = SetBuffer(sizeof(hostname) + 10 + 10);
	strcpy(buf, hostname);
	strcat(buf, "_");
	char temp[10];
	sprintf(temp, "%d", portnum);

	strcat(buf, temp);
	strcat(buf, "_");
	sprintf(temp, "%d", start_time);
	strcat(buf, temp);

	return buf;
}

// get a temporary file name
//	at many places, the program needs to write something in a temporary file
//	this function returns a unique file name placed in the HomeDir/tmp directory
//	the temp_file should be deleted immediately after its usage
//	else, all the files in HomeDir/tmp are deleted when the node shuts down
//	in case of a crash, the files are also deleted when the node starts

// NOTE: the mutex tempfile_mutex is used solely by this function
//		and not available to the user
//	So the user doesn't need to take any lock before calling this funtion
//	the function guarantees a unique temporary filename
//	The maximum number of tempfiles can be 10000 only. 
//			(can be changed inside this fucntion)
int tempfile_count;
pthread_mutex_t tempfile_mutex;
char *get_tempfile_name()
{
	pthread_mutex_lock(&tempfile_mutex);
	char *temp;
	temp = SetBuffer(strlen(HomeDir) + 4 + 9 + 1);
			// "Homedir/tmp/temp0001\0"
	sprintf(temp, "%s/tmp/temp%04d\0", HomeDir, tempfile_count++);
	pthread_mutex_unlock(&tempfile_mutex);

	return temp;
}

// parse a Metadata file and return the values
// any matafile can be given to this function and it will fill
//		the structure parsed_metadata with the parsed values
//		(please see utilities.h for the structure)
parsed_metadata parse_metafile(char *metafile)
{
	// load the iniparser
	int i;
	dictionary *ini;
	char *tempStr;
	ini = iniparser_load(metafile);

	// get all the values and build the structure
	parsed_metadata data;
	
	tempStr = iniparser_getstring(ini, "metadata:FileName", NULL);
	data.FileName = (char *)malloc(strlen(tempStr));
	strcpy(data.FileName, tempStr);

	tempStr = iniparser_getstring(ini, "metadata:FileSize", NULL);
	data.FileSize = atol(tempStr);

	tempStr = iniparser_getstring(ini, "metadata:SHA1", NULL);
	XtoChar(tempStr, data.SHA1);

	tempStr = iniparser_getstring(ini, "metadata:Nonce", NULL);
	XtoChar(tempStr, data.Nonce);

	tempStr = iniparser_getstring(ini, "metadata:Bit-vector", NULL);
	char templine[1];
	for(i=0; i<256; i++)	{// 256 is the length of Bit vector
		sprintf(templine, "%c", tempStr[i]);
		data.BitVector[i] = atoi(templine);
	}

	// keywords
	//	it will be a string with each character separated by a space
	//	use 'strtok' to extract the keywords from it
	data.KeywordCount =0;
	data.Keywords = (char **)malloc((i+1)*sizeof(char *));

	tempStr = iniparser_getstring(ini, "metadata:Keywords", NULL);
	char *result = NULL;
	result = strtok(tempStr, " ");
	i= 0;
	while(result != NULL)	{
		data.Keywords = (char **)realloc(data.Keywords, (i+1)*sizeof(char *));
		data.Keywords[i] = (char *)malloc(strlen(result));
		strcpy(data.Keywords[i], result);
		data.KeywordCount++;		// one more keyword, Aha!!!
		
		result = strtok(NULL, " ");
		i++;
	}

	// unload the ini file
	iniparser_freedict(ini);

	// finally return the parsed metadata structure
	return data;
}

// parse a signed file and take the data part of it.
// used only for parsing a DELETE message
// parses the file name, SHA1 and Nonce from the buffer
// of a signed message
// first, put the buffer into a temp file and then give
// this file as the 'signed_file' parameter.
// the temp file may be deleted after calling this function
int parse_signed(char *signed_file, char *filename, \
					unsigned char *SHA1,	\
					unsigned char *Nonce)
{
	FILE *fp;
	if( (fp = fopen(signed_file, "r")) == NULL)
		return -1;

	char line[MAXLINE];	
	do{
		fgets(line, MAXLINE, fp);
	}while(line[0]!= '-' && line[1]!= '-');

	int i;
	for(i=0; i<3; i++)	{
		fgets(line, MAXLINE, fp);
		line[strlen(line)-2] = '\0'; //remove \r\n from end

		char *result = NULL;
		result = strtok(line, "=");
		if(result == NULL)	return -1;
		result = strtok(NULL, "=");
		if(result == NULL)	return -1;
		
		if(i== 0)	// first time, so take filename
			strcpy(filename, result);

		else if(i== 1)	// second time, so take SHA1
			XtoChar(result, SHA1);

		else	// third time, so take Nonce
			XtoChar(result, Nonce);
	}
	fclose(fp);
	return 0;
}

// converts a hex string of 40 bytes (sha1 or nonce)
// into corresponding array
// this function is very useful in the following situations:
//	-> the SHA1 is read from the user on stdout
//	-> the SHA1 is read from a ASCII file
// As the file or the user gives the SHA1 as a hex-encoded string
//		of 40 bytes, we need to convert it to actual SHA1 of 20 bytes
// this function do this for the user
// input: A string of 40 bytes (hex-encoded SHA1) and empty array of
//				of unsigned char type of 20 bytes long
// output: the given array is filled with the actual SHA1
void XtoChar(char *tempStr, unsigned char *id)
{
	uint8_t out[20], out1 =0, out2=0;
	int i,j;
	for(i=0; i<2*20; i+=2)	{
		if(tempStr[i] >= 'a' && tempStr[i] <='f')
			out1 = 10+ (int)(tempStr[i] - 'a');
		else
			out1 = (int)(tempStr[i] - '0');

		if(tempStr[i+1] >= 'a' && tempStr[i+1] <='f')
			out2 = 10+ (int)(tempStr[i+1] - 'a');
		else
			out2 = (int)(tempStr[i+1] - '0');
		
		j = i/2;
		out[j] = 16*out1 + out2;
	}
	
	for(i=0; i<20; i++)
		memcpy(&id[i], &out[i], 1);
}

// adds a file to the index lists
//	needed to be called everytime a file is added to the filesystem
//	the file is marked in the index_files
//	the following details are needed:
//	-> the file is in cache or permanent space
//	-> complete metadata of file, in the parsed_metadata structure
//			(see utilities.h and the function above)
//	-> the number of the file (got using getFileNumber)
int add_lists(bool isCache, parsed_metadata metadata, int fileNum)
{
	int i;
	// Make an entry in name_index list here..
	indexNode file_node;
	file_node.bitVector = NULL;
	file_node.fileNumber = fileNum;
	memset(file_node.sha1, '\0', 20);
	memcpy(file_node.nonce, metadata.Nonce, 20);

	file_node.filename = SetBuffer(strlen(metadata.FileName));
	strcpy(file_node.filename, metadata.FileName);
	file_node.filesize = metadata.FileSize;
	file_node.isDeleted = false;
	file_node.isCached = isCache;

	Add_index(&fnameList, file_node);	// add it to the list

	// Make an entry in sha1_index list here..
	indexNode sha1_node;
	sha1_node.bitVector = NULL;
	sha1_node.fileNumber = fileNum;
	memcpy(sha1_node.sha1, metadata.SHA1, 20);
	memcpy(sha1_node.nonce, metadata.Nonce, 20);

	sha1_node.filename = NULL;
	sha1_node.filesize = metadata.FileSize;
	sha1_node.isDeleted = false;
	sha1_node.isCached = isCache;

	Add_index(&sha1List, sha1_node);	// add it to the list

	// Make an entry in the keyword_index list here..
	// put an entry for each keyword
	indexNode kword_node;
	kword_node.bitVector = (int *)malloc(256* sizeof(int));
	for(i=0; i<metadata.KeywordCount; i++)	{
		memset(kword_node.bitVector, 0, 256*sizeof(int));
		if(generateBitVector(kword_node.bitVector, metadata.Keywords[i]) == -1)	{
			return -1;
		}

		kword_node.fileNumber = fileNum;
		memset(file_node.sha1, '\0', 20);
		memcpy(kword_node.nonce, metadata.Nonce, 20);

		kword_node.filename = NULL;
		kword_node.filesize = metadata.FileSize;
		kword_node.isDeleted = false;
		kword_node.isCached = isCache;

		Add_index(&kwordList, kword_node);	// add it to the list
	}

	// decrease cache size if the file is stored in cache
	if(isCache)
		cacheFreeSize -= metadata.FileSize;
	// lists updated

	return 0;
}

// this function returns a boolean value if the outcome of flipping
// a coin with given probability (0-1) is positive or not
bool coin_outcome(double prob)
{
	if(prob<0 || prob>1)
		return false;

	double dval = (double)drand48();
	if(dval <= prob)
		return true;
	else
		return false;
}

// this function prints the response of a search result
// on stdout in a proper format
void print_get(parsed_metadata meta, unsigned char *FileID, int num)
{
	int i;
	
	// first, fileID
	printf("\n");
	printf("\t[%d] FileID=", num);
	for(i=0; i<20; i++)
		printf("%02x", FileID[i]);
	printf("\n");

	// now, FileName and FileSize
	printf("\t    FileName=%s\n", meta.FileName);
	printf("\t    FileSize=%ld\n", meta.FileSize);

	// Sha1
	printf("\t    SHA1=");
	for(i=0; i<20; i++)
		printf("%02x", meta.SHA1[i]);
	printf("\n");

	// Nonce
	printf("\t    Nonce=");
	for(i=0; i<20; i++)
		printf("%02x", meta.Nonce[i]);
	printf("\n");

	// Keywords
	printf("\t    Keywords=");
	for(i=0; i<meta.KeywordCount; i++)
		printf("%s ", meta.Keywords[i]);

	// and, we are done, just flush the stdout
	fflush(stdout);
	return;
}

//	A function to allocate memory to a char pointer to use it as string
//	Takes the number of bytes as an argument
//	Return the allocated buffer and clears the memory using memset.
char *SetBuffer(int size)
{
	char *temp = (char *)malloc(size);
	if(temp == NULL)
		fprintf(stderr, "malloc() failed\n");
	memset(temp, 0, size);
	return temp;
}

// Clears all the contents of the directory.
void removeDirectory(char * path)
{
    char * command;
    command = SetBuffer(MAXLINE);
    sprintf(command,"rm -Rf \"%s\"",path);
    system(command);
	free(command);
}

// Makes a new directory at he filepath specified. 
// If a directory already exits all the files in that directory are deleted.
int makeDirectory(char * filepath)
{
	if(filepath)
	{
		struct stat st;
		bool folderExists = (stat(filepath, &st) == 0);
		if(!folderExists)
		{
			int retVal = mkdir(filepath,S_IRWXU|S_IRWXG|S_IRWXO);
			if(retVal > 0)
				return -1;
		}
	}
	return 1;
}


// this function returns all the matched files of a search query
// this is used only for searchtype 1 or 2 (means filename or sha1)
// for keywords, use the function give below
int getFileNumberList(int searchType, char * searchValue, int ** fileNumber)
{
	int count=0;
	indexNode *current = NULL;
	if(searchType == 1)
		current = fnameList;
	else if(searchType == 2)
		current = sha1List;
	else
		return -1;
			
	(*fileNumber) = (int *)malloc(1 * sizeof(int));
	switch(searchType)
	{
		case 1: 
			while(current)	
			{
				if(current->isDeleted == false && strcmp(current->filename,searchValue)==0 )
				{
					count++;	
					(*fileNumber) = (int *) realloc((*fileNumber),count * sizeof(int));
					(*fileNumber)[count - 1] = current->fileNumber;
				}
				current = current->next;
			}			
			break;

		case 2: 
			unsigned char sha1[20];
			memcpy(sha1,searchValue,20);
			while(current)	
			{
				if(current->isDeleted == false && memcmp(current->sha1,sha1,20)==0 )
				{
					count++;	
					(*fileNumber) = (int *)realloc((*fileNumber),count * sizeof(int));
					(*fileNumber)[count - 1] = current->fileNumber;
				}
				current = current->next;
			}	
			break;

	}
	return count;
}

// this function returns all the matched file for the given keywords
int searchKeywords(int ** fileNumbers,char * keywords)
{
	// found: Used to check if the keyword is foundin file if the bitVector matches.
	bool found = 1; 

	// bitVector: contains bitVector generated by all the keywords keyed in for search.
	// fileNumCount: counts the number of files that match the search creiteria.
	// wordCount: counts the number of words in the keywords provided for search.
	// count: Contains the number of  files present in the Home Folder/files directory.
	int bitVector[256]={0},fileNumCount=0,wordCount=0,count=0,j=0,i=0,k=0,l=0;
	unsigned int m=0;

	// Corresponds to each  word separate from the list of keywords.
	char * word = (char *) malloc(80); 

	// Contains the list of all the words in the keywords provided for search.
	char ** wordList = NULL;
	wordList = (char **)malloc(1 * sizeof(char *));

	parsed_metadata metaData;

	// Contains the fileNumber pointer provided to the GetActiveFileNumbers function to get the list of files present in the 
	// Home directory.
	int *fileNumber=NULL;

	fileNumber = (int *)malloc(1 * sizeof(int));
	(*fileNumbers) = (int *)malloc(1 * sizeof(int));
	// STEP: 1. Spearate the keywords and generate the bitVector for all the keywords.
	
	if(word)
	{
		memset(word,'\0',80);	
		for(i=0;i<(int)strlen(keywords);i++)
		{
			if(keywords[i] == ' ')
			{
				wordCount++;
				wordList = (char **)realloc(wordList,wordCount * sizeof(char *));
				wordList[wordCount-1] = (char *) malloc(strlen(word));
				strcpy(wordList[wordCount-1],word);

				// Generates the biVector for the keywords
				generateBitVector(bitVector,word);
				memset(word,'\0',80);	
				j=0;					
			}
			else
			{
				word[j++]=tolower(keywords[i]);
			}
		}
		wordCount++;				
		wordList = (char **)realloc(wordList,wordCount * sizeof(char *));
		wordList[wordCount-1] = (char *) malloc(strlen(word));
		strcpy(wordList[wordCount-1],word);
		generateBitVector(bitVector,word);			
	}
	
	// 2. Get the list of the filenumbers
	count = getActiveFileNumbers(fnameList, &fileNumber);

	// 3. For each of the fileNumbers do the following:
	//	a. Call the metaparser and get the metaData corresponding to the .meta file.
	//	b. Loop 256 times and compare the keywords bitvector with file bitVector.
	//	c. If there is mis-match then continue.
	//	d. If there is match then compare the keywords with the actual keywords in file.
	//	e. If all the keyword match with the ones in file then add th filenumber to the list.

	if(count > 0)
	{
		memset(word,'\0',80);
		for(i=0;i<count;i++)
		{
			// Generate the <filenumber>.meta filepath.
			sprintf(word,"%s/%s/%d.meta",HomeDir,FilesDir,fileNumber[i]);

			// Get the parsed_metadata from the .meta file.
			metaData = parse_metafile(word);

			// Compare the bitVectors.	
			for(j=0;j<256;j++)
			{
				if(bitVector[j] == 0)
					continue;
				else
					if(bitVector[j] != metaData.BitVector[j])
					{
						break;
					}
			}

			// Check if all the values in the bitVector matched.
			if(j == 256)
			{			
				// Check if the all the keywords are present in file.
				for(k=0;k < wordCount;k++)
				{
				   found = 0;
 				   for(l=0;l<metaData.KeywordCount;l++)
				   {
					if(strlen(wordList[k]) == strlen(metaData.Keywords[l]))
					{
						for(m=0;m<strlen(metaData.Keywords[l]);m++)
						{
							if(tolower(wordList[k][m]) != tolower(metaData.Keywords[l][m]))
								break;
						}
						if(m == strlen(wordList[k]))
						{
							found = 1 ;				
							break;
						}
					}
				   }
				   if(!found)
					break;					
				}

				if(k==wordCount)
				{
					fileNumCount++;	
					(*fileNumbers) = (int *)realloc((*fileNumbers),fileNumCount * sizeof(int));
					(*fileNumbers)[fileNumCount - 1] = fileNumber[i];				
				}
			}
		}
	}

	return fileNumCount;
}
