#ifndef H_FTP
#define H_FTP

const char * ftpStrerror(int ftpErrno);

#define FTPERR_BAD_SERVER_RESPONSE   -1
#define FTPERR_SERVER_IO_ERROR       -2
#define FTPERR_SERVER_TIMEOUT        -3
#define FTPERR_BAD_HOST_ADDR         -4
#define FTPERR_BAD_HOSTNAME          -5
#define FTPERR_FAILED_CONNECT        -6
#define FTPERR_FILE_IO_ERROR         -7
#define FTPERR_PASSIVE_ERROR         -8
#define FTPERR_FAILED_DATA_CONNECT   -9
#define FTPERR_FILE_NOT_FOUND        -10
#define FTPERR_UNKNOWN               -100

int httpOpen(urlinfo *u);

int ftpOpen(urlinfo *u);
int ftpGetFile(int sock, char * remotename, FD_t dest);
int ftpGetFileDesc(int sock, char * remotename);
int ftpGetFileDone(int sock);
void ftpClose(int sock);

#endif
