== Design ==

	1. THREADS
		> Each connection with other node will be handled by a
		  separate thread.
		> Command line interface will be handled by a separate thread.
		> Dispatcher and timer will be separate threads.
		> Dumper thread which will create the index files from the
		  index data structures.
		> 'main()' function is also a thread waiting for other
		  connections.

	2. FLOODING
		> Each connection has a separate queue for messages.
		> There is one dispatcher queue for inter thread communication.
		> connection A, wanting to send a message to conneciton B of a
		  node, will put this message in the dispatcher queue.
		> The dispatcher will put this message in the queue of Node B.
		> Node B will check its list and will forward the message to
		  its neighbor.
		> If a message is for flooding, dispatcher will put the message
		  in the queue of every other connection.

		> There is one UOID list which stores all the seen UOIDs of
		  flooding messages i.e. STRQ, JNRQ, CKRQ.
		> Any such message if present in the list, will not be put for
		  flooding.


	3. INDEX FILES
		> There are 3 index files namely:
		  - name_index : File that stores the filename and corresponding
		    filenumber. It follows the following format:
		 <filename> <filenumber> <nonce> <filesize> <priority> <isCached>\n

		  - sha1_index : File that stores the sha1 of a file and
		    corresponding filenumber. It follows the following format:
		 <sha1> <filenumber> <isCached>\n

		  - kwrd_index : File that stores bit Vector corresponding to a
		    single keyword and filenumber.It follows the following format:
		 <bitVector> <filenumber> <isCached>\n

		A separate thread which runs periodically creates the disk image
		of the 3 in memory linked lists corresponding to the 3 index files.
		The index files are placed in the HomeDirectory of the node.

		Also, there is a filenum.txt file which stores the latest
		filenumber and the cache priority number (for LRU).

	4. LRU
  	     > The linked list is used to maintain the LRU priority and the filesize.
	       Whenever a file needs to be inserted in the cache a decision is made
	       on which node to evict based on the prioiryt associated with the node.
	       Accordingly, the node is marked for deletion and its disk image is
	       updated when the dumper thread write the index and other files.
	     > Additionally,  a variable is maintained to store the cash free size
	       to facilitate faster eviction.

	5. TEMPORARY FILES
	     > There are additonal temporary files placed in the HomeDirectory/tmp
	       folder which are used to store intermediate processing data.
	     > There is also a filenum.txt file that store the last filenumber
	       assigned when the last file was stored in the HomeDirectory/files
	       directory.

== STATUS OUTPUT NAM FILE ==

	> BEACON NODES:		in RED color and black label
	> NON BEACON NODES:	in BROWN color and black label
	> LINKS:		in BLUE color

	> Please press 'Relayout' to see the topology properly in the NAM
	  window.

== CREDITS ==

	> INIPARSER from Nicolas Devillard <ndevilla@free.fr>
	Copyright (c) 2000 by Nicolas Devillard.
	Written by Nicolas Devillard. Not derived from licensed software.
	> FILES: iniparser.h, iniparser.cc, dictionary.h, dictionary.cc

	> DESIGN motivation from Prof Bill Cheng's slides.