#include <stdio.h>

#include <sys/socket.h>   // for recv...
#include <unistd.h>       // for read...
#include <stdlib.h>       // for exit...
#include <sys/param.h>
#include <sys/time.h>
#include <pthread.h>

#include "util.h"
#include "debug.h"
#include "nvt.h"
#include "modem_core.h"
#include "ip.h"

#include "bridge.h"

static const char MDM_NO_ANSWER[] = "NO ANSWER\n";
static unsigned int const ring_interval_secs = 2;
#define TEXT_BUF_SIZE 1025

static void
do_all_checks(modem_config * cfg);

int accept_connection(modem_config *cfg) {
  LOG_ENTER();

  if(-1 != line_accept(&cfg->line_data)) {
    if(cfg->direct_conn == TRUE) {
      cfg->conn_type = MDM_CONN_INCOMING;
      mdm_off_hook(cfg);
    } else {
      cfg->rings = 0;
      mdm_send_ring(cfg);
    }
    // tell parent I got it.
    LOG(LOG_DEBUG, "Informing parent task that I am busy");
    writePipe(cfg->mp[0][1], MSG_BUSY);
  }
  LOG_EXIT();
  return 0;
}

int parse_ip_data(modem_config *cfg, unsigned char *data, int len) {
  // I'm going to cheat and assume it comes in chunks.
  int i = 0;
  unsigned char ch;
  unsigned char text[TEXT_BUF_SIZE];
  int text_len = 0;

  if(cfg->line_data.is_data_received == FALSE) {
    cfg->line_data.is_data_received = TRUE;
    if((data[0] == 0xff) || (data[0] == 0x1a)) {
      //line_write(cfg, (char*)TELNET_NOTICE,strlen(TELNET_NOTICE));
      LOG(LOG_INFO, "Detected telnet");
      // TODO add in telnet stuff
      cfg->line_data.is_telnet = TRUE;
      /* we need to let the other end know that our end will
       * handle the echo - otherwise "true" telnet clients like
       * those that come with Linux & Windows will echo characters
       * typed and you'll end up with doubled characters if the remote
       * host is echoing as well...
       * - gwb
       */
      send_nvt_command(cfg->line_data.fd, &cfg->line_data.nvt_data, NVT_WILL, NVT_OPT_ECHO);
    }
  }

  if(cfg->line_data.is_telnet == TRUE) {
    // once the serial port has seen a bit of data and telnet is active,
    // we can decide on binary transmit, not before
    if(cfg->is_binary_negotiated == FALSE) {
      if(dce_get_parity(&cfg->dce_data)) {
        // send explicit notice this connection is not 8 bit clean
        send_nvt_command(cfg->line_data.fd,
                         &cfg->line_data.nvt_data,
                         NVT_WONT,
                         NVT_OPT_TRANSMIT_BINARY
                        );
        send_nvt_command(cfg->line_data.fd,
                         &cfg->line_data.nvt_data,
                         NVT_DONT,
                         NVT_OPT_TRANSMIT_BINARY
                        );
      } else {
        send_nvt_command(cfg->line_data.fd,
                         &cfg->line_data.nvt_data,
                         NVT_WILL,
                         NVT_OPT_TRANSMIT_BINARY
                        );
        send_nvt_command(cfg->line_data.fd,
                         &cfg->line_data.nvt_data,
                         NVT_DO,
                         NVT_OPT_TRANSMIT_BINARY
                        );
      }
      cfg->is_binary_negotiated = TRUE;
    }
    while(i < len) {
      ch = data[i];
      if(NVT_IAC == ch) {
        // what if we roll off the end?
        ch = data[i + 1];
        switch(ch) {
          case NVT_WILL:
          case NVT_DO:
          case NVT_WONT:
          case NVT_DONT:
            /// again, overflow issues...
            LOG(LOG_INFO, "Parsing nvt command");
            parse_nvt_command(&cfg->dce_data,
                              cfg->line_data.fd,
                              &cfg->line_data.nvt_data,
                              ch,
                              data[i + 2]
                             );
            i += 3;
            break;
          case NVT_SB:      // sub negotiation
            // again, overflow...
            i += parse_nvt_subcommand(&cfg->dce_data,
                                      cfg->line_data.fd,
                                      &cfg->line_data.nvt_data,
                                      data + i,
                                      len - i
                                     );
            break;
          case NVT_IAC:
            if (cfg->line_data.nvt_data.binary_recv)
              text[text_len++] = NVT_IAC;
              // fall through to skip this sequence
          default:
            // ignore...
            i += 2;
        }
      } else {
        text[text_len++] = data[i++];
      }
      if(text_len == sizeof(text) - 1) {
        text[text_len] = '\0';
        // write to serial...
        mdm_write(cfg, text, text_len);
        text_len = 0;
      }
    }
    if(text_len > 0) {
      text[text_len] = '\0';
      // write to serial...
      mdm_write(cfg, text, text_len);
    }
  } else {
    mdm_write(cfg, data, len);
  }
  return 0;
}

