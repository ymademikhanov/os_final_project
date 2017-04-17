#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>


#define BUFSIZE		4096
#define LOADSIZE 1

_Bool alive = 1;
int sockets[LOADSIZE];
pthread_t threads[LOADSIZE];
char key[BUFSIZE] = {'a', '\0'};

sem_t mutex;

int connectsock( char *host, char *service, char *protocol );
void *listen_to_server( void *ign );
void *listen_to_user( void *ign );


void ksa(unsigned char* state, unsigned char* key, int len);
void prga(unsigned char* state, unsigned char* out, int len);

void num2str(int x, char *str);


/*	Client */

int main( int argc, char *argv[] )
{
	char		*service;
	char		*host = "localhost";




	pthread_t listen_to_user_thread, listen_to_server_thread;

	switch( argc ) {
		case    2:
			service = argv[1];
			break;
		case    3:
			host = argv[1];
			service = argv[2];
			break;
		default:
			fprintf( stderr, "usage: chat [host] port\n" );
			exit(-1);
	}

	int i;
	for (i = 0; i < LOADSIZE; i++) {
		/*	Create the socket to the controller  */
		sockets[i] = connectsock( host, service, "tcp" );
		if ( sockets[i] == 0 ) {
			fprintf( stderr, "Cannot connect to server.\n" );
			exit( -1 );
		}
		pthread_create(&threads[i], NULL, listen_to_server, (void*) &sockets[i]);
	}

	printf( "The server is ready, please start sending to the server.\n" );
	printf( "Type q or Q to quit.\n" );
	fflush( stdout );



		printf("\nPROTOCOL for setting up key for encryption/decryption. Type:\nSETKEY key\n");

		fflush(stdout);
	int status;
	status = pthread_create( &listen_to_user_thread, NULL, listen_to_user, NULL);

	// waiting pthreads to be finished
	pthread_join(listen_to_user_thread, NULL );
	for (i = 0; i < LOADSIZE; i++)
		pthread_join( threads[i], NULL );

	for (i = 0; i < LOADSIZE; i++)
		close( sockets[i] );
}

void *listen_to_user( void *ign ) {
	// 	infinitely listening to server
	char	buf[BUFSIZE];
	char 	command[BUFSIZE];
	int		cc, i;

	while ( fgets( buf, BUFSIZE, stdin ) != NULL && alive) {
		// If user types 'q' or 'Q', end the connection
		if ( buf[0] == 'q' || buf[0] == 'Q' ) {
			// saying we do not listening to server and user anymore
			alive = 0;
		} else {
			for (i = 0; i < LOADSIZE; i++) {
				memcpy (command, &buf[0], 6);
				command[6] = '\0';
				if (strcmp(command, "SETKEY") == 0) {
					// user wants to set key for encryption/decryption
					int keylen = (strlen(buf) - 7);
					memcpy(key, &buf[7], keylen);
					key[keylen] = '\0';

					printf("Your new key is: %s", key);
				} else {
					memcpy(command, &buf[0], 4);
					command[4] = '\0';
					if (strcmp(command, "MSGE") == 0) {
						if (buf[5] != '#') {
							// message w/o tag
							int j;
							char message[BUFSIZE];
							int msglen = strlen(buf) - 5;
							unsigned char state[256];
							unsigned char *out = (char*) malloc(sizeof(char) * msglen);
							unsigned char *enc = (char*) malloc(sizeof(char) * msglen);
							ksa(state, key, strlen(key));
							prga(state, out, msglen);

							for (j = 0; j < msglen; j++)
								enc[j] = (out[j] ^ buf[j + 5]);

							unsigned char final_message[BUFSIZE] = {'M', 'S', 'G', 'E', ' '};
							unsigned char bytecount[BUFSIZE];

							num2str(msglen, bytecount);
							int counter = 5;
							j = 0;

							while (bytecount[j] != '\0') {
								final_message[counter++] = bytecount[j];
								j++;
							}

							final_message[counter++] = '/';

							for (j = 0; j < msglen; j++)
								final_message[counter++] = enc[j];
							final_message[counter] = '\0';
							write(sockets[i], final_message, counter);
							free(enc);
							free(out);
						} else {
							// tagged message
							int j;
							unsigned char final_message[BUFSIZE] = {'M', 'S', 'G', 'E', ' '};
							unsigned char bytecount[BUFSIZE];
							int counter = 5;
							while (buf[counter] != ' ') {
								final_message[counter] = buf[counter];
								counter++;
							}

							final_message[counter++] = ' ';

							int msglen = strlen(buf) - counter;
							unsigned char state[256];
							unsigned char *out = (char*) malloc(sizeof(char) * msglen);
							unsigned char *enc = (char*) malloc(sizeof(char) * msglen);
							ksa(state, key, strlen(key));
							prga(state, out, msglen);

							for (j = 0; j < msglen; j++)
								enc[j] = (out[j] ^ buf[j + counter]);

							num2str(msglen, bytecount);

							j = 0;

							while (bytecount[j] != '\0') {
								final_message[counter++] = bytecount[j];
								j++;
							}

							final_message[counter++] = '/';

							for (j = 0; j < msglen; j++)
								final_message[counter++] = enc[j];
							final_message[counter] = '\0';

							write(sockets[i], final_message, counter);
							free(enc);
							free(out);
						}
					} else
					if ( write( sockets[i], buf, strlen(buf) ) < 0 ) {
						// Send to the server
						fprintf( stderr, "client write: %s\n", strerror(errno) );
						exit( -1 );
					}
				}
			}
		}
	}
	pthread_exit( NULL );
}

