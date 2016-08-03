#include "sv_node.h"

// global arrays to hold the timeout values for connected threads
int timeout_join[FD_SETSIZE];
int timeout_alive[FD_SETSIZE];
int timeout_node_dead[FD_SETSIZE];
int timeout_check;

extern int AutoShutdown;

pthread_mutex_t timout;
pthread_mutex_t msg_parse_lock;
pthread_mutex_t dispatcher_lock;
pthread_cond_t dispatcher_cond;
pthread_mutex_t conn_list_lock[FD_SETSIZE];
pthread_mutex_t fileNumMutex ;

// this thread will decrement the counter values by 1
// after each second. this is a global thread working
// exclusively to keep the timer for all the connections
void *countdown(void *threadid)
{
	int i;
	while(1)	{
		sleep(1);	// do this every second

		// first check if needs to shutdown
		if(shutdown_flag == true)
			break;

		pthread_mutex_lock(&timout);
		for(i=0; i<FD_SETSIZE; i++)	{
			if(timeout_join[i] > 0)
				timeout_join[i]--;
			if(timeout_alive[i] > 0)
				timeout_alive[i]--;
			if(timeout_node_dead[i] > 0)
				timeout_node_dead[i]--;
		}
		if(timeout_check > 0)
			timeout_check--;
		pthread_mutex_unlock(&timout);
	}

	pthread_exit(NULL);
	return 0;
}


// this thread will router or flood the messages
// it will act on the dispatcher list which includes all the
// events put by any active connection or by the user.
// it checks the event and routes to proper destination
// or it forwards it to all the connections
// it simply copies the event to other connection's list
// to do the needful.
void *dispatch(void *threadid)
{
	// wait until there is something to process
	// a thread, after putting something, will signal this
	event curr_event;
	int i=0, conn_num=0;
	int16_t t;

	while(1)	{
		pthread_mutex_lock(&dispatcher_lock);
		// wait till there is an event in the list
		// but before that, check if node has SHUTDOWN
		if(IsEmpty(dispatcher_first))		{
			pthread_cond_wait(&dispatcher_cond, &dispatcher_lock);
			if(shutdown_flag == true)	{
				pthread_mutex_unlock(&dispatcher_lock);
				break;
			}
		}
		// take the first entered event from the list
		curr_event = Delete(dispatcher_first);
		pthread_mutex_unlock(&dispatcher_lock);

		if(curr_event.flooding == true)	{
			// this message should be flooded
			for(i=0; i<FD_SETSIZE; i++)	{
				if(active_conn[i] != -1)	{
					pthread_mutex_lock(&conn_list_lock[i]);
					//printf("dispatcher: give event to neighbour %d\n", active_conn[i]);
					Append(connection_first[i], curr_event);
					pthread_mutex_unlock(&conn_list_lock[i]);
				}
			}	// put in the list of all the connections
		}		// flooding done

		if(curr_event.flooding == false)	{
			// this message should be put in only 1 connection's list
			// this connection is identified by the port number

			// TRY TO PUT A HASH TABLE STRUCTURE IF TIME PERMITS
			// FOR MAPPING PORT NUMBER WITH THREAD NUMBER
			// (TRY TO USE STL IF POSSIBLE)
			pthread_mutex_lock(&timout);
			for(i=0; i<FD_SETSIZE; i++)	{
				if(curr_event.port_num == active_conn[i])
					break;
			}
			conn_num = i;		// save the connection number
			pthread_mutex_unlock(&timout);
			// now put the message in this connection's list
			pthread_mutex_lock(&conn_list_lock[conn_num]);
			t=0;
			memcpy(&t, &curr_event.buffer[20], 2);
			//printf("in dispatcher, putting in %d: t= %d\n", active_conn[conn_num], t);
			Append(connection_first[conn_num], curr_event);
			pthread_mutex_unlock(&conn_list_lock[conn_num]);
		}
	}

	pthread_exit(NULL);
	return 0;
}

// this thread will dump the index lists into the index files
// it does so continuously once every 2 seconds
// it deletes all the 3 files, and then recreates them using
// the index lists
void *dump(void *threadid)
{
	int retval;
	while(1)	{
		sleep(2);

		// after waking up, dump the lists to the files
		pthread_mutex_lock(&lockListMutex);
		if( (retval = dumpIndexData(fnameList, sha1List, \
								kwordList, HomeDir)) == -1)	{
			pthread_mutex_unlock(&lockListMutex);
			kill_threads();	// error in dumping data, shut down the node
		}
		pthread_mutex_unlock(&lockListMutex);

		// also write the latest fileNumber to the file
		pthread_mutex_lock(&fileNumMutex);
		FILE *fp;
		char filenum_name[MAXLINE];
		sprintf(filenum_name, "%s/filenum.txt", HomeDir);
		if( (fp = fopen(filenum_name, "w")) == NULL)
			continue;
		fprintf(fp, "%ld %ld", fileNumber, cacheFilePiority);
		fclose(fp);
		pthread_mutex_unlock(&fileNumMutex);

		if(shutdown_flag)
			pthread_exit(NULL);

		// go back at the start of the loop
	}
}