static int action_pending = FALSE;

static void line_data_cb(struct uloop_fd * u, unsigned int events);

static void
action_pending_change(modem_config * cfg, bool new_action_pending)
{
    action_pending = new_action_pending;

    if (!action_pending
        && cfg->conn_type != MDM_CONN_NONE
        && cfg->is_cmd_mode == FALSE
        && cfg->line_data.fd > -1
        && cfg->line_data.is_connected == TRUE
        )
    {
        cfg->line_data.ufd.cb = line_data_cb;
        cfg->line_data.ufd.fd = cfg->line_data.fd;
        uloop_fd_add(&cfg->line_data.ufd, ULOOP_READ);
    }
    else
    {
        uloop_fd_delete(&cfg->line_data.ufd);
    }
}

static void line_data_cb(struct uloop_fd * u, unsigned int events)
{
    modem_config * const cfg = container_of(u, modem_config, line_data.ufd);

    LOG_ENTER();

    if (u->eof || u->error)
    {
        LOG(LOG_INFO, "No socket data read, assume closed peer");
        writePipe(cfg->cp[0][1], MSG_DISCONNECT);
        action_pending_change(cfg, true);
        uloop_fd_delete(u);
        goto done;
    }

    if (cfg->line_data.is_connected == TRUE) {
      unsigned char buf[256];

      LOG(LOG_DEBUG, "Data available on socket");
      int const res = line_read(&cfg->line_data, buf, sizeof(buf) - 1);
      if(res < 0) {
        LOG(LOG_INFO, "No socket data read, assume closed peer");
        writePipe(cfg->cp[0][1], MSG_DISCONNECT);
        action_pending_change(cfg, true);
      } else {
        LOG(LOG_DEBUG, "Read %d bytes from socket", res);
        writePipe(cfg->cp[0][1], MSG_NONE); /* reset inactivity timer */
        buf[res] = '\0';
        parse_ip_data(cfg, buf, res);
      }
    }

done:
    LOG_EXIT();
}

static void
cp1_read_handler_cb(struct uloop_fd * u, unsigned int events)
{
    LOG_ENTER();
    modem_config * const cfg = container_of(u, modem_config, cp_ufd[1]);
    unsigned char buf[256];
    int const res = readPipe(cfg->cp[1][0], buf, sizeof(buf) - 1);
    (void)res;
    LOG(LOG_DEBUG, "IP thread notified");
    action_pending_change(cfg, false);
    LOG_EXIT();
}

static void
ip_thread(modem_config* cfg)
{
  LOG_ENTER();
  action_pending_change(cfg, action_pending);
  cfg->cp_ufd[1].cb = cp1_read_handler_cb;
  cfg->cp_ufd[1].fd = cfg->cp[1][0];
  uloop_fd_add(&cfg->cp_ufd[1], ULOOP_READ);
  LOG_EXIT();
}

static void
ctrl_thread(modem_config * cfg)
{
    control_data_st * const control_data = &cfg->control_data;
    int const new_status = dce_get_control_lines(&cfg->dce_data);

    if (new_status > -1 && control_data->status != new_status)
    {
        LOG(LOG_DEBUG, "Control Line Change");
        if ((new_status & DCE_CL_DTR) != (control_data->status & DCE_CL_DTR))
        {
            if ((new_status & DCE_CL_DTR))
            {
                LOG(LOG_INFO, "DTR has gone high");
                writePipe(cfg->wp[0][1], MSG_DTR_UP);
            }
            else
            {
                LOG(LOG_INFO, "DTR has gone low");
                writePipe(cfg->wp[0][1], MSG_DTR_DOWN);
            }
        }
        if ((new_status & DCE_CL_LE) != (control_data->status & DCE_CL_LE))
        {
            if ((new_status & DCE_CL_LE))
            {
                LOG(LOG_INFO, "Link has come up");
                writePipe(cfg->wp[0][1], MSG_LE_UP);
            }
            else
            {
                LOG(LOG_INFO, "Link has gone down");
                writePipe(cfg->wp[0][1], MSG_LE_DOWN);
            }
        }
    }

    control_data->status = new_status;
    if (control_data->status < 0)
    {
        /* Can't obtain status, so exit the program. */
        uloop_done();
    }
}

