// store two functions
//	1. Create the index files from the index lists
//	2. Create the index lists from the index files
#include "sv_node.h"

// dumpIndexData()
//	Dumps the index lists data to the corresponding index files
//	the 'dumper' thread runs periodically to call this functin
//	this ensures that the list data and index files are syncronized
//	periodically so that a sudden crash of the node doesn't prevent
//	it from regaining the previous status back.
int dumpIndexData(indexNode* nameFile,indexNode* sha1File, \
					indexNode* keywordsFile, char * homeDirectory)
{
	char temp[500];
	char * filepath = (char*)malloc(100);
	indexNode* current = NULL;
	int i;
	char * line = (char*)malloc(500);

	// Create the file path for the sha1 index file.
	memset(filepath,'\0',100);
	strcpy(filepath,sha1_index_file);

	// Open the file for the sha1 index.
	FILE * fp = fopen(filepath,"w");
	if(fp)	{
		// Loop through the linked list and form the line in the format  <sha1> <fileNumber> <cached>\n
		current = sha1File;
		while(current)	{
			if(!current->isDeleted)	{
				memset(line,'\0',500);
				memset(temp,'\0',500);
				for(i=0; i<20; i++)	{
					sprintf(temp, "%02x", current->sha1[i]);
					strcat(line, temp);
				}
				memset(temp,'\0',500);
				sprintf(temp," %d %d\n",current->fileNumber,(current->isCached== true) ? 1:0);
				strcat(line, temp);
				fputs(line, fp);
			}
			current = current->next;
		}
		fflush(fp);
		fclose(fp);
	}
	else	{
		return -1;
	}

	// Create the file path for the name index file.
	memset(filepath,'\0',100);
	//sprintf(filepath,"%s//%s",homeDirectory,name_index_file);
	strcpy(filepath,name_index_file);

	current = NULL;

	// Open the file for the name index.
	fp = fopen(filepath,"w");
	if(fp)	{
		// Loop through the linked list and form the line in the format 
		// <filename> <fileNumber> <nonce> <fileSize> <cache>\n
		current = nameFile;
		while(current)	{
			if(!current->isDeleted)	{			
				memset(line,'\0',500);
				memset(temp,'\0',500);
				sprintf(line,"%s %d ",current->filename,current->fileNumber);
				for(i=0; i<20; i++)	{
					sprintf(temp, "%02x", current->nonce[i]);
					strcat(line, temp);
				}
				memset(temp,'\0',500);
				sprintf(temp, " %d ", current->filesize);
				strcat(line, temp);
				memset(temp,'\0',500);
				sprintf(temp, "%d",current->lruPriority);
				strcat(line, temp);
				memset(temp,'\0',500);
				sprintf(temp, " %d\n", (current->isCached== true) ? 1:0);
				strcat(line, temp);
				fputs(line, fp);
				
			}
			current = current->next;
		}
		fflush(fp);
		fclose(fp);
	}
	else	{
		return -1;
	}

	// Create the file path for the keyword index file.
	memset(filepath,'\0',100);
	//sprintf(filepath,"%s//%s",homeDirectory,keyword_index_file);
	strcpy(filepath,keyword_index_file);

	current = NULL;

	// Open the file for the keyword index.
	fp = fopen(filepath,"w");

	if(fp)	{
		// Loop through the linked list and form the line in the format  <keyword> <fileNumber> <cache>\n
		current = keywordsFile;
		while(current)	{
			if(!current->isDeleted)	{
				memset(line,'\0',500);
				int i=0;
				for(i=0;i<256;i++)	{
					fprintf(fp,"%d",(current->bitVector)[i]);
				}
				sprintf(line," %d %d\n",current->fileNumber,(current->isCached== true) ? 1:0);
				fputs(line, fp);
			}
			current = current->next;
		}
		fflush(fp);
		fclose(fp);
	}
	else	{
		return -1;
	}

	free(line);
	free(filepath);
	return 1;
}


