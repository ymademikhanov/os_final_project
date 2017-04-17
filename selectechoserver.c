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

typedef struct User {
    int id;
    struct User *next;
} USER;

typedef struct Tag {
    char *tag;
    USER *users;
    struct Tag *next;
} TAG;

typedef struct Msg {
	char *tag;
	char *msg;
} MSG;

TAG *all_tags = NULL;

int *registered_to_all;
int	nfds;

int passivesock( char *service, char *protocol, int qlen, int *rport );
void register_tag(int id, char *tag);
void addIdToTag(int id, TAG *tag);
void deregister_tag(int id, char *tag);

void send_message(MSG *msg);
void send_message_encrypted(MSG *msg);


/* 	The server ... */

int main( int argc, char *argv[] )
{
	char			buf[BUFSIZE];
	char			*service;
	struct sockaddr_in	fsin;
	int				msock;
	int				ssock;
	fd_set			rfds;
	fd_set			afds;
	int				alen;
	int				fd;
	int				rport = 0;
	int				cc;
	pthread_t 		x;
	char request[20];
	char tag_string[BUFSIZE];
	char msg_string[BUFSIZE];



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

	registered_to_all = (int*) malloc(sizeof(int) * nfds);
	int ii = 0;
	for (; ii < nfds; ii++)
		registered_to_all[ii] = 0;

	FD_ZERO(&afds);
	FD_SET( msock, &afds );

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
					// processing requests from user
					if (strcmp(buf, "REGISTERALL") == 0) {
						// registering user to all tags
						deregister_tag(fd, "DEREGISTERALL");
						registered_to_all[fd] = 1;
					} else
					if (strcmp(buf, "DEREGISTERALL") == 0) {
						// deregistering all
						deregister_tag(fd, "DEREGISTERALL");
						registered_to_all[fd] = 0;
					} else {
						// registering to specific tags
						memcpy (request, &buf[0], 8);
						request[8] = '\0';
						if (strcmp(request, "REGISTER") == 0) {
							if (registered_to_all[fd] == 0) {
								// registering to specific tag
								int tag_len = strlen(buf) - 9 + 1;
								memcpy(tag_string, &buf[9], tag_len);
								tag_string[tag_len] = '\0';
								register_tag(fd, tag_string);
							}
						}
						memcpy (request, &buf[0], 10);
						request[10] = '\0';
						if (strcmp(request, "DEREGISTER") == 0) {
							if (registered_to_all[fd] == 0) {
								// derregistering from specific tag
								int tag_len = strlen(buf) - 11 + 1;
								memcpy(tag_string, &buf[11], tag_len);
								tag_string[tag_len] = '\0';
								deregister_tag(fd, tag_string);
							}
						}
						memcpy (request, &buf[0], 3);
						request[3] = '\0';
						if (strcmp(request, "MSG") == 0 && buf[3] == ' ') {
							// message
							if (buf[4] == '#') {
								// with tag
								// extracting tag
								int j = 5, counter = 0;
								for (; j < strlen(buf) && buf[j] != ' '; j++)
									tag_string[counter++] = buf[j];
								tag_string[counter++] = '\0';

								int msg_len = strlen(buf) - counter + 1;
								memcpy(msg_string, &buf[counter + 5], msg_len);
								msg_string[msg_len] = '\0';

								MSG *msg = (MSG*) malloc(sizeof(MSG));

								msg -> tag = (char*) malloc(sizeof(char) * strlen(tag_string));
								msg -> msg = (char*) malloc(sizeof(char) * strlen(msg_string));

								strcpy(msg -> tag, tag_string);
								strcpy(msg -> msg, msg_string);

								send_message( msg );

								// freeing up memory
								free(msg -> tag);
								free(msg -> msg);
								free(msg);
							} else {
								// without tag
								int i, msg_len = strlen(buf) - 4 + 1;
								memcpy(msg_string, &buf[4], msg_len);
								msg_string[msg_len] = '\0';
								for ( i = 0; i < nfds; i++) {
									if (i != msock && FD_ISSET(i, &afds) && registered_to_all[i])
										write( i, buf, strlen(buf));
								}
							}
						}

                        memcpy (request, &buf[0], 4);
						request[4] = '\0';
						if (strcmp(request, "MSGE") == 0) {
							// message
							if (buf[5] == '#') {
								// with tag
								// extracting tag
								int j = 6, counter = 0;
								for (; j < strlen(buf) && buf[j] != ' '; j++)
									tag_string[counter++] = buf[j];
								tag_string[counter++] = '\0';

								int msg_len = strlen(buf) - counter + 1;
								memcpy(msg_string, &buf[counter + 6], msg_len);
								msg_string[msg_len] = '\0';

								MSG *msg = (MSG*) malloc(sizeof(MSG));

								msg -> tag = (char*) malloc(sizeof(char) * strlen(tag_string));
								msg -> msg = (char*) malloc(sizeof(char) * strlen(msg_string));

								strcpy(msg -> tag, tag_string);
								strcpy(msg -> msg, msg_string);


								send_message_encrypted( msg );

								// freeing up memory
								free(msg -> tag);
								free(msg -> msg);
								free(msg);
							} else {
								// without tag
								int i, msg_len = strlen(buf) - 5 + 1;
								memcpy(msg_string, &buf[5], msg_len);
								msg_string[msg_len] = '\0';
								for (i = 0; i < nfds; i++) {
									if (i != msock && FD_ISSET(i, &afds) && registered_to_all[i])
										write( i, buf, strlen(buf));
								}
							}
						}
					}
				}
			}
		}
	}
}

