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
#define	BUFSIZE			2048

#define true            1
#define false           0

#define MSG_CHECK (buf[0] == 'M' && buf[3] == ' ')
#define MSGE_CHECK (buf[0] == 'M' && buf[3] == 'E')
#define IMAGE_CHECK (buf[0] == 'I' && buf[4] == 'E')
#define REGISTER_CHECK (buf[0] == 'R' && buf[8] == ' ')
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

typedef struct longRequest {
    char *buf;
    int ID;
    int len;
} LONGREQUEST;

typedef struct longwrite {
    char *buf;
    int ID;
    int len;
} LONGWRITE;


// optimized
LONGWRITE* createLongWrite(int ID, char *buf, int len) {
    LONGWRITE *longwrite = (LONGWRITE*) malloc(sizeof(LONGWRITE));
    longwrite -> ID = ID;
    longwrite -> len = len;
    longwrite -> buf = buf;
    return longwrite;
}

// LONGWRITE* createLongWrite(int ID, char *buf, int len) {
//     LONGWRITE *longwrite = (LONGWRITE*) malloc(sizeof(LONGWRITE));
//     longwrite -> ID = ID;
//     longwrite -> buf = (char*) malloc(sizeof(char) * len);
//     longwrite -> len = len;
//     int i;
//     for (i = 0; i < len; i++)
//         longwrite -> buf[i] = buf[i];
//     return longwrite;
// }

LONGREQUEST* createLongRequest(int ID, char *buf, int len) {
    LONGREQUEST *req = (LONGREQUEST*) malloc(sizeof(LONGREQUEST));
    req -> ID = ID;
    req -> len = len;
    req -> buf = (char*) malloc(sizeof(char) * len);
    int i;
    for (i = 0; i < len; i++)
        req -> buf[i] = buf[i];
    return req;
}

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

int min(int a, int b) {
    if (a > b)
        return b;
    return a;
}

TAG     *tags = NULL;
int     *registerAll, *busy;
int	    nfds;

int passivesock( char *service, char *protocol, int qlen, int *rport );

// Supporting functions
void handleRequest(int ID, char *buf, int cc);
void registerTag(int ID, char *tag);
void deregisterTag(int ID, char *tag);
void registerUser(int ID, TAG *tag);
void sendMessage(char *tag, char *buf);
void *writeLong(void *ign);

pthread_t heavyRequestThread;
pthread_t *threads;
pthread_mutex_t *mutexes;
char tag[BUFSIZE];
void *handleHeavyRequest(void *ign);

void print(char *buf, int len) {
    int i;
    printf("DEBUG:\n");
    for (i = 0; i < len; i++)
        printf("%c", buf[i]);
    printf("#\n");
}

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
    threads = (pthread_t*) calloc(nfds, sizeof(pthread_t));
    mutexes = (pthread_mutex_t*) calloc(nfds, sizeof(pthread_mutex_t));
    busy = (int*) calloc(nfds, sizeof(int));

    int mi;
    for (mi = 0; mi < nfds; mi++)
        pthread_mutex_init(&mutexes[mi], NULL);

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
			if (fd != msock && FD_ISSET(fd, &rfds)) {
                if (busy[fd] == false) {
                    printf("not bysy fd is %d\n", fd);
                    if ( ( cc = read( fd, buf, BUFSIZE ) ) <= 0 ) {
                        printf( "The client has gone.\n" );
                        (void) close(fd);
                        FD_CLR( fd, &afds );
                    } else {
                        // processing requests from user
                        print(buf, cc);
                        handleRequest(fd, buf, cc);
                    }
                }
			}
		}
	}
}

void handleRequest(int id, char *buf, int cc) {
    if (REGISTERALL_CHECK) {
        deregisterTag(id, "DEREGISTERALL");
        registerAll[id] = true;
    } else if (DEREGISTERALL_CHECK) {
        deregisterTag(id, "DEREGISTERALL");
        registerAll[id] = false;
    } else if (REGISTER_CHECK) {
        int taglen = cc - 9 - 2;
        memcpy(tag, &buf[9], taglen);
        tag[taglen] = '\0';
        registerTag(id, tag);
    } else if (DEREGISTER_CHECK) {
        int taglen = cc - 11 - 2;
        memcpy(tag, &buf[11], taglen);
        tag[taglen] = '\0';
        deregisterTag(id, tag);
    } else
    if (MSG_CHECK || MSGE_CHECK || IMAGE_CHECK) {
        // MSG, MSGE and IMAGE with tag or without tag
        // these may need a background execution
        busy[id] = true;
        LONGREQUEST *req = createLongRequest(id, buf, cc);
        pthread_create( &heavyRequestThread, NULL, handleHeavyRequest, (void*) &req );
    }
}

