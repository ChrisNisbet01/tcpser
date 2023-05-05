#include <termios.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "serial.h"
#include "modem_core.h"
#include "ip232.h"      // needs modem_core.h
#include "dce.h"

void dce_init_config(dce_config *cfg) {
  cfg->parity = -1;  // parity not yet checked.
}

static int detect_parity (int charA, int charT) {
  int parity, eobits;

  parity = ((charA >> 6) & 2)  | (charT >> 7);
  eobits = gen_parity(charA & 0x7f) << 1 | gen_parity(charT & 0x7f);

  if((parity == 1) || (parity == 2)) {
    if(parity == eobits)
      return PARITY_EVEN;
    else
      return PARITY_ODD;
  } else
      return parity;
}

int dce_connect(dce_config *cfg) {
  int rc;

  LOG_ENTER();
  if (cfg->is_ip232) {
    rc = ip232_init_conn(cfg);
  } else {
    cfg->serial = serial_side_create(cfg->tty, cfg->port_speed, cfg->stopbits);
    if (cfg->serial == NULL) {
        rc = -1;
    } else {
        cfg->is_connected = TRUE;
        rc = 1;
    }
  }

  LOG_EXIT();
  return rc;
}

void
dce_close(dce_config * cfg)
{
  LOG_ENTER();

  if (cfg->is_ip232) {
    ip232_close_conn(cfg);
  } else {
    if (cfg->serial != NULL) {
      cfg->serial->methods->free(cfg->serial);
      cfg->serial = NULL;
    }
  }
  cfg->is_connected = false;

  LOG_EXIT();
}

int dce_set_flow_control(dce_config *cfg, int opts) {
  unsigned iflag = 0;
  unsigned cflag = 0;
  int rc = 0;

  LOG_ENTER();
  if(opts == 0) {
    LOG(LOG_ALL, "Setting NONE flow control");
  } else {
    if((opts & MDM_FC_RTS) != 0) {
      LOG(LOG_ALL, "Setting RTSCTS flow control");
      cflag |= CRTSCTS;
    }
    if((opts & MDM_FC_XON) != 0) {
      iflag |= (IXON | IXOFF);
      LOG(LOG_ALL, "Setting XON/XOFF flow control");
    }
  }

  if (cfg->is_ip232) {
    rc = ip232_set_flow_control(cfg, iflag, cflag);
  } else {
    rc = cfg->serial->methods->set_flow_control(cfg->serial, iflag, cflag);
  }

  LOG_EXIT()
  return rc;
}

int dce_set_parity_databits(dce_config *cfg, unsigned val) {
  unsigned cflag = 0;
  int rc = 0;

  LOG_ENTER();

  switch (val) {
    case MDM_PARITY_NONE_DATA_8:
      LOG(LOG_ALL, "Setting NONE parity, 8 data bits");
      cflag |= CS8;
      break;
    case MDM_PARITY_ODD_DATA_7:
      LOG(LOG_ALL, "Setting ODD parity, 7 data bits");
      cflag |= PARENB | PARODD | CS7;
      break;
    case MDM_PARITY_EVEN_DATA_7:
      LOG(LOG_ALL, "Setting EVEN parity, 7 data bits");
      cflag |= PARENB | CS7;
      break;
    case MDM_PARITY_NONE_DATA_7:
      LOG(LOG_ALL, "Setting NONE parity, 7 data bits");
      cflag |= CS7;
      break;
    case MDM_PARITY_ODD_DATA_8:
      LOG(LOG_ALL, "Setting ODD parity, 8 DATA");
      cflag |= PARENB | PARODD | CS8;
      break;
    case MDM_PARITY_EVEN_DATA_8:
      LOG(LOG_ALL, "Setting EVEN parity, 8 DATA");
      cflag |= PARENB | CS8;
      break;
  }

  if (cfg->is_ip232) {
  } else {
      rc = cfg->serial->methods->set_parity_databits(cfg->serial, cflag);
  }

  LOG_EXIT()
  return rc;
}

int dce_set_speed(dce_config *cfg, unsigned speed) {
  int rc = 0;

  LOG_ENTER();

  if (cfg->is_ip232) {
  } else {
    rc = cfg->serial->methods->set_speed(cfg->serial, speed);
  }

  LOG_EXIT()
  return rc;
}

