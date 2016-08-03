#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern char *SetBuffer(int);

typedef struct head	{
	unsigned char	msg_type;
	unsigned char	UOID[20];
	uint8_t			TTL;
	int8_t			reserved;
	int32_t			data_length;
}msg_header;

// structures for the 'data' part of all the messages
typedef struct hello	{
	int				host_port;
	char			*hostname;
}hello_data;

typedef struct status	{
	char			status_type;
}status_data;

typedef struct status_response	{
	unsigned char	UOID[20];
	int16_t			host_info_len;
	uint16_t		host_port;
	char			*hostname;
	int				*record_len;
	int				*n_host_port;// neighbor's port number
	char			**n_hostname;// neighbor's hostname
}status_response_data;

typedef struct join	{
	unsigned int	host_location;
	uint16_t		host_port;
	char			*hostname;
}join_data;

typedef struct join_response	{
	unsigned char	UOID[20];
	unsigned int	Distance;
	uint16_t		host_port;
	char			*hostname;
}join_response_data;

typedef struct check_reponse	{
	unsigned char	UOID[20];
}check_response_data;

		
typedef struct store	{
	int				metadata_len;
	char			*metadata;
	int				cert_len;
	char			*certificate;
	char			*file_data;
}store_data;

typedef struct srch	{
	uint8_t			search_type;
	char			*key;
}search_data;

typedef struct srch_response	{
	unsigned char	UOID[20];
	int				*next_length;
	unsigned char	**FileID;
	char			**metadata;
}search_response_data;

typedef struct get	{
	unsigned char	FileID[20];
}get_data;

typedef struct get_response	{
	unsigned char	UOID[20];
	int				metadata_len;
	char			*metadata;
	int				cert_len;
	char			*certificate;
	char			*file_data;
}get_response_data;

// a list element for dispatcher and connection threads
typedef struct dispatcher	{
	bool			flooding;
	int				port_num;
	msg_header		header;
	char			*buffer;
	dispatcher		*next;
} event;

// member functions
//	each function at least takes the head of the list
//	as one parameter to identify the list
void Append(event*&, event);
event Delete(event*&);
bool IsEmpty(event*&);
int ListSize(event*&);

// functions and structure for storing UOID of flooded messages
// implemented using a linked list
// supports Add(element), Remove(key), Get(key) and Find(key) commands
// There will be only one list for all the connections of a node
typedef unsigned char* msg_key;
typedef int32_t msg_value;
typedef struct UOID_element	{
	unsigned char	UOID[20];	// it is the key
	msg_value		port_num;	// it is the value
	UOID_element	*next;		// pointing to the next element
}msg_entry;

// see list.cc for definitions of these functions
void Add_UOID(msg_entry);		// Adds at the front of the list
void Delete_UOID(msg_key);		// Removes the entry with given key
msg_value Get_UOID(msg_key);	// Gets the value of given key
bool Find_UOID(msg_key);		// Checks if an entry is there with given key
void Print_UOID();				// Prints the entire list

// functions and structure for storing FileID of search results
// implemented using a linked list
// supports Add(element), Remove(key), Get(key) and Find(key) commands
// There will be only one list for all the connections of a node
typedef struct FileID_element	{
	unsigned char	FileID[20];	// it is the key
	msg_value		file_num;	// it is the value
	FileID_element	*next;		// pointing to the next element
}file_entry;

// see list.cc for definitions of these functions
void Add_FileID(file_entry);		// Adds at the front of the list
void Delete_FileID(msg_key);	// Removes the entry with given key
msg_value Get_FileID(msg_key);	// Gets the value of given key
bool Find_FileID(msg_key);		// Checks if an entry is there with given key
void Print_FileID();			// Prints the entire list
