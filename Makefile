CC      = g++
RM      = rm -f
ALL	= sv_node
COMPILE_SHA = -I/home/scf-22/csci551b/openssl/include
LINK_SHA = -L/home/scf-22/csci551b/openssl/lib -lcrypto
SWITCHES = -lsocket -lnsl -lresolv -pthreads

OBJ = sv_node.o init.o client_part.o server_part.o \
	utilities.o list.o dictionary.o iniparser.o \
	dispatcher.o msg_parse.o std_input.o nam.o \
	join_handler.o join_asker.o log.o store.o \
	bit-vector.o index_list.o signature.o dump.o \
	delete.o meta.o cache.o search_get.o

all: $(ALL)

sv_node: $(OBJ)
	$(CC) -o sv_node $(OBJ) $(SWITCHES) $(LINK_SHA)

sv_node.o: sv_node.cc sv_node.h
	$(CC) -g -c sv_node.cc -Wall

init.o: init.cc
	$(CC) -g -c init.cc -Wall

client_part.o: client_part.cc
	$(CC) -g -c client_part.cc -Wall

server_part.o: server_part.cc
	$(CC) -g -c server_part.cc -Wall

dictionary.o: dictionary.cc dictionary.h
	$(CC) -g -c dictionary.cc -Wall

iniparser.o: iniparser.cc iniparser.h
	$(CC) -g -c iniparser.cc -Wall

utilities.o: utilities.cc
	$(CC) -g -c utilities.cc $(COMPILE_SHA) -Wall

list.o: list.cc
	$(CC) -g -c list.cc -Wall

dispatcher.o: dispatcher.cc
	$(CC) -g -c dispatcher.cc -Wall

msg_parse.o: msg_parse.cc
	$(CC) -g -c msg_parse.cc $(COMPILE_SHA) -Wall

std_input.o: std_input.cc
	$(CC) -g -c std_input.cc -Wall

nam.o: nam.cc
	$(CC) -g -c nam.cc -Wall

join_handler.o: join_handler.cc
	$(CC) -g -c join_handler.cc -Wall

join_asker.o: join_asker.cc
	$(CC) -g -c join_asker.cc -Wall

log.o: log.cc
	$(CC) -g -c log.cc -Wall

store.o: store.cc
	$(CC) -g -c store.cc -Wall $(COMPILE_SHA)

bit-vector.o: bit-vector.cc
	$(CC) -g -c bit-vector.cc -Wall $(COMPILE_SHA)

index_list.o: index_list.cc
	$(CC) -g -c index_list.cc -Wall $(COMPILE_SHA)

signature.o: signature.cc
	$(CC) -g -c signature.cc -Wall

dump.o: dump.cc
	$(CC) -g -c dump.cc -Wall

delete.o: delete.cc
	$(CC) -g -c delete.cc -Wall

meta.o: meta.cc
	$(CC) -g -c meta.cc -Wall

cache.o: cache.cc
	$(CC) -g -c cache.cc -Wall

search_get.o: search_get.cc
	$(CC) -g -c search_get.cc -Wall

clean:
	$(RM) $(ALL) *.o