int dce_set_control_lines(dce_config *cfg, int state) {
  int rc;

  LOG_ENTER();
//  if((state & DCE_CL_CTS) != 0) {
//    LOG(LOG_ALL, "Setting CTS pin high");
//  } else {
//    LOG(LOG_ALL, "Setting CTS pin low");
//  }
  if((state & DCE_CL_DCD) != 0) {
    LOG(LOG_ALL, "Setting DCD pin high");
  } else {
    LOG(LOG_ALL, "Setting DCD pin low");
  }

  if (cfg->is_ip232) {
    rc = ip232_set_control_lines(cfg, state);
  } else {
    rc = cfg->serial->methods->set_control_lines(cfg->serial, state);
  }

  LOG_EXIT();
  return rc;
}

int dce_get_control_lines(dce_config *cfg) {
  int state;

  if (cfg->is_ip232) {
    state = ip232_get_control_lines(cfg);
  } else {
    state = cfg->serial->methods->get_control_lines(cfg->serial);
  }
  return state;
}

int dce_check_control_lines(dce_config *cfg) {
  int state = 0;
  int new_state = 0;

  LOG_ENTER();
  state = dce_get_control_lines(cfg);
  new_state = state;
  while(new_state > -1 && state == new_state) {
    usleep(100000);
    new_state = dce_get_control_lines(cfg);
  }

  LOG_EXIT();
  return new_state;
}

int dce_write(dce_config *cfg, unsigned char data[], int len) {
  unsigned char *buf;
  int rc;
  int i;

  log_trace(TRACE_SERIAL_OUT, data, len);
  if (cfg->is_ip232) {
    return ip232_write(cfg, data, len);
  } else if(cfg->parity) {
    buf = malloc(len);  // TODO what if malloc fails?
    memcpy(buf, data, len);

    if(0 < cfg->parity) {
      for (i = 0; i < len; i++) {
        buf[i] = apply_parity(data[i], cfg->parity);
      }
    }
  } else {
    buf = data;
  }
  rc = cfg->serial->methods->write(cfg->serial, buf, len);
  if(cfg->parity)
    free(buf);
  return rc;
}

int dce_write_char_raw(dce_config *cfg, unsigned char data) {
  int rc;

  log_trace(TRACE_SERIAL_OUT, &data, 1);
  if (cfg->is_ip232) {
    rc = ip232_write(cfg, &data, 1);
  } else {
    rc = cfg->serial->methods->write(cfg->serial, &data, 1);
  }
  return rc;
}

int dce_read(dce_config *cfg, unsigned char data[], int len) {
  int res;
  int i;

  if (cfg->is_ip232) {
    res = ip232_read(cfg, data, len);
  } else {
    res = cfg->serial->methods->read(cfg->serial, data, len);
  }
  if(0 < res) {
    LOG(LOG_DEBUG, "Read %d bytes from serial port", res);
    if(0 < cfg->parity) {
      for (i = 0; i < res; i++) {
        data[i] &= 0x7f;  // strip parity from returned data
      }
    }
    log_trace(TRACE_SERIAL_IN, data, res);
  }
  return res;
}

int dce_read_char_raw(dce_config *cfg) {
  int res;
  unsigned char data[1];

  if (cfg->is_ip232) {
    res = ip232_read(cfg, data, 1);
  } else {
    res = cfg->serial->methods->read(cfg->serial, data, 1);
  }
  if(0 < res) {
    res = data[0];
    LOG(LOG_DEBUG, "Read 1 raw byte from serial port");
    log_trace(TRACE_SERIAL_IN, data, 1);
  }
  return res;
}

void dce_detect_parity(dce_config *cfg, unsigned char a, unsigned char t) {
  cfg->parity = detect_parity(a, t);
}

int dce_strip_parity(dce_config *cfg, unsigned char data) {
  return (cfg->parity ? data & 0x7f : data);
}

int dce_get_parity(dce_config *cfg) {
  return cfg->parity;
}

int dce_rx_fd(dce_config const * const cfg)
{
    if (cfg->is_ip232) {
      return cfg->ip232.fd;
    }

    return  cfg->serial->methods->get_rx_fd(cfg->serial);
}

