#include "sv_node.h"
#include "iniparser.h"	// for parsing the ini file

#define LINESIZE 80	// maximum size of a line is INI file

// entries for neighbors for non beacon node
char **neighbor_hostname;
int *neighbor_port;
int neighbor_count= 0;

int get_int_from_ini(char *parameter, int defaultVal);
float get_float_from_ini(char *parameter, float defaultVal);

// first parse the command line parameters
dictionary * ini ;

void node_init(int argc, char *argv[])
{
	Assert((argc>1 && argc<4), "\tusage: sv_node [-reset] <filename>.ini\n");
	int i;
	char *ini_file_name;

	for(i=1; i<argc; i++)	{
		if(!strcmp(argv[i], "-reset"))	{
			// ini file is a must, so check about it
			Assert(argc==3, "\tusage: sv_node [-reset] <filename>.ini\n");
			reset_flag = true;
			continue;
		}
		ini_file_name = (char *)malloc(strlen(argv[i]));
		strcpy(ini_file_name, argv[i]);
	}
	// command line parameters parsed

	// now parse the ini file found above
	ini = iniparser_load(ini_file_name);	//load the ini parser

	// first collect mandatory parameters
	Port = iniparser_getint(ini, "init:Port", -1);
	Assert(Port != -1, "\tERROR parsing INI file: [init]-Port\n");

	char *takeFromINI;
	takeFromINI = iniparser_getstring(ini, "init:HomeDir", NULL);
	Assert(takeFromINI != NULL, "\tERROR parsing INI file: [init]-HomeDir\n");
	HomeDir = (char *)malloc(strlen(takeFromINI));
	strcpy(HomeDir, takeFromINI);

	takeFromINI = iniparser_getstring(ini, "init:Location", NULL);
	Assert(takeFromINI != NULL, "\tERROR parsing INI file: [init]-Location\n");
	Location = atoi(takeFromINI);

	// now collect optional parameters

	// LogFilename
	takeFromINI = iniparser_getstring(ini, "init:LogFilename", NULL);
	if(takeFromINI == NULL)	{
		LogFilename = (char *)malloc(strlen(HomeDir) + 11);
		// this is because the file name is given with full path of homeDir
		// and the length of default filename "servant.log" is 11
		strcpy(LogFilename, HomeDir);
		strcat(LogFilename, "/servant.log");
	}	else	{
		LogFilename = (char *)malloc(strlen(HomeDir) + strlen(takeFromINI));
		// here the file name is taken from the INI file
		strcpy(LogFilename, HomeDir);
		strcat(LogFilename, "/");
		strcat(LogFilename, takeFromINI);
	}

	// get all the optional integer values from [init] sections
	// the second argument of get_int_from_ini() is the default value
	AutoShutdown = get_int_from_ini("init:AutoShutdown", 900);
	TTL = get_int_from_ini("init:TTL", 30);
	MsgLifetime = get_int_from_ini("init:MsgLifetime", 30);
	InitNeighbors = get_int_from_ini("init:InitNeighbors", 3);
	JoinTimeout = get_int_from_ini("init:JoinTimeout", 15);
	KeepAliveTimeout = get_int_from_ini("init:KeepAliveTimeout", 60);
	MinNeighbors = get_int_from_ini("init:MinNeighbors", 2);
	NoCheck = get_int_from_ini("init:NoCheck", 0);
	CacheSize = get_int_from_ini("init:CacheSize", 500);
	PermSize = get_int_from_ini("init:PermSize", 500);

	// get all the optional float values from the [init] section
	// the second argument of get_float_from_ini() is the default value
	CacheProb = get_float_from_ini("init:CacheProb", 0.1);
	StoreProb = get_float_from_ini("init:StoreProb", 0.1);
	NeighborStoreProb = get_float_from_ini("init:NeighborStoreProb", 0.1);

	// get the value of Retry from the [beacons] section
	Retry = get_int_from_ini("beacons:Retry", 30);

	// find out if this node is a beacon node or a regular node
	// pass the hostname:port to check if it is there in the
	//			beacons list or not
	takeFromINI = (char *)malloc(MAXLINE);
	strcpy(takeFromINI, "beacons:nunki.usc.edu:");
	char myport[10];
	sprintf(myport, "%d", Port);
	strcat(takeFromINI, myport);	// append it's own port number
	int node_type = iniparser_find_entry(ini, takeFromINI);
	my_node_type = (node_type == 1) ? BEACON : REGULAR;

	// now free up the ini_parser
	//	CAUTION: This freedict(ini) function contains bugs
	//	It corrputs the memory and add havocs to the programmer
	//	keep it commented, even if some resources are not released
	//iniparser_freedict(ini);

	// now take the hostnames and port numbers of the beacon nodes
	// this can't be parsed using the ini parser code as key value is not given
	// and also, the key name is not known in advance
	// so, we have to parse it using standard file handling commands
	FILE *fp;
	fp = fopen(ini_file_name, "r");
	Assert(fp != NULL, "\tERROR in opening INI file \n");

	char ini_line[LINESIZE];
	do	{
		fgets(ini_line, LINESIZE, fp);
	}while(strcmp(ini_line, "[beacons]\n"));
	
	i=0;
	char beacon[20][LINESIZE];
	while(!feof(fp))	{
		if(fgets(ini_line, LINESIZE, fp) == NULL)
			break;

		// here, Retry=XX is an optional parameter, so we need to
		// discard it.
		if(strstr(ini_line, "Retry=") != NULL)
			continue;

		// now remove the '\n' character from the end of line
		// fgets() gets the string with the '\n' character
		size_t line_len = strlen(ini_line);
		if(ini_line[line_len-1] == '\n')
			ini_line[line_len-1] = '\0';
		strcpy(beacon[i], ini_line);
		i++;
	}
	fclose(fp);
	beacon_count = i;	// number of beacon nodes

	// allocate memory to store all beacon hostname and port numbers
	// use 'strtok' to extract hostname and port num from the string
	beacon_host = (char **)malloc(beacon_count * sizeof(char *));
	for(i=0; i<beacon_count; i++)
		beacon_host[i] = (char *)malloc(strlen(beacon[i]) * sizeof(char));
	beacon_port = (int *)malloc(beacon_count * sizeof(int));
	char *host =NULL; 
	char *portnum =NULL;
	for(i=0; i<beacon_count; i++)	{
		host = strtok(beacon[i], ":");	// store hostname
		strcpy(beacon_host[i], host);
		portnum = strtok(NULL, ":");	// store port number
		beacon_port[i] = atoi(portnum);

//		printf("beacon %d: %s \t%d\n", i, beacon_host[i], beacon_port[i]); 
	}

	// open the LOG file and keep a pointer to it (make it global)
	fp_log = fopen(LogFilename, "a");
	// if HomeDir doesn't exist, shut down
	Assert(fp_log != NULL, 
		"\tERROR: Unable to write in Home Directory, Check \'ini\' file\n");

//	printf("Reset_flag = %s\n", reset_flag == true ? "true" : "false");
//	printf("node type = %s\n", my_node_type == BEACON ? "BEACON" : "REGULAR");
//	printf("Port = %d\n", Port);
//	printf("Location = %lu\n", Location);
//	printf("HomeDir = %s\n", HomeDir);
//	printf("LogFilename = %s\n", LogFilename);
//	printf("AutoShutdown = %d\n", AutoShutdown);
//	printf("TTL = %d\n", TTL);
//	printf("MsgLifetime = %d\n", MsgLifetime);
//	printf("InitNeighbors = %d\n", InitNeighbors);
//	printf("JoinTimeout = %d\n", JoinTimeout);
//	printf("KeepAliveTimeout = %d\n", KeepAliveTimeout);
//	printf("MinNeighbors = %d\n", MinNeighbors);
//	printf("NoCheck = %d\n", NoCheck);
//	printf("CacheProb = %f\n", CacheProb);
//	printf("StoreProb = %f\n", StoreProb);
//	printf("NeighborStoreProb = %f\n", NeighborStoreProb);
//	printf("CacheSize = %d\n", CacheSize);
//	printf("PermSize = %d\n", PermSize);
//
//	printf("Retry = %d\n", Retry);
}

