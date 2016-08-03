#include "sv_node.h"
// array to get rid of duplicate entries for nodes
// else the 'nam' file will go to hell
int *seen;
int add_nam;

int write_nam(status_response_data response, int num_records)
{
	// write one record for each node and each link
	// data is available in the structure 'response'
	// see list.h for the details
	int i,j,k, source_port;
	char record[MAXLINE];
	bool isDup;
	bool isBeacon;	// to give a different color to beacon nodes
					// they are special ones

	if(fp_extfile == NULL)	return -1;

	// first put the host
	pthread_mutex_lock(&extfile_lock);
	source_port = (int)response.host_port;
	
	// find out wether this node is a beacon or not
	isBeacon = false;
	for(i=0; i<beacon_count; i++)	{
		if(source_port == beacon_port[i])	{
			isBeacon = true;
			break;
		}
	}

	isDup = false;
	for(i=0; i<add_nam; i++)	{
		if(seen[i] == source_port)	{
			isDup = true;
			break;
		}
	}
	if(isDup == false)	{
		add_nam++;
		seen = (int *)realloc(seen, add_nam*sizeof(int));
		seen[add_nam -1] = source_port;

		if(isBeacon)
			sprintf(record, "n -t * -s %d -c red -i black\n", source_port);
		else
			sprintf(record, "n -t * -s %d -c brown -i black\n", source_port);

		if( (fputs(record, fp_extfile)) == EOF)	return -1;
		fflush(fp_extfile);
		//fputs(record, stdout);
		//fflush(stdout);
	}

	for(i=0; i<num_records; i++)	{
		// each record indicates a neighbor of the host_port		
		isDup = false;
		for(j=0; j<add_nam; j++)	{
			if(seen[j] == response.n_host_port[i])	{
				isDup = true;
				break;
			}
		}
		if(isDup == false)	{
			add_nam++;
			seen = (int *)realloc(seen, add_nam*sizeof(int));
			seen[add_nam -1] = response.n_host_port[i];

			// find out wether this node is a beacon or not
			// if a beacon, give it red color, else brown color
			isBeacon = false;
			for(k=0; k<beacon_count; k++)	{
				if(response.n_host_port[i] == beacon_port[k])	{
					isBeacon = true;
					break;
				}
			}
			if(isBeacon)
				sprintf(record, "n -t * -s %d -c red -i black\n", response.n_host_port[i]);
			else
				sprintf(record, "n -t * -s %d -c brown -i black\n", response.n_host_port[i]);
			
			if( (fputs(record, fp_extfile)) == EOF)	return -1;
			fflush(fp_extfile);
			//fputs(record, stdout);
			//fflush(stdout);
		}

		sprintf(record, "l -t * -s %d -d %d -c blue\n",
							source_port, response.n_host_port[i]);
		if( (fputs(record, fp_extfile)) == EOF)	return -1;
		//fputs(record, stdout);
		fflush(fp_extfile);
		//fflush(stdout);
	}
	pthread_mutex_unlock(&extfile_lock);
	return 0;
}
