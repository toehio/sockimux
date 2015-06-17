#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>

// TODO: specify input (STDIO, socket or TCP port) as argument

#define DEBUG_LEVEL 0
#define BUFF_SIZE 1024 * 8
#define MAX_CLIENTS 24

typedef struct imux_header {
    unsigned int seq_no;
    short bytes;
} imux_header;

typedef struct client_conn {
    int sock;
    struct sockaddr_un remote;
    short open;
    imux_header queued;
    short has_queued;
} client_conn;

client_conn clients[MAX_CLIENTS];
int listen_sock;

char *buff;

unsigned int send_seq_no = 1; // next seq_no to send
unsigned int recv_seq_no = 0; // last seq_no received
unsigned int bytes_sent = 0;
unsigned int bytes_recvd = 0;

int bytes_available(int fd) {
    int count;
    ioctl(fd, FIONREAD, &count);
    return count;
}

int is_open(int fd) {
    int n = 0;
    ioctl(fd, FIONREAD, &n);
    return n != 0;
}

void usage(char *argv[]) {
    fprintf(stdout, "Usage: %s path_to_unix_pipe\n\n", argv[0]);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Invalid arguments: specify path to unix socket.\n");
        usage(argv);
        exit(1);
    }

    if (argv[1] == "-h" || argv[1] == "--help") {
        usage(argv);
        exit(0);
    }

    buff = (char *)malloc(BUFF_SIZE);

    int len, max_fd, activity, open_clients, i, sendto = 0;
    int stdin_closed = 0;
    size_t n;
    fd_set read_set;
    struct sockaddr_un local;
    imux_header *h_send;
    imux_header h_recv;
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].open = 0;
        clients[i].has_queued = 0;
    }

    if ((listen_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("Error: socket()");
        exit(1);
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, argv[1]);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(listen_sock, (struct sockaddr *)&local, len) == -1) {
        perror("Error: bind()");
        exit(1);
    }

    if (listen(listen_sock, 5) == -1) {
        perror("Error: listen()");
        exit(1);
    }
    if (DEBUG_LEVEL > 1) fprintf(stderr, "Listenning...\n");

    while (1) {
        if (DEBUG_LEVEL > 1) fprintf(stderr, "\n================\n");
        FD_ZERO(&read_set);
        FD_SET(listen_sock, &read_set);
        max_fd = listen_sock;
        open_clients = 0;

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].open == 0) continue;
            FD_SET(clients[i].sock, &read_set);
            if (clients[i].sock > max_fd) max_fd = clients[i].sock;
            open_clients++;
        }
        if (open_clients > 0 && !stdin_closed)
            FD_SET(STDIN_FILENO, &read_set);

        activity = select(max_fd + 1, &read_set, NULL, NULL, NULL);
        if (activity < 0) { //&& (errno!=EINTR))
            perror("Error: select()");
            exit(1);
        }

        if (FD_ISSET(listen_sock, &read_set)) { // Accept incomming connection
            for (i = 0; i < MAX_CLIENTS; i++)
                if (clients[i].open == 0) break;
            len = sizeof(clients[i].remote);
            if ((clients[i].sock = accept(listen_sock, (struct sockaddr *)&clients[i].remote, &len)) == -1) {
                perror("Error: accept()");
                exit(1);
            }
            clients[i].open = 1;
            if (DEBUG_LEVEL > 1) fprintf(stderr, "Client connected.\n");
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock == -1 || !FD_ISSET(clients[i].sock, &read_set)) continue;
            if (!is_open(clients[i].sock)) { // this has to be checked before reading all data
                open_clients--;
                //clients[i].open = 0;
            }
            if (!clients[i].has_queued) {
                if (!bytes_available(clients[i].sock))
                    return 0;
                if((n = recv(clients[i].sock, &clients[i].queued, sizeof(imux_header), 0)) == -1) {
                    perror("Error: read header:  recv()");
                    exit(4);
                }
                assert(n == sizeof(imux_header));
                clients[i].has_queued = 1;
            }
            if (clients[i].has_queued && clients[i].queued.seq_no == recv_seq_no + 1) {
                if((n = recv(clients[i].sock, buff, clients[i].queued.bytes, 0)) == -1) {
                    perror("Error: OO data: recv()");
                    exit(4);
                }
                assert(n == clients[i].queued.bytes);
                if (write(STDOUT_FILENO, buff, n) == -1) {
                    perror("Error: write() STDOUT");
                    exit(4);
                }
                bytes_recvd += n;
                recv_seq_no++;
                clients[i].has_queued = 0;
            }
        }

        if (!FD_ISSET(STDIN_FILENO, &read_set)) continue;
        if (!is_open(STDIN_FILENO) && bytes_sent > 0) break;
        if (!bytes_available(STDIN_FILENO) && bytes_sent == 0) {
            stdin_closed = 1;
            continue;
        }
        if (open_clients == 0) continue;

        // Choose next client to send on (round robin)
        sendto++;
        for (i = 0; i < MAX_CLIENTS; i++)
            if (clients[(sendto + i) % MAX_CLIENTS].open) break;
        if (i == MAX_CLIENTS) continue; // no available clients
        sendto = (sendto + i) % MAX_CLIENTS;

        if((n = read(STDIN_FILENO, buff + sizeof(imux_header), BUFF_SIZE - sizeof(imux_header))) == 0) {
            perror("Error: read() STDIN");
            exit(1);
        }
        if (n == 0) break;
        h_send = (imux_header *)buff;
        h_send->seq_no = send_seq_no;
        h_send->bytes = n;
        if (send(clients[sendto].sock, buff, sizeof(imux_header) + n, 0) == -1) {
            perror("Error: send()");
            exit(1);
        }
        bytes_sent += n;
        send_seq_no++;
        if (DEBUG_LEVEL > 1) fprintf(stderr, "Send: to: %d, seq_no: %d, bytes: %d, send_seq_no: %d\n", sendto, h_send->seq_no, h_send->bytes, send_seq_no);
    }

    // TODO: is there a better way than just waiting?
    // This is done to ensure the socket is open for long enough for the other
    // end to receive.
    if (bytes_sent > 0)
        sleep(1);

    close(listen_sock);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].open == 0) continue;
        close(clients[i].sock);
    }

    return 0;
}
