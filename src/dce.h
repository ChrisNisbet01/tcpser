#ifndef DCE_H
#define DCE_H 1

#include "serial_side.h"

#include <libubox/uloop.h>

#include <stdint.h>

#define DCE_CL_DSR 1
#define DCE_CL_DCD 2
#define DCE_CL_CTS 4
#define DCE_CL_DTR 8
#define DCE_CL_LE 16

static inline unsigned int gen_parity(uint_fast8_t const ch)
{
    return (0x6996 >> (((ch >> 4) ^ ch) & 0x0f)) & 1;
}

/* This is an impressive piece of code written by Chris Osborn (fozztexx@fozztexx.com)
 * that given a value where SPACE=0,ODD=1,EVEN=2,MARK=3 parity is present in p,
 * it will quickly add the correct parity to a 7 bit data value
 */
#ifndef apply_parity
#  define apply_parity(v, p) ((unsigned char)((v & 0x7f) | (((p >> gen_parity(v & 0x7f))) & 1) << 7))
#endif

enum {
  PARITY_SPACE_NONE = 0,
  PARITY_ODD,
  PARITY_EVEN,
  PARITY_MARK
};

typedef struct ip232 {
  bool dtr;
  bool dcd;
  bool iac;
  int fd;
} ip232;

typedef struct dce_config {
  char tty[256];
  int port_speed;
  int stopbits;
  int parity;
  int sSocket;
  struct uloop_fd sSocket_ufd;
  bool is_connected;
  bool is_ip232;
  struct uloop_fd ufd;
  ip232 ip232;
  serial_side_api_st *serial; /* Used when !is_ip232. */
} dce_config;

void dce_init_config(dce_config *cfg);
int dce_connect(dce_config *cfg);
void
dce_close(dce_config * cfg);

int dce_set_flow_control(dce_config *cfg, int opts);
int dce_set_parity_databits(dce_config *cfg, unsigned val);
int dce_set_speed(dce_config *cfg, unsigned speed);
int dce_set_control_lines(dce_config *cfg, int state);
int dce_get_control_lines(dce_config *cfg);
int dce_check_control_lines(dce_config *cfg);
int dce_write(dce_config *cfg, unsigned char *data, int len);
int dce_write_char_raw(dce_config *cfg, unsigned char data);
int dce_read(dce_config *cfg, unsigned char *data, int len);
int dce_read_char_raw(dce_config *cfg);
void dce_detect_parity(dce_config *cfg, unsigned char a, unsigned char t);
int dce_strip_parity(dce_config *cfg, unsigned char data);
int dce_get_parity(dce_config *cfg);
int dce_rx_fd(dce_config const * cfg);

//int dce_check_for_break(dce_config *cfg, char ch, int chars_left);

#endif
