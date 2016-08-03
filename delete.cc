// This files stores all the functions related to 'deleting' a file from the ndoe.
// -> Marks the 'isDeleted' bit in the index lists
// -> Physically deletes a file from the HomeDir
#include "sv_node.h"

// cleanFiles()
//	Physically delete the requrid meta, pem and data files from the disk
//	the files are no longer available with the node anymore
//	'lockListMutex' should be taken before calling this function
void cleanFiles(int fileNum)
{
	char* filename = (char *) malloc(100);
	if(filename)
	{
		memset(filename,'\0',100);
		sprintf(filename,"%s/%s/%d.meta",HomeDir,FilesDir,fileNum);
		remove(filename);
	
		memset(filename,'\0',100);
		sprintf(filename,"%s/%s/%d.data",HomeDir,FilesDir,fileNum);
		remove(filename);

		memset(filename,'\0',100);
		sprintf(filename,"%s/%s/%d.pem",HomeDir,FilesDir,fileNum);
		remove(filename);	
	}
}

// deleteFile()
//	it uses other delete functions to mark the 'delete' bit in all
//		the lists
int deleteFile(char * filename,unsigned char *sha1,unsigned char *nonce)
{
	pthread_mutex_lock(&lockListMutex);
	int fileNum = -1;
	fileNum = setDeleteFlagForFile(fnameList,filename,nonce);
	if(fileNum >0)
	{
		int retVal = -1;
		retVal = setDeleteFlag(sha1List,fileNum);
		if(retVal >0)
			retVal = setDeleteFlag(kwordList,fileNum);
		if(retVal > 0)
			cleanFiles(fileNum);
	}
	pthread_mutex_unlock(&lockListMutex);
	return 1;
}

// setDeleteFlag()
//	this function turns on the 'delete' flag for all the files
//		which are associated with perticular filenumber in the
//		'keywords' list.
//	Then it returns 1 if it is successful in doing so
int setDeleteFlag(indexNode* head, int fileNumber)
{
	bool found = false;
	if(!IsEmpty_index(head))
	{	
		indexNode * current = head;
		while(current != NULL)
		{
			if(current->fileNumber == fileNumber && !(current->isDeleted))
			{
				current->isDeleted = 1;
				found = true;
			}
			current = current->next;
		}
		if(found)
			return 1;
	}
	return -1;
}

// setDeleteFlagForFile()
//	Sets the 'delete' flag for a file in the 'fname' list
//	It is done on only one file which is found uniquely from the
//		filename and it's Nonce ID.
//	The files which have 'delete' flag set are not returned for any 'search'
int setDeleteFlagForFile(indexNode* head, char *filename,unsigned char *nonce)
{
	if(!IsEmpty_index(head))
	{
		indexNode* current = head;
		while(current != NULL)
		{
			if(strcmp(current->filename,filename)== 0 && !(current->isDeleted) && memcmp(current->nonce,nonce,20)== 0)
			{
				current->isDeleted = true;
				if(current->isCached)
					cacheFreeSize += current->filesize;
				return current->fileNumber;
			}
			current = current->next;
		}
	}
	return -1;
}