void *handleHeavyRequest(void *ign) {
    LONGREQUEST *req = *((LONGREQUEST**) ign);
    char    buf[BUFSIZE];
    char    *tag = (char*) calloc(BUFSIZE, sizeof(char));
    char    *msg;
    int     taglen;
    int     msglen;
    int     bytecount = 0;
    int     buflen;
    int     i, j, pos, cc;
    int     ID = req -> ID;

    for (i = 0; i < req -> len; i++)
        buf[i] = req -> buf[i];

    // printf("request length %d\n", req -> len);

    if (MSG_CHECK) {
        pos = 4;
        if (buf[pos] == '#') {
            for (pos = pos + 1; buf[pos] != ' '; pos++)
                tag[taglen++] = buf[pos];
            tag[taglen] = '\0';
        } else {
            tag = NULL;
        }
        msg = (char*) malloc(req -> len * sizeof(char));
        msglen = req -> len;
        int qq;
        for (qq = 0; qq < msglen; qq++) {
            msg[qq] = buf[qq];
        }

    } else {
        int shift = 0; // shift due to #
        if (IMAGE_CHECK)
            pos = 6;
        else
            pos = 5;

        if (buf[pos] == '#') {
            // extracting tag
            for (pos = pos + 1; buf[pos] != ' '; pos++)
                tag[taglen++] = buf[pos];
            tag[taglen] = '\0';
            shift = 1;

        } else {
            tag = NULL;
        }


        // extracing bytecount
        for (pos = pos + shift; buf[pos] != '/'; pos++)
            bytecount = bytecount * 10 + (int) (buf[pos] - '0');


        int slash = pos;
        msg = (char*) malloc((pos + bytecount) * sizeof(char));
        for (i = 0; i < req -> len; i++)
            msg[i] = buf[i];

        int notbytecount = i - slash - 1;

        int last = bytecount - notbytecount;
        while ( last > 0 ) {
            int cc = read( ID, buf, min(BUFSIZE, last));
            for (j = 0; j < cc; j++)
                msg[i++] = buf[j];
            last -= cc;
        }
        msglen = i;
    }

    busy[ID] = false;
    //
    // printf("after reading fully %d\n", msglen);
    // fflush(stdout);

    /*
        extracted tag and message from requests
        sending to users
    */

    if (tag != NULL) {
        TAG *cur = tags;
        for (; cur != NULL; cur = cur -> next) {
            if (!strcmp(cur -> tag, tag)) {
                // sending message to all users with such tag
                USER *curID = cur -> users;
                for (; curID != NULL; curID = curID -> next) {
                    LONGWRITE *longwrite = createLongWrite(curID -> ID, msg, msglen);
                    pthread_create( &threads[curID -> ID], NULL, writeLong, (void*) &longwrite );
                    pthread_join( threads[curID -> ID], NULL );
                }
            }
        }
    }

    for (i = 0; i < nfds; i++) {
        if (registerAll[i] == 1) {
            LONGWRITE *longwrite = createLongWrite(i, msg, msglen);
            pthread_create( &threads[i], NULL, writeLong, (void*) &longwrite);
            pthread_join( threads[i], NULL );
        }
    }

    free(req -> buf);
    free(req);
    free(msg);
    free(tag);
    pthread_exit(NULL);
}


void *writeLong(void *ign) {
    LONGWRITE *longwrite = *((LONGWRITE**) ign);

    int ID = longwrite -> ID;
    // mutex on
    pthread_mutex_lock(&mutexes[ID]);

    write ( ID, longwrite -> buf, longwrite -> len);
    free(longwrite);

    // mutex off
    pthread_mutex_unlock(&mutexes[ID]);

    pthread_exit(NULL);
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

    if (prev == NULL) {
        tag -> users = node;
    } else {
        prev -> next = node;
    }
    // printf("%d registered to tag %s\n", node -> id, tag -> tag);
}
