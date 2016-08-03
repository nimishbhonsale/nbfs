// list.cc
//---------------------------------------------------------
//	implementation of a LINKED LIST to store
//	Each list is identified by a header pointing to the list

#include "list.h"

// declare all the new lists here
// each list is identified by a header pointer for the list

// APPEND
//	next pointer points at the head of the next node
void Append(event*& first, event event_data)
{
	event *temp = (event *)malloc(sizeof(event));
	if(temp == NULL)	{
		fprintf(stderr, "malloc() failed... out of memory\n");
		exit(1);
	}

	// store the parameters in the structure
	temp->flooding = event_data.flooding;
	temp->port_num = event_data.port_num;
	temp->header = event_data.header;
	temp->buffer = SetBuffer(event_data.header.data_length);
	memcpy(temp->buffer, event_data.buffer, event_data.header.data_length); 
	temp->next = first;	//next pointer points to the old top of the list

	first = temp;	// new head of the list
}

// DELETE
//	the next pointer of pervious node points to NULL
//	Should be called only after checking that list is not empty
//	by calling IsEmpty() first. This error handling is not done
//	in this function. please make sure to check it before.
event Delete(event*& first)
{
	event toDelete;
	event *temp = first;
	event *temp_prv = NULL;

	while(temp->next != NULL)	{	// go to the first event
		temp_prv = temp;
		temp = temp->next;
	}
	toDelete.flooding = temp->flooding;
	toDelete.port_num = temp->port_num;
	toDelete.header = temp->header;
	toDelete.buffer = SetBuffer(temp->header.data_length);
	memcpy(toDelete.buffer, temp->buffer, temp->header.data_length); 
	toDelete.next = NULL;
	
	if(temp_prv != NULL)
		temp_prv->next = NULL;	//remove the last event
	else first = NULL;

	// retrun the details of the deleted events
	return toDelete;
}

// ISEMPTY
//	checks if the list is empty or not
bool IsEmpty(event*& first)
{
	// if the head is pointing to NULL, then the list is empty
	return (first == NULL) ? true : false;
}

// LISTSIZE
//	returns the number of nodes in the list
int ListSize(event*& first)
{
	int size=0;
	// keep checking till the next pointer points to NULL
	event *temp = first;
	while(temp != NULL)	{
		size++;
		temp = temp->next;	// advance it to the next node
	}
	return size;
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//	functions definitions for the UOID msg_entry structure (see list.h)
msg_entry *head = NULL;	// pointing at the first element of the list

void Add_UOID(msg_entry entry)
{
	msg_entry *temp = (msg_entry *)malloc(sizeof(msg_entry));
	if(temp == NULL)	{
		fprintf(stderr, "malloc() failed... out of memory\n");
		exit(1);
	}

	// store the parameters in the structure
	memcpy(temp->UOID, entry.UOID, 20);
	temp->port_num = entry.port_num;
	temp->next = head;	//next pointer points to the old top of the list

	head = temp;	// new head of the list
}

void Delete_UOID(msg_key key)	// Removes the entry with given key
{
	msg_entry *temp;
	temp = head;
	while(temp != NULL)	{
		if(!memcmp(temp->UOID, key, 20))	{
			temp = temp->next;	// point to the next element
			temp->next = NULL;	// nullify this element (delete it)
			break;
		}
		temp = temp->next;
	}
}

msg_value Get_UOID(msg_key key)	// Gets the value of given key
{
	msg_entry *temp;
	temp = head;
	while(temp != NULL)	{
		if(memcmp(temp->UOID, key, 20) == 0)
			return temp->port_num;	// return this value
		temp = temp->next;
	}
	return 0;		// not found
}

bool Find_UOID(msg_key key)		// Checks if an entry is there with given key
{
	msg_entry *temp;
	temp = head;
	while(temp != NULL)	{
		if(memcmp(temp->UOID, key, 20) == 0)
			return true;	// return this value
		temp = temp->next;
	}
	return false;		// not found
}

void Print_UOID()
{
	msg_entry *temp;
	temp = head;
//	int i;
//	while(temp!= NULL)	{
//		for(i=0; i<20; i++)
//			printf("%02x", temp->UOID[i]);
//		printf("\t%5d\n", temp->port_num);
//		temp = temp->next;
//	}
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
//	functions definitions for the FileID msg_entry structure (see list.h)
file_entry *fhead = NULL;	// pointing at the first element of the list

void Add_FileID(file_entry entry)
{
	file_entry *temp = (file_entry *)malloc(sizeof(file_entry));
	if(temp == NULL)	{
		fprintf(stderr, "malloc() failed... out of memory\n");
		exit(1);
	}

	// store the parameters in the structure
	memcpy(temp->FileID, entry.FileID, 20);
	temp->file_num = entry.file_num;
	temp->next = fhead;	//next pointer points to the old top of the list

	fhead = temp;	// new fhead of the list
}

void Delete_FileID(msg_key key)	// Removes the entry with given key
{
	file_entry *temp;
	temp = fhead;
	while(temp != NULL)	{
		if(!memcmp(temp->FileID, key, 20))	{
			temp = temp->next;	// point to the next element
			temp->next = NULL;	// nullify this element (delete it)
			break;
		}
		temp = temp->next;
	}
}

msg_value Get_FileID(msg_key key)	// Gets the value of given key
{
	file_entry *temp;
	temp = fhead;
	while(temp != NULL)	{
		if(memcmp(temp->FileID, key, 20) == 0)
			return temp->file_num;	// return this value
		temp = temp->next;
	}
	return 0;		// not found
}

bool Find_FileID(msg_key key)		// Checks if an entry is there with given key
{
	file_entry *temp;
	temp = fhead;
	while(temp != NULL)	{
		if(memcmp(temp->FileID, key, 20) == 0)
			return true;	// return this value
		temp = temp->next;
	}
	return false;		// not found
}

void Print_FileID()
{
	file_entry *temp;
	temp = fhead;
	int i;
	while(temp!= NULL)	{
		for(i=0; i<20; i++)
			printf("%02x", temp->FileID[i]);
		printf("\t%5d\n", temp->file_num);
		temp = temp->next;
	}
}