static void
ctrl_thread_timer_cb(struct uloop_timeout * const t)
{
    modem_config * const cfg = container_of(t, modem_config, ctrl_thread_timer);

    ctrl_thread(cfg);
    t->cb = ctrl_thread_timer_cb;
    uloop_timeout_set(t, 100);
}

static void
check_connection_type_change(modem_config * const cfg)
{
  bridge_data_st * const bridge_data = &cfg->bridge_data;

    if(bridge_data->last_conn_type != cfg->conn_type) {
      LOG(LOG_ALL, "Connection status change, handling");
      writePipe(cfg->cp[1][1], MSG_NOTIFY);
      if(cfg->conn_type == MDM_CONN_OUTGOING) {
        if(strlen(cfg->local_connect) > 0) {
          writeFile(cfg->local_connect, cfg->line_data.fd);
        }
        if(strlen(cfg->remote_connect) > 0) {
          writeFile(cfg->remote_connect, cfg->line_data.fd);
        }
      } else if(cfg->conn_type == MDM_CONN_INCOMING) {
        if(strlen(cfg->local_answer) > 0) {
          writeFile(cfg->local_answer, cfg->line_data.fd);
        }
        if(strlen(cfg->remote_answer) > 0) {
          writeFile(cfg->remote_answer, cfg->line_data.fd);
        }
      }
      bridge_data->last_conn_type = cfg->conn_type;
    }
}

static void
check_command_mode_change(modem_config * const cfg)
{
    bridge_data_st * const bridge_data = &cfg->bridge_data;

    if(bridge_data->last_cmd_mode != cfg->is_cmd_mode) {
      writePipe(cfg->cp[1][1], MSG_NOTIFY);
      bridge_data->last_cmd_mode = cfg->is_cmd_mode;
    }
}

static void
dce_data_cb(struct uloop_fd * u, unsigned int const events)
{
  dce_config * const dce_data = container_of(u, dce_config, ufd);
  modem_config * const cfg = container_of(dce_data, modem_config, dce_data);
  LOG_ENTER();

  if (u->eof || u->error)
  {
      uloop_fd_delete(u);
      /* TODO: what? End program? (uloop_done();) */
      goto done;
  }

  LOG(LOG_DEBUG, "Data available on serial port");
  unsigned char buf[256];
  int const res = mdm_read(cfg, buf, sizeof(buf));
  if(res > 0) {
    if(cfg->conn_type == MDM_CONN_NONE
       && !cfg->is_cmd_mode
       && cfg->is_off_hook) {
      // this handles the case where atdt/ata goes off hook, but no
      // connection
      mdm_disconnect(cfg, FALSE);
    } else {
      mdm_parse_data(cfg, buf, res);
    }
  }

done:
  do_all_checks(cfg);
  LOG_EXIT();
}

static void
check_read_dce_data(modem_config * const cfg)
{
  LOG_ENTER();

  if(cfg->dce_data.is_connected) {
    cfg->dce_data.ufd.fd = dce_rx_fd(&cfg->dce_data);
    cfg->dce_data.ufd.cb = dce_data_cb;
    uloop_fd_add(&cfg->dce_data.ufd, ULOOP_READ);
  }
  else
  {
    uloop_fd_delete(&cfg->dce_data.ufd);
  }

  LOG_EXIT();
}

static void
handle_ring_timeout(modem_config * const cfg)
{
  if(cfg->is_cmd_mode && cfg->conn_type == MDM_CONN_NONE && cfg->line_data.is_connected)
  {
    if(cfg->s[0] == 0 && cfg->rings == 10) {
      // not going to answer, send some data back to IP and disconnect.
      if(strlen(cfg->no_answer) == 0) {
        line_write(&cfg->line_data, (unsigned char *)MDM_NO_ANSWER, strlen(MDM_NO_ANSWER));
      } else {
        writeFile(cfg->no_answer, cfg->line_data.fd);
      }
      cfg->is_ringing = FALSE;
      mdm_disconnect(cfg, FALSE);
    }
    else
    {
      mdm_send_ring(cfg);
    }
  }
  do_all_checks(cfg);
}

