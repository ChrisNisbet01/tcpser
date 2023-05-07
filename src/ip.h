#ifndef IP_H
#define IP_H

int ip_init(void);
int ip_init_server_conn(char *ip, int port);
int ip_connect(char const *ip);
int ip_accept(int sSocket);
int ip_disconnect(int fd);
int ip_write(int fd, unsigned char *data, int len);
int ip_read(int fd, unsigned char *data, int len);

#endif
