#ifndef SERIAL_H
#define SERIAL_H 1

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

int ser_get_bps_const(int speed);
int ser_init_conn(char const *tty, int speed, int stopbits);
int ser_set_flow_control(int fd, unsigned iflag, unsigned cflag);
int ser_set_parity_databits(int fd, unsigned cflag);
int ser_set_speed(int fd, unsigned speed);
int ser_get_control_lines(int fd);
int ser_set_control_lines(int fd, int state);
int ser_write(int fd, unsigned char const *data,int len);
int ser_read(int fd, unsigned char *data, int len);

#endif