static void
handle_ring_timeout_cb(struct uloop_timeout * const t)
{
  modem_config * const cfg = container_of(t, modem_config, ring_timer);
  handle_ring_timeout(cfg);
}

static void
check_start_ring_timer(modem_config * const cfg)
{
  struct uloop_timeout * const t = &cfg->ring_timer;

  if(cfg->is_cmd_mode
     && cfg->conn_type == MDM_CONN_NONE
     && cfg->line_data.is_connected
     && cfg->is_ringing)
  {
          LOG(LOG_ALL, "Setting timer for rings");
          t->cb = handle_ring_timeout_cb;
          uloop_timeout_set(t, ring_interval_secs * 1000);
  }
  else
  {
    uloop_timeout_cancel(t);
  }
}

static void
handle_other_timeout(modem_config * const cfg)
{
  if (!cfg->is_cmd_mode || cfg->conn_type != MDM_CONN_NONE || !cfg->line_data.is_connected)
  {
    mdm_handle_timeout(cfg);
  }
  do_all_checks(cfg);
}

static void
handle_other_timeout_cb(struct uloop_timeout * const t)
{
  modem_config * const cfg = container_of(t, modem_config, other_timer);
  handle_other_timeout(cfg);
}

static void
check_start_other_timer(modem_config * const cfg)
{
    struct uloop_timeout * const t = &cfg->other_timer;
    int timeout_msecs = 0;

    if (cfg->is_cmd_mode == FALSE)
    {
        if (cfg->pre_break_delay == FALSE || cfg->break_len == 3)
        {
            LOG(LOG_ALL, "Setting timer for break delay");
            timeout_msecs = cfg->s[S_REG_GUARD_TIME] * 20;
        }
        else if (cfg->pre_break_delay == TRUE && cfg->break_len > 0)
        {
            LOG(LOG_ALL, "Setting timer for inter-break character delay");
            timeout_msecs = 1000;
        }
        else if (cfg->s[30] != 0)
        {
            LOG(LOG_ALL, "Setting timer for inactivity delay");
            timeout_msecs = cfg->s[S_REG_INACTIVITY_TIME] * 10 * 1000;
        }
    }
    if (timeout_msecs > 0)
    {
        t->cb = handle_other_timeout_cb;
        uloop_timeout_set(t, timeout_msecs);
    }
    else
    {
        uloop_timeout_cancel(t);
    }
}

static void
wp0_read_handler_cb(struct uloop_fd * const u, unsigned int const events)
{
  modem_config * const cfg = container_of(u, modem_config, wp_ufd[0]);

  LOG_ENTER();

  if (u->eof || u->error)
  {
      uloop_fd_delete(u);
      /* TODO: what? End program? (uloop_done();) */
      goto done;
  }

  unsigned char buf[256];
  int const res = readPipe(cfg->wp[0][0], buf, sizeof(buf));
  LOG(LOG_DEBUG, "Received %s from control line watch task", buf);
  for(int i = 0; i < res ; i++) {
    switch (buf[0]) {
      case MSG_DTR_DOWN:
        // DTR drop, close any active connection and put
        // in cmd_mode
        mdm_disconnect(cfg, FALSE);
        break;
      default:
        break;
    }
  }

done:
  do_all_checks(cfg);
  LOG_EXIT();
}

static void
cp0_read_handler_cb(struct uloop_fd * const u, unsigned int const events)
{
  modem_config * const cfg = container_of(u, modem_config, cp_ufd[0]);

  LOG_ENTER();

  if (u->eof || u->error)
  {
      uloop_fd_delete(u);
      /* TODO: what? End program? (uloop_done();) */
      goto done;
  }

  unsigned char buf[256];
  int const res = readPipe(cfg->cp[0][0], buf, sizeof(buf));
  (void)res;
  LOG(LOG_DEBUG, "Received %c from ip thread", buf[0]);
  switch (buf[0]) {
    case MSG_DISCONNECT:
      if(cfg->direct_conn == TRUE) {
        // what should we do here...
        LOG(LOG_ERROR, "Direct Connection Link broken, disconnecting and awaiting new direct connection");
        mdm_disconnect(cfg, TRUE);
      } else {
        mdm_disconnect(cfg, FALSE);
      }
      break;
  }

done:
  do_all_checks(cfg);
  LOG_EXIT();
}

