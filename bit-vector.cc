// this file has all functions needed for bit vector generation
// it's a rough implementation of a 'bloom filter'

// the bit vector is string of 256 integers
// the first 128 integers stores the bit vector corresponding to last nine
//		SHA1 bytes of the input keyword
// the last 128 integers stores the bit vector corresponding to last nine
//		MD5 bytes of the input keyword
// both are combined to get the final 256 bit-vector
#include "sv_node.h"
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <math.h>

// generateSHA1BitVector()
//	this function retunrs the first 128 integers of the bit-vector
//	these are based on the last 9 bytes of SHA1 of the given keyword
int generateSHA1BitVector(int * bitVector,char * word)
{
	char sha1_buf[SHA_DIGEST_LENGTH];
	char sha1_buf1[2*SHA_DIGEST_LENGTH];
	int i=0;
	SHA1((const unsigned char *)word, strlen(word), (unsigned char *)sha1_buf);
	for (i = 0; i < SHA_DIGEST_LENGTH; i++) 
	{
		sprintf(&sha1_buf1[2*i],"%02x",(unsigned char)sha1_buf[i]);
	}
	char temp[3];
	temp[0]= sha1_buf1[2*SHA_DIGEST_LENGTH-3];
	temp[1]= sha1_buf1[2*SHA_DIGEST_LENGTH-2];
	temp[2]= sha1_buf1[2*SHA_DIGEST_LENGTH-1];
	int deci = (int)strtoul(temp, 0, 16);
	deci = deci & 511;
	deci += 512;
	bitVector[256-((int)deci/4)-1]= (int)pow(2,(deci%4));
	return 1;
}

// generateMD5BitVector()
//	this function retunrs the last 128 integers of the bit-vector
//	these are based on the last 9 bytes of MD5 of the given keyword
int generateMD5BitVector(int * bitVector,char * word)
{
	char key[16];
	char key1[32];
	int i=0;
	MD5_CTX md5Context;
	MD5_Init(&md5Context);
	MD5_Update(&md5Context,word,strlen(word));
	MD5_Final((unsigned char *)key,&md5Context);
	for (i = 0; i < 16; i++) 
	{
		sprintf(&key1[2*i],"%02x",(unsigned char)key[i]);
	}
	char temp[3];
	temp[0]= key1[29];
	temp[1]= key1[30];
	temp[2]= key1[31];
	int deci = (int)strtoul(temp, 0, 16);
	deci = deci & 511;
	bitVector[256-((int)deci/4)-1]=(int)pow(2,(deci%4));
	return 1;
}

// generateBitVector()
//	this function combines the first and last 128 integers generated above
//	this function takes the 'refrence' of the bit-vector
//	so it can be called multiple times to keep combining the bitvectors
//		of different keywords
//	returns -1 in case of an error and 1 otherwise.

// An example------
//	let's say we need the final bitvector of keywords 'foo bar foobar'
//		and the resultant bit-vector should be in 'vector[256]'
//	So call this function generateBitVector() three times using with these
//		parameters::
//	1. bitVector = vector & word = foo
//	2. bitVector = vector & word = bar
//	3. bitVector = vector & word = foobar
//	
//	So, finally, 'vector' will contain the combined bit-vector of
//		all the three keywords.
int generateBitVector(int *bitVector, char * word)
{	
	if(!generateSHA1BitVector(bitVector,word))
		return -1;
	if(!generateMD5BitVector(bitVector,word))
		return -1;
	return 1;
}