void *listen_to_server( void *ign ) {
	//
 	char	buf[BUFSIZE];
	char	command[BUFSIZE];
	int		cc;
	int 	i, j;
	int csock = *((int*) ign);
	char	bytecount[BUFSIZE];

	while (alive) {
		if ( (cc = read( csock, buf, BUFSIZE )) <= 0 ) {
            printf( "The server has gone.\n" );
            close(csock);
            break;
        } else {
            buf[cc] = '\0';
			memcpy(command, &buf[0], 4);
			command[4] = '\0';
			if (strcmp(command, "MSGE") == 0) {
				// message which is encrypted
				if (buf[5] != '#') {
					// message w/o tag
					j = 0;
					int slash = 5;
					while (buf[slash] != '/') {
						bytecount[j++] = buf[slash] - '0';
						slash++;
					}

					int msglen = strlen(buf) - (slash + 1);
					unsigned char state[256];
					unsigned char *out = (char*) malloc(sizeof(char) * msglen);
					unsigned char *enc = (char*) malloc(sizeof(char) * msglen);
					ksa(state, key, strlen(key));
					prga(state, out, msglen);

					for (j = 0; j < msglen; j++)
						enc[j] = (out[j] ^ buf[j + slash + 1]);

					enc[msglen] = '\0';
					printf("%s", enc);
					free(out);
					free(enc);
				} else {
					// w/ tag
					j = 5;
					do {
						printf("%c", buf[j++]);
					} while (buf[j] != ' ');

					printf(" ");

					int slash = j + 1; // where the bytecount starts
					j = 0;
					while (buf[slash] != '/') {
						bytecount[j++] = buf[slash] - '0';
						slash++;
					}

					int msglen = strlen(buf) - (slash + 1);
					unsigned char state[256];
					unsigned char *out = (char*) malloc(sizeof(char) * msglen);
					unsigned char *enc = (char*) malloc(sizeof(char) * msglen);
					ksa(state, key, strlen(key));
					prga(state, out, msglen);

					for (j = 0; j < msglen; j++)
						enc[j] = (out[j] ^ buf[j + slash + 1]);

					out[msglen] = '\0';
					printf("%s", enc);
					free(out);
					free(enc);
				}
			} else {
				// message which is not encrypted
				for (i = 4; i < strlen(buf); i++)
					printf("%c", buf[i]);
			}
			printf("\n");
		}
	}

	pthread_exit( NULL );
}

void num2str(int x, char *str) {
	int copyx = x, d = 1;
	while (copyx) {
		d *= 10;
		copyx /= 10;
	}
	int counter = 0;
	d /= 10;
	while (x && d > 0) {
		str[counter++] = (x / d) + 48;
		x %= d;
		d /= 10;
	}
	str[counter] = '\0';
}


// Key Scheduling Algorithm
// Input: state - the state used to generate the keystream
//        key - Key to use to initialize the state
//        len - length of key in bytes
void ksa(unsigned char* state, unsigned char* key, int len)
{
   int i,j=0,t;

   for (i=0; i < 256; ++i)
      *(state + i) = i;
   for (i=0; i < 256; ++i) {
      j = (j + *(state + i) + *(key + (i % len))) % 256;
      t = state[i];
      *(state + i) = *(state + j);
      *(state + j) = t;
   }
}

// Pseudo-Random Generator Algorithm
// Input: state - the state used to generate the keystream
//        out - Must be of at least "len" length
//        len - number of bytes to generate
void prga(unsigned char* state, unsigned char* out, int len)
{
   int i=0,j=0,x,t;

   for (x=0; x < len; ++x)  {
      i = (i + 1) % 256;
      j = (j + *(state + i)) % 256;
      t = *(state + i);
      *(state + i) = *(state + j);
      *(state + j) = t;
      *(out + x) = *(state + ((*(state + i) + *(state + j)) % 256));
   }
}
