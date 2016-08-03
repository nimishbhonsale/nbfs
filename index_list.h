#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct keywordNode
{
	int * bitVector;
	int fileNumber;
	unsigned char sha1[20];
	unsigned char nonce[20];
	char * filename;
	bool isDeleted;
	bool isCached;
	int lruPriority;
	long filesize;
	struct keywordNode * next;
} indexNode;


extern int Add_index( indexNode** ,  indexNode );
extern int IsEmpty_index( indexNode*);
extern int GetLength_index( indexNode*);
extern int GetData_index(indexNode*,int*);
extern void PrintList_index(indexNode *head);
int dumpIndexData(indexNode* nameFile,indexNode* sha1File, \
					indexNode* keywordsFile, char * homeDirectory);
int createIndexData(indexNode** nameFile, indexNode** sha1File, \
					indexNode** keywordsFile, char * homeDirectory);
extern int getFileNumber_index(indexNode* head, char * filename, \
					unsigned char * nonce);
void GetFileNumbers( indexNode* head, int *file_nums);
int getActiveFileNumbers(indexNode* head, int **fileNumber);
