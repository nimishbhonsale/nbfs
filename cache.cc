// this file has all the funciton which are needed for LRU
//	operation on the 'cache' filesystem.
#include "sv_node.h"

// insertFileInCache()
//	this funciton checks wether the size of the stored file is less than the
//		CacheSize or not
//	if not, it return false, and the file is too large to store in cache
//	if yes, then it keeps deleting the files according to LRU mark order
//		until the cache has enough free space for the incoming new file
//	the function return true in this case and the file can be stored in cache
bool insertFileInCache(indexNode* head,long filesize)
{
	if(filesize > CacheSize * 1024)
		return false;
	else
		while(filesize >= cacheFreeSize)	{
			cacheFreeSize += removeLRUNode(head);
		}
	return true;
}

// setLRUPriority() & setLRUPriority_by_filenum()
//	this function puts a file at the end of LRU access list
//	that means this file is the least recently accessed file
//	this function should be called whenever a file is accessed e.g
//		search, get, store etc. (but not for status command)

// the diffrence between these two functions is as follows:
//	the user is supposed to use only the later function using a filenumber
//	the former is used by the later function and should not be called by the user
bool setLRUPriority(indexNode* head, char *filename,unsigned char *nonce)
{	
	return (setNodePriority(head,filename,nonce,++cacheFilePiority));
}

int setLRUPriority_by_filenum(int file_num)
{
	char name[MAXLINE];
	sprintf(name, "%s/%s/%d.meta", HomeDir, FilesDir, file_num);
	parsed_metadata meta;
	meta = parse_metafile(name);
	setLRUPriority(fnameList, meta.FileName, meta.Nonce);
	return 0;
}

// setNodePriority()
//	this funciton is NOT available to the user, he or she should use
//		setLRUPriority_by_filenum() instead.
//	this funcitoni is used only internally by setLRUPriority_by_filenum() funciton
//	it is used to changed the 'prioriy' variable in the file-index list
int setNodePriority(indexNode* head, char *filename,unsigned char *nonce,int priority)
{
	indexNode *current = head;
	while(current)
	{
		if(current->isCached && !current-> isDeleted && (strcmp(current->filename,filename)==0) && memcmp(current->nonce,nonce,20)==0)
		{
			return current->lruPriority = priority;	
		}
		current = current->next;
	}
	return -1;		// not found
}

// removeLRUNode()
//	this funciton is unsed 'internally' and not available to a user
//	this is used by the 'insertFileInCache()' function (see above)
//	it checks the 'priority' field of all the elements of the filename-list
//	then find the elemnet with the 'minimum priority'
//	then checks if the max-priority allowed is reached or not
//	if not, then it mark this file for deletion by setting the 'isDeleted' flag
//	next time, when the 'dumper' thread runs, all the files that are marked for
//			deletion are physically removed from the filesystem (see dump.cc)
//			by calling the 'cleanfiles()' functions (see delete.cc)

// So, effectively, this funciton is used for making space in the cache
//			to store a new file
long removeLRUNode(indexNode* head)
{
	indexNode *current = head;
	indexNode *candidateNode = head; 
	// this field is set to 32768 which is the maximum number of files
	//	that can be stored by a node in its filesystem
	// it can be changed if more files are requried (maximum is 2^31)
	int minPriority = 32768;
	while(current)
	{
		if(current->lruPriority < minPriority && current->isCached && !(current-> isDeleted))
		{
			minPriority = current->lruPriority ;
			candidateNode = current;
		}
		current = current->next;
	}
	if(minPriority < 32768)
	{
		candidateNode->isDeleted = true;
		return candidateNode->filesize;
	}
	return 0;		// not found
}

// bring_to_permanent()
//	this function brings a file from cache to permanent area
//	used after getting a file using 'get' command
int bring_to_permanent(int file_num)
{
	indexNode *current = fnameList;
	while(current!= NULL)
	{
		if(current->fileNumber == file_num && current->isCached && !current->isDeleted)
		{
			current->isCached = false;
			cacheFreeSize += current->filesize;	
			return 1;
		}
		current = current->next;
	}
	return -1;
}
	


