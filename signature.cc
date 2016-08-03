// this files has all the functions related to digital signatures
// it creates the private and public keys, verify two keys, execute popen etc.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// executePopen()
//	executes the popen command on UNIX prompt (it forks a new shell)
//	the output is redirected so that it doesn't appear on the stdout
int executePopen(char * cmd, char * mode)
{
	FILE *pfp=NULL;
	if ((pfp=(FILE*)popen(cmd, mode)) == NULL) 
	{
		fprintf(stderr, "Cannot execute '%s'.\n", cmd);
		return 0;
	}
	pclose(pfp);
	return 1;
}


// createKey()
//	this function creates the private key file and the public key file
//	stores the files as specified in the parameters
int createKey(char * privateKey, char * publicKey)
{
	char cmd[1024];
	char sz_opensslpath[]="/usr/lsd/openssl/0.9.7g/bin/openssl";
	char sz_param[]=" req -new -nodes -newkey rsa:1024 -x509 -subj \"/C=US/ST=CA/L=LA/O=USC/OU=Servant-`  date +%d%b%Y`-rootca/CN=`hostname`/emailAddress=`  /usr/ucb/whoami`@usc.edu\"  -keyout ";
	char sz_param1[]=" -out ";
	snprintf(cmd, sizeof(cmd), "bash -c \"%s %s %s %s %s\" 2>&1", sz_opensslpath, sz_param,privateKey,sz_param1,publicKey);		
	return(executePopen(cmd,"r"));
} 

// signCertificate()
//	Signs the 'inFilename' using the 'privateKey' and puts the result
//			in the 'outFilename'
// the 'outFilename' is the digitally signed copy of the 'inFilename'
int signCertificate(char * privateKey, char * publicKey, char * inFilename, char* outFilename)
{
  char cmd[1024];// buf[256];
  char sz_opensslpath[]="/usr/lsd/openssl/0.9.7g/bin/openssl";
  char sz_args[]= "smime -sign -in ";
  char sz_args1[] = " -out "; 
  char sz_args2[] = " -nocerts -signer ";
  char sz_args3[] = " -inkey ";
  FILE *pfp=NULL;  
  snprintf(cmd, sizeof(cmd), "bash -c \"%s %s %s %s %s %s %s %s %s\" 2>&1", sz_opensslpath, sz_args,inFilename,sz_args1,outFilename,sz_args2,privateKey,sz_args3,publicKey);
	if ((pfp=(FILE*)popen(cmd, "r")) == NULL) 
	{
		fprintf(stderr, "Cannot execute '%s'.\n", cmd);
		return 0;
	}
	pclose(pfp);	
    return 1;
}

// verifyCertificate()
//	Verifies a digitally signed file 'filename' with a public key certificate
//		file 'certFilename'
//	this is used for verifying the ownership before deleting any file
int verifyCertificate(char * filename, char * certFilename)
{
  char cmd[1024], buf[256];
  char sz_opensslpath[]="/usr/lsd/openssl/0.9.7g/bin/openssl", sz_args[]= "smime -verify -noverify -in", sz_args1[] = " -signer ", sz_args2[] = "-certfile ";
  FILE *pfp=NULL;
  int flag = -1;
  snprintf(cmd, sizeof(cmd), "bash -c \"%s %s %s %s %s %s %s 2>&1\"", sz_opensslpath, sz_args, filename,sz_args1,certFilename,sz_args2,certFilename);
	if ((pfp=(FILE*)popen(cmd, "r")) == NULL) 
	{
		fprintf(stderr, "Cannot execute '%s'.\n", cmd);
		return -1;
	}
	while (fgets(buf, sizeof(buf), pfp) != NULL) 
	{
		if(buf && strcmp(buf, "Verification successful\n") ==0)
		{
			flag = 1;
			break;
		}		
	}
	pclose(pfp);
	
 return flag;
}
