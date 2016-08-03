// stores structures, mutexes and functions for index files and lists
// The index files are used to map the index lists
// This is useful to bring a node up with the same previous status
// So the index lists data is saved periodically in the corresponding
//		index files by the 'dumper' thread (see dump.cc & dispatcher.cc)
// when a node starts up without the -reset option, the files are read
// propery and the data is saved in the lists back.
#include "index_list.h"

pthread_mutex_t lockListMutex;

// Add a new node to the end of the list each time.
// Remember to populate all the fields of data for each of the lists.
int Add_index( indexNode** head,  indexNode data)
{ 
	indexNode * current = *head;
	indexNode *node = ( indexNode *)malloc(sizeof(indexNode));
	if(node == NULL)
	{
		fprintf(stderr, "malloc() failed... out of memory\n");
		return -1;
	}

	if(data.bitVector)
	{
		node->bitVector = (int *) malloc(256 * sizeof(int));
		int i=0;
		for(i=0;i<256;i++)
		{
			//printf("%d", data.bitVector[i]);
			(node->bitVector)[i] =  data.bitVector[i];
		}
	}

	if(data.sha1)
	{
		memcpy(node->sha1,data.sha1,20);
	}
	
	if(data.nonce)
	{
		memcpy(node->nonce,data.nonce,20);
	}

	if(data.filename)
	{
		node->filename = (char *) malloc(strlen(data.filename) +1);
		memset(node->filename, '\0', strlen(data.filename) +1);
		memcpy(node->filename,data.filename,strlen(data.filename));
	}
	node->fileNumber = data.fileNumber;
	node->isDeleted = 0;
	node->isCached = data.isCached;
	node->filesize=data.filesize;
	node->lruPriority=data.lruPriority;
	node->next = NULL;
	if(current == NULL)
	{
		*head = node;
	}
	else
	{	
		while(current->next != NULL)
		{
			current = current->next;
		}
		current->next = node;
	}
	return 1;
}

// dumps the specified index list on the stdout
// useful for debugging purpose
void PrintList_index(indexNode *head)
{
	indexNode * current = head;
	int count = 1,i;
	while(current)
	{
		printf("Element - %d \n",count);
		if(current->filename)
			printf("File name: %s\n",current->filename);
		if(current->fileNumber)
			printf("File number: %d\n",current->fileNumber);
		if(current->sha1[0] != '\0')	{
			printf("SHA1: ");
			for(i=0; i<20; i++)
				printf("%02x",current->sha1[i]);
		}
		if(current->nonce[0] != '\0')	{
			printf("Nonce: ");
			for(i=0; i<20; i++)
				printf("%02x",current->nonce[i]);
		}
		if(current->bitVector)
		{
			printf("Bit Vector: \n");
			for(int i=0;i<256;i++)
			{
				printf("%d",(current->bitVector)[i]); 
			}
		}
		printf("\n");
		current = current->next;
		count++;
	}
	return;
}

// this function tells if an index list is empty or not
// this decision is taken on the basis of the 'head' of the list
//	if the 'head' points to NULL, the list is empty
int IsEmpty_index( indexNode* head)
{
	return (head == NULL) ? 1 : 0;
}

// this function returns the number of elements in a index list
int GetLength_index( indexNode* head)
{
	int size=0;
	 indexNode *current = head;
	while(current)	{
		if(current->isDeleted == false)
			size++;
		current = current->next;	
	}
	return size;
}

// return the file numbers of all the files stored on this node
// it takes the numbers from the index lists
void GetFileNumbers( indexNode* head, int *file_nums)
{
	indexNode *current = head;
	int i=0;
	while(current)	{
		if(current->isDeleted == false)
			file_nums[i++] = current->fileNumber;
		current = current->next;
	}
	return;
}


// return the file numbers of all the files stored on this node
int getActiveFileNumbers(indexNode* head, int **fileNumber)
{
	indexNode *current = head;
	int count=0;
	while(current)	
	{		
		if(!current->isDeleted)
		{
			count++;
			(*fileNumber) = (int *) realloc((*fileNumber),count * sizeof(int));
			(*fileNumber)[count - 1] = current->fileNumber;
		}
		current = current->next;
	}
	return count;
}

int getFileNumber_index(indexNode * head, char * filename,unsigned char * nonce)
{
	indexNode *current = head;
	while(current)
	{
		if(!current-> isDeleted && (strcmp(current->filename,filename)==0) && memcmp(current->nonce,nonce,20)==0)
		{
			return current->fileNumber;	
		}
		current = current->next;
	}
	return -1;		// not found
}