void send_message(MSG *msg) {
	TAG *cur = all_tags;
	char whole[BUFSIZE];

	whole[0] = 'M'; whole[1] = 'S'; whole[2] = 'G'; whole[3] = ' '; whole[4] = '#';
	int i, counter = 5;
	for (i = 0; i < strlen(msg -> tag); i++)
		whole[counter++] = msg -> tag[i];

	whole[counter++] = ' ';

	for (i = 0; i < strlen(msg -> msg); i++)
		whole[counter++] = msg -> msg[i];

	whole[counter] = '\0';

	while (cur != NULL) {
        if (strcmp(cur -> tag, msg -> tag) == 0) {
            // sending message to all users with such tag
			USER *cur2 = cur -> users;
		    while (cur2 != NULL) {
		        write ( cur2 -> id, whole, strlen(whole));
		        cur2 = cur2 -> next;
		    }
        }
        cur = cur -> next;
    }

	for (i = 0; i < nfds; i++)
		if (registered_to_all[i] == 1)
			write ( i, whole, strlen(whole));
}

void send_message_encrypted(MSG *msg) {
	TAG *cur = all_tags;
	char whole[BUFSIZE];

	whole[0] = 'M'; whole[1] = 'S'; whole[2] = 'G'; whole[3] = 'E'; whole[4] = ' '; whole[5] = '#';
	int i, counter = 6;
	for (i = 0; i < strlen(msg -> tag); i++)
		whole[counter++] = msg -> tag[i];

	whole[counter++] = ' ';

	for (i = 0; i < strlen(msg -> msg); i++)
		whole[counter++] = msg -> msg[i];

	whole[counter] = '\0';

    printf("%s\n", whole);
	while (cur != NULL) {
        if (strcmp(cur -> tag, msg -> tag) == 0) {
            // sending message to all users with such tag
			USER *cur2 = cur -> users;
		    while (cur2 != NULL) {
		        write ( cur2 -> id, whole, strlen(whole));
		        cur2 = cur2 -> next;
		    }
        }
        cur = cur -> next;
    }

	for (i = 0; i < nfds; i++)
		if (registered_to_all[i] == 1)
			write ( i, whole, strlen(whole));
}

void register_tag(int id, char *tag) {
	TAG *cur = all_tags, *prev = NULL;
    while (cur != NULL) {
        if (strcmp(cur -> tag, tag) == 0) {
            // such tag exists and just add user id to tag
            addIdToTag(id, cur);
            return;
        }
        prev = cur;
        cur = cur -> next;
    }

    TAG *node = (TAG*) malloc(sizeof(TAG));
    node -> tag = (char*) malloc(strlen(tag) * sizeof(char));
    node -> users = NULL;
	node -> next = NULL;
    strcpy(node -> tag, tag);

    addIdToTag(id, node);

    if (prev == NULL)
        all_tags = node;
    else
        prev -> next = node;
}

void addIdToTag(int id, TAG *tag) {
    USER *cur = tag -> users, *prev = NULL;
    while (cur != NULL) {
        if (cur -> id == id)
            return;
        prev = cur;
        cur = cur -> next;
    }

    USER *node = (USER*) malloc(sizeof(USER));
    node -> next = NULL;
    node -> id = id;
	// printf("%d registered to tag %s\n", node -> id, tag -> tag);
    if (prev == NULL)
        tag -> users = node;
    else
        prev -> next = node;
}

void deregister_tag(int id, char *tag) {
	TAG *cur = all_tags, *prev = NULL;
    while (cur != NULL) {
        if (strcmp(tag, "DEREGISTERALL") == 0 || strcmp(cur -> tag, tag) == 0) {
			USER *cur_id = cur -> users, *prev2 = NULL;
		    while (cur_id != NULL && cur_id -> id != id) {
		        prev2 = cur_id;
		        cur_id = cur_id -> next;
		    }

			if (cur_id != NULL) {
				if (prev2 != NULL)
					prev2 -> next = cur_id -> next;
				else
					cur -> users = NULL;
				free(cur_id);
			}

            break;
        }
        prev = cur;
        cur = cur -> next;
    }
}