static void
mp1_read_handler_cb(struct uloop_fd * const u, unsigned int const events)
{
  modem_config * const cfg = container_of(u, modem_config, mp_ufd[1]);

  LOG_ENTER();

  if (u->eof || u->error)
  {
      uloop_fd_delete(u);
      /* TODO: what? End program? (uloop_done();) */
      goto done;
  }

  LOG(LOG_DEBUG, "Data available on incoming IPC pipe");
  unsigned char buf[256];
  int const res = readPipe(cfg->mp[1][0], buf, sizeof(buf));
  (void)res;
  switch (buf[0]) {
    case MSG_CALLING:       // accept connection.
      accept_connection(cfg);
      break;
  }

done:
  do_all_checks(cfg);
  LOG_EXIT();
}

static void
do_all_checks(modem_config * const cfg)
{
  check_connection_type_change(cfg);
  check_command_mode_change(cfg);
  check_read_dce_data(cfg);
  check_start_ring_timer(cfg);
  check_start_other_timer(cfg);
  LOG(LOG_ALL, "Waiting for modem/control line/timer/socket activity");
  LOG(
      LOG_ALL,
      "CMD:%d, DCE:%d, LINE:%d, TYPE:%d, HOOK:%d",
      cfg->is_cmd_mode,
      cfg->dce_data.is_connected,
      cfg->line_data.is_connected,
      cfg->conn_type,
      cfg->is_off_hook
  );
}

void bridge_init(modem_config * const cfg)
{
  bridge_data_st * const bridge_data = &cfg->bridge_data;

  LOG_ENTER();

  bridge_data->last_cmd_mode = cfg->is_cmd_mode;
  bridge_data->action_pending = false;

  cfg->mp_ufd[1].cb = mp1_read_handler_cb;
  cfg->mp_ufd[1].fd = cfg->mp[1][0];
  uloop_fd_add(&cfg->mp_ufd[1], ULOOP_READ);

  if(-1 == pipe(cfg->wp[0])) {
    ELOG(LOG_FATAL, "Control line watch task incoming IPC pipe could not be created");
    exit(-1);
  }
  cfg->wp_ufd[0].cb = wp0_read_handler_cb;
  cfg->wp_ufd[0].fd = cfg->wp[0][0];
  uloop_fd_add(&cfg->wp_ufd[0], ULOOP_READ);

  if(-1 == pipe(cfg->cp[0])) {
    ELOG(LOG_FATAL, "IP thread incoming IPC pipe could not be created");
    exit(-1);
  }
  cfg->cp_ufd[0].cb = cp0_read_handler_cb;
  cfg->cp_ufd[0].fd = cfg->cp[0][0];
  uloop_fd_add(&cfg->cp_ufd[0], ULOOP_READ);

  if(-1 == pipe(cfg->cp[1])) {
    ELOG(LOG_FATAL, "IP thread outgoing IPC pipe could not be created");
    exit(-1);
  }
  if(dce_connect(&cfg->dce_data) < 0) {
    ELOG(LOG_FATAL, "Could not open serial port %s", cfg->dce_data.tty);
    exit(-1);
  }

  ctrl_thread_timer_cb(&cfg->ctrl_thread_timer);
  ip_thread(cfg);

  mdm_set_control_lines(cfg);
  bridge_data->last_conn_type = cfg->conn_type;
  cfg->allow_transmit = FALSE;
  // call some functions behind the scenes
  if(cfg->cur_line_idx) {
    mdm_parse_cmd(cfg);
  }
  if (cfg->direct_conn == TRUE) {
    if(strlen((char *)cfg->direct_conn_num) > 0 &&
       cfg->direct_conn_num[0] != ':') {
        // we have a direct number to connect to.
      strncpy(cfg->dialno, cfg->direct_conn_num, sizeof(cfg->dialno));
      if(0 != line_connect(&cfg->line_data, cfg->dialno)) {
        LOG(LOG_FATAL, "Cannot connect to Direct line address!");
        // probably should exit...
        exit(-1);
      } else {
        cfg->conn_type = MDM_CONN_OUTGOING;
      }
    }
  }
  cfg->allow_transmit = TRUE;
  do_all_checks(cfg);

  LOG_EXIT();
}

void
bridge_close(modem_config * const cfg)
{
  LOG_ENTER();
  dce_close(&cfg->dce_data);
  LOG_EXIT();
}

