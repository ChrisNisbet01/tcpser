#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/param.h>
#include <pthread.h>

#include "bridge.h"
#include "debug.h"
#include "init.h"
#include "ip.h"
#include "modem_core.h"
#include "phone_book.h"
#include "util.h"

const char MDM_BUSY[] = "BUSY\n";

#define MAX_MODEMS 16

static bool accept_pending = false;
static int modem_count;
static char *ip_addr = NULL;
static char all_busy[255];
static modem_config cfgs[MAX_MODEMS];
static int sSocket = -1;
struct uloop_fd sSocket_ufd;

static void
listening_socket_handler(struct uloop_fd * u, unsigned int events);

static void
accept_pending_change(bool const new_accept_pending)
{
  accept_pending = new_accept_pending;

  /* Use cfg[0] for the uloop sSocket fd handler. */
  if (sSocket >= 0 && !accept_pending)
  {
      sSocket_ufd.fd = sSocket;
      sSocket_ufd.cb = listening_socket_handler;
      uloop_fd_add(&sSocket_ufd, ULOOP_READ);
  }
  else
  {
    uloop_fd_delete(&sSocket_ufd);
  }
}
static void bridge_task_incoming_ipc_handler(struct uloop_fd * u, unsigned int events)
{
    modem_config * const cfg = container_of(u, modem_config, mp_ufd[0]);

    LOG_ENTER();

    if (u->eof || u->error)
    {
        uloop_fd_delete(u);
        goto done;
    }

    char buf[256];
    int const rc = read(cfg->mp[0][0], buf, sizeof(buf) -1);
    int const i = cfg - &cfg[0];
    if(rc > -1) {
      buf[rc] = '\0';
      LOG(LOG_DEBUG, "modem core #%d sent response '%s'", i, buf);
      accept_pending_change(false);
    }

done:
    LOG_EXIT();
}

static void
listening_socket_handler(struct uloop_fd * u, unsigned int events)
{
    LOG_ENTER();

    if (u->eof || u->error)
    {
        uloop_fd_delete(u);
        goto done;
    }

    if(!accept_pending) {
      int i;

      LOG(LOG_DEBUG, "Incoming connection pending");
      // first try for a modem that is listening.
      for(i = 0; i < modem_count; i++) {
        if(cfgs[i].s[0] != 0 && cfgs[i].is_off_hook == FALSE) {
          // send signal to pipe saying pick up...
          LOG(LOG_DEBUG, "Sending incoming connection to listening modem #%d", i);
          writePipe(cfgs[i].mp[1][1], MSG_CALLING);
          accept_pending_change(true);
          break;
        }
      }
      // now, send to any non-active modem.
      for(i = 0; i < modem_count; i++) {
        if(cfgs[i].is_off_hook == FALSE) {
          // send signal to pipe saying pick up...
          LOG(LOG_DEBUG, "Sending incoming connection to non-connected modem #%d", i);
          writePipe(cfgs[i].mp[1][1], MSG_CALLING);
          accept_pending_change(true);
          break;
        }
      }
      if(i == modem_count) {
        LOG(LOG_DEBUG, "No open modem to send to, send notice and close");
        // no connections.., accept and print error
        int const cSocket = ip_accept(sSocket);
        if(cSocket > -1) {
          // No tracing on this data output
          if(strlen(all_busy) < 1) {
            ip_write(cSocket, (unsigned char *)MDM_BUSY, strlen(MDM_BUSY));
          } else {
            writeFile(all_busy, cSocket);
          }
          close(cSocket);
        }
      }
    }

done:
    LOG_EXIT();
}

int main(int argc, char *argv[]) {
  log_init();

  LOG_ENTER();

  log_set_level(LOG_FATAL);

  mdm_init();

  pb_init();

  signal(SIGIO, SIG_IGN); /* Some Linux variant term on SIGIO by default */

  modem_count = init(argc, argv, cfgs, MAX_MODEMS, &ip_addr, all_busy, sizeof(all_busy));
  if (ip_addr != NULL) {
    sSocket = ip_init_server_conn(ip_addr, 6400);
    if(-1 == sSocket) {
      ELOG(LOG_FATAL, "Could not listen on %s", ip_addr);
      exit (-1);
    }
  }

  uloop_init();

  for(int i = 0; i < modem_count; i++) {
    LOG(LOG_INFO, "Creating modem #%d", i);
    LOG(LOG_DEBUG, "serial device %s ip %s", cfgs[i].dce_data.tty, ip_addr);
    if(-1 == pipe(cfgs[i].mp[0])) {
      ELOG(LOG_FATAL, "Bridge task incoming IPC pipe could not be created");
      exit(-1);
    }
    cfgs[i].mp_ufd[i].cb = bridge_task_incoming_ipc_handler;
    cfgs[i].mp_ufd[i].fd = cfgs[i].mp[0][0];
    uloop_fd_add(&cfgs[i].mp_ufd[0], ULOOP_READ);

    if(-1 == pipe(cfgs[i].mp[1])) {
      ELOG(LOG_FATAL, "Bridge task outgoing IPC pipe could not be created");
      exit(-1);
    }

    cfgs[i].line_data.sfd = sSocket;

    accept_pending_change(false);
    LOG(LOG_DEBUG, "serial device %s", cfgs[i].dce_data.tty);
    bridge_task(&cfgs[i]);
  }

  LOG(LOG_ALL, "Waiting for incoming connections and/or indicators");

  uloop_run();

  for (int i = 0; i < modem_count; i++)
  {
    bridge_task_close(&cfgs[i]);
  }

  LOG_EXIT();

  return EXIT_SUCCESS;
}
