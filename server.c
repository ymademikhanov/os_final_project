#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <pthread.h>

#define	QLEN			5
#define	BUFSIZE			4096

#define true            1
#define false           0

#define MSG_CHECK (buf[0] == 'M' && buf[3] == ' ')
#define MSGE_CHECK (buf[0] == 'M' && buf[3] == 'E')
#define IMAGE_CHECK (buf[0] == 'I' && buf[4] == 'E')
#define REGISTER_CHECK (buf[0] == 'R' && buf[0] == ' ')
#define DEREGISTER_CHECK (buf[0] == 'D' && buf[10] == ' ')
#define REGISTERALL_CHECK (buf[0] == 'R' && buf[8] == 'A')
#define DEREGISTERALL_CHECK (buf[0] == 'D' && buf[10] == 'A')


typedef struct User {
    int ID;
    struct User *next;
} USER;

typedef struct Tag {
    char *tag;
    USER *users;
    struct Tag *next;
} TAG;

TAG* createTag(char *tag) {
    TAG *node = (TAG*) malloc(sizeof(TAG));
    node -> tag = (char*) malloc(strlen(tag) * sizeof(char));
    node -> users = NULL;
	node -> next = NULL;
    strcpy(node -> tag, tag);
    return node;
}

USER* createUser(int ID) {
    USER *node = (USER*) malloc(sizeof(USER));
    node -> next = NULL;
    node -> ID = ID;
    return node;
}

typedef struct Msg {
	char *tag;
	char *msg;
} MSG;

TAG     *tags = NULL;
int     *registerAll;
int	    nfds;
char    tag[BUFSIZE];
char    msg[BUFSIZE];

int passivesock( char *service, char *protocol, int qlen, int *rport );

// Supporting functions
void handleRequest(int ID, char *buf);
void registerTag(int ID, char *tag);
void deregisterTag(int ID, char *tag);
void registerUser(int ID, TAG *tag);

/* 	The server ... */
int main( int argc, char *argv[] )
{
    struct sockaddr_in	fsin;
    pthread_t 		client_thread;
	fd_set			rfds;
	fd_set			afds;
	char			buf[BUFSIZE];
	char			*service;
    int				rport = 0, msock, alen, fd, cc;

	switch (argc) {
		case	1:
			// No args? let the OS choose a port and tell the user
			rport = 1;
			break;
		case	2:
			// User provides a port? then use it
			service = argv[1];
			break;
		default:
			fprintf( stderr, "usage: server [port]\n" );
			exit(-1);
	}

	msock = passivesock( service, "tcp", QLEN, &rport );

	if (rport) {
		//	Tell the user the selected port
		printf( "server: port %d\n", rport );
		fflush( stdout );
	}

	nfds = getdtablesize();
	FD_ZERO(&afds);
	FD_SET( msock, &afds );

    registerAll = (int*) calloc(nfds, sizeof(int));

	for (;;) {
		memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));
		if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0) {
			fprintf( stderr, "server select: %s\n", strerror(errno) );
			exit(-1);
		}

		/*	Handle the main socket - a new guy has checked in  */
		if (FD_ISSET( msock, &rfds)) {
			int	ssock;
			alen = sizeof(fsin);
			ssock = accept( msock, (struct sockaddr *)&fsin, &alen );
			if (ssock < 0) {
				fprintf( stderr, "accept: %s\n", strerror(errno) );
				exit(-1);
			}
			/* start listening to this guy */
			FD_SET( ssock, &afds );
		}

		/*	Handle the participants requests  */
		for ( fd = 0; fd < nfds; fd++ ) {
			if (fd != msock && FD_ISSET(fd, &rfds) ) {
				printf("%d\n", fd);
				if ( ( cc = read( fd, buf, BUFSIZE ) ) <= 0 ) {
					printf( "The client has gone.\n" );
					(void) close(fd);
					FD_CLR( fd, &afds );
				} else {
                    buf[cc - 1] = '\0';
                    /*
                        ^^^^^^^^^^^^^
                        check-out above line
                    */
					// processing requests from user
                    handleRequest(fd, buf);
				}
			}
		}
	}
}


void handleRequest(int id, char *buf) {
    int taglen;
    if (MSG_CHECK) {
        // this may need a background execution

    } else if (MSGE_CHECK) {
        // this may need a background execution

    } else if (IMAGE_CHECK) {
        // this may need a background execution

    } else if (REGISTERALL_CHECK) {
        deregisterTag(id, "DEREGISTERALL");
        registerAll[id] = true;
    } else if (DEREGISTERALL_CHECK) {
        deregisterTag(id, "DEREGISTERALL");
        registerAll[id] = false;
    } else if (REGISTER_CHECK) {
        taglen = (int)strlen(buf) - 9;
        memcpy(tag, &buf[9], taglen);
        tag[taglen] = '\0';
        registerTag(id, tag);
    } else if (DEREGISTER_CHECK) {
        taglen = (int)strlen(buf) - 11;
        memcpy(tag, &buf[11], taglen);
        tag[taglen] = '\0';
        deregisterTag(id, tag);
    }
}

void registerTag(int ID, char *tag) {
	TAG *cur = tags, *prev = NULL;
    for (; cur != NULL; cur = cur -> next) {
        if (!strcmp(cur -> tag, tag)) {
            // such tag exists and just add user id to tag
            registerUser(ID, cur);
            return;
        }
        prev = cur;
    }

    TAG *node = createTag(tag);
    registerUser(ID, node);

    if (prev != NULL) {
        prev -> next = node;
    } else {
        tags = node;
    }
}

void deregisterTag(int ID, char *tag) {
    TAG *cur = tags, *prev = NULL;
    for (; cur != NULL; cur = cur -> next) {
        if (!strcmp(tag, "DEREGISTERALL") || !strcmp(cur -> tag, tag)) {
            USER *curID = cur -> users, *prevID = NULL;
            for (; curID != NULL && curID -> ID != ID; curID = curID -> next)
                prevID = curID;

            if (curID != NULL) {
                if (prevID != NULL)
                    prevID -> next = curID -> next;
                else
                    cur -> users = NULL;
                free(curID);
            }
            break;
        }
        prev = cur;
    }
}

void registerUser(int ID, TAG *tag) {
    USER *cur = tag -> users, *prev = NULL;
    for (; cur != NULL; cur = cur -> next) {
        if (cur -> ID == ID)
            return;
        prev = cur;
    }
    USER *node = createUser(ID);

    if (prev == NULL)
        tag -> users = node;
    else
        prev -> next = node;

    // printf("%d registered to tag %s\n", node -> id, tag -> tag);
}