bool parse_neighbors()
{

	// if this is a non-beacon node, then check if init_neighbor_file exists!
	// returns false if the files doesn't exists
	// else, parse the file and returns true
	bool isExist = true;
	char file_input[MAXLINE];
	FILE *fp_neighbor;

	neighbor_file = SetBuffer(strlen(HomeDir) + strlen("/init_neighbor_list"));
	strcpy(neighbor_file, HomeDir);
	strcat(neighbor_file, "/init_neighbor_list");
	if( (fp_neighbor = fopen(neighbor_file, "r")) == NULL)	{
		isExist = false;	// file not there, have to join
		return isExist;
	}

	// if -reset is specified by the user, then return true for this
	if(reset_flag && firsttime)
		return true;

	char *result = NULL;
	int num = 1;

	neighbor_port = (int *)malloc(1*sizeof(int));

	if(my_node_type == REGULAR && isExist == true)	{
		// read the input file and parse it
		fgets(file_input, MAXLINE, fp_neighbor);
		while(!feof(fp_neighbor))	{
			file_input[strlen(file_input)-1] = '\0';
			result = strtok(file_input, ":");
			neighbor_hostname = 
				(char **)realloc(neighbor_hostname, num*sizeof(char *));
			neighbor_hostname[num-1] = (char *)malloc(MAXLINE*sizeof(char));
			strcpy(neighbor_hostname[num-1], result);
			result = strtok(NULL, ":");
			neighbor_port = (int *)realloc(neighbor_port, num*sizeof(int));
			neighbor_port[num-1] = atoi(result);
			num++;

			fgets(file_input, MAXLINE, fp_neighbor);
		}
		neighbor_count = num-1;		// number of neighbors to ask for connection
	}
	return isExist;	// which is 'true'
}

int get_int_from_ini(char *parameter, int defValue)
{
	int getValue = 0;
	getValue = iniparser_getint(ini, parameter, -1);
	if(getValue == -1)
		getValue = defValue;	// key not found, return the default value
	return getValue;			// key found, return it
}

float get_float_from_ini(char *parameter, float defValue)
{
	char *getValue;
	getValue = iniparser_getstring(ini, parameter, NULL);
	if(getValue == NULL)
		return defValue;	// key not found, return the default value
	return atof(getValue);	// key found, return it
}
