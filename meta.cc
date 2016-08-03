// prints the results of a STATUS FILES query in the 'extfile'
//	specified by the user in the command
#include "sv_node.h"

// takes the input as the data structure status_response_data (see list.h)
// also takes the number of recordes to print in 'extfile'
// the 'extfile_lock' is locked before printing anything and released later
int write_meta(status_response_data response, int num_records)
{
	int i;
	if(fp_extfile == NULL)	return -1;	// CTRL-C given by user,
										// don't write anything else

	pthread_mutex_lock(&extfile_lock);
	if(num_records == 0)
		fprintf(fp_extfile, "nunki.usc.edu:%d has no file\n", \
									response.host_port);
	else if(num_records == 1)
		fprintf(fp_extfile, "nunki.usc.edu:%d has the following file\n", \
									response.host_port);
	else
		fprintf(fp_extfile, "nunki.usc.edu:%d has the following files\n", \
									response.host_port);

	// write each metadata in the file
	for(i=0; i<num_records; i++)
		fprintf(fp_extfile, "%s\n", response.n_hostname[i]);

	pthread_mutex_unlock(&extfile_lock);
	return 0;
}