// createIndexData()
//	recreate the index lists from the saved index files
//	this function is called only once when a node stars again without
//		the -reset option
//	So in event of a shutdown or crash, a node can easily return to 
//		its previous status using this function
int createIndexData(indexNode** nameFile, indexNode** sha1File, \
					indexNode** keywordsFile, char * homeDirectory)
{
	char * filepath =(char*)malloc(100);
	char * line = (char*)malloc(500);
	char ch=NULL;
	int count =0;
	int charCount=0;
	int cacheFilled = 0;

	// Create the file path for the sha1 index file.
	memset(filepath,'\0',100);
	strcpy(filepath,sha1_index_file);

	// Open the file for the sha1 index.
	FILE * fp = fopen(filepath,"r");
	if(fp)	{
		// Loop through the file and for the linked list.
		// Each line in the file has format  <sha1> <fileNumber> <cached>\n
		memset(line,'\0',500);
		indexNode node;

		while((ch = fgetc( fp )) != EOF)	{
			if(ch == ' ')	{
			   // sha1
			   if(count == 0)	{
					XtoChar(line, node.sha1);
			   }

			   // fileNumber
			   if(count == 1)	
					node.fileNumber = (int) atol(line);
			   count++;
			   memset(line,'\0',500);
			   charCount = 0;
			}
			if(ch== '\r' || ch=='\n')
			{
			   // Cached
			   if(count == 2)
					node.isCached = (int)atoi(line);
			   memset(line,'\0',500);
			   node.bitVector = NULL;
			   Add_index(sha1File,node);

			   // Clear all the fields
			  // Clear all the fields
			   memset(node.sha1,'\0',20);
			   node.fileNumber =-1;
			   node.filename = NULL;
			   memset(node.nonce,'\0',20);
			   count = 0;
			   charCount = 0;
			}
			if(ch != '\r' && ch != ' ' && ch != '\n')
					line[charCount++] = ch;
		}
		fflush(fp);
		fclose(fp);
	}
	else
		return -1;

	// Create the file path for the name index file.
	memset(filepath,'\0',100);
	strcpy(filepath, name_index_file);
	count =0;
	charCount=0;

	// Open the file for the name index.
	fp = fopen(filepath,"r");
	if(fp)	{
		// Loop through the file and for the linked list.
		// Each line in the file has format  <filename> <fileNumber> <nonce> <cached>\n
		memset(line,'\0',500);
		indexNode node;
		while((ch = fgetc( fp )) != EOF)	{
			if(ch == ' ')	{
			   // fileName
			   if(count == 0)	{
					node.filename = (char *)malloc(strlen(line));
					memcpy(node.filename,line,strlen(line));
			   }

			   // fileNumber
			   if(count == 1)
					node.fileNumber = (int) atol(line);
			   
			   // Nonce
			   if(count == 2)	{
					XtoChar(line, node.nonce);
			   }

			    // Filesize
			   if(count == 3)	{
					node.filesize = (long) atol(line);
			   }

			    // Priority
			   if(count == 4)	{
					node.lruPriority = (long) atol(line);
			   }
			   count++;
			   charCount = 0;
			   memset(line,'\0',500);
			}
			
			if(ch== '\r' || ch=='\n')	{
			  // Nonce
			   if(count == 5)
					node.isCached = (int)atoi(line);
			   memset(line,'\0',500);
			   node.bitVector = NULL;
			   Add_index(nameFile,node);
				if(node.isCached)
				{
					cacheFilled  += node.filesize;
				}
			    // Clear all the fields
			   // Clear all the fields
			   memset(node.sha1,'\0',20);
			   node.fileNumber =-1;
			   node.filename = NULL;
			   memset(node.nonce,'\0',20);
			   count = 0;
			   charCount = 0;
			}
			if(ch != '\r' && ch != ' ' && ch != '\n')
					line[charCount++] = ch;
		}
		fflush(fp);
		fclose(fp);
	}
	else
		return -1;

	// Create the file path for the keyword index file.
	memset(filepath,'\0',100);
	strcpy(filepath,keyword_index_file);
	count =0;
	charCount=0;

	// Open the file for the keyword index.
	fp = fopen(filepath,"r");
	if(fp)	{
		// Loop through the file and for the linked list.
		// Each line in the file has format  <bitVector> <fileNumber> <isCached>\n
		memset(line,'\0',500);
		indexNode node;
		while((ch = fgetc( fp )) != EOF)	{
			if(ch == ' ')	{
			   // bitVector
			   if(count == 0)	{
					node.bitVector = (int *)malloc(256*sizeof(int));
					int i=0;
					for(i=0;i<256;i++)
						(node.bitVector)[i] = (int)(line[i] - '0');
			   }

			   // fileNumber
			   if(count == 1)
					node.fileNumber = (int) atol(line);

			   count++;
			   charCount = 0;
			   memset(line,'\0',500);
			}

			if(ch== '\n')	{
			   // isCached
			   if(count == 2)
					node.isCached = (int) atol(line);

			   memset(line,'\0',500);

			   Add_index(keywordsFile,node);

			   // Clear all the fields
			   memset(node.sha1,'\0',20);
			   node.fileNumber =-1;
			   node.filename = NULL;
			   memset(node.nonce,'\0',20);
			   count = 0;
			   charCount = 0;
			}
			if(ch != '\r' && ch != ' ' && ch != '\n')
			   line[charCount++] = ch;			
		}

		fflush(fp);
		fclose(fp);
	}
	else
		return -1;
	 cacheFreeSize = (CacheSize * 1024) - cacheFilled;
	return 1;
}
