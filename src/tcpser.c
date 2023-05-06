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

static int modem_count;
static char * ip_addr = NULL;
static char all_busy[255];
static modem_config cfgs[MAX_MODEMS];
static int sSocket = -1;
struct uloop_fd sSocket_ufd;

static void
listening_socket_handler(struct uloop_fd * u, unsigned int events);

static void
monitor_listening_socket(void)
{
    LOG_ENTER();

    LOG(LOG_DEBUG, "start listening for incoming IP connections");
    sSocket_ufd.fd = sSocket;
    sSocket_ufd.cb = listening_socket_handler;
    uloop_fd_add(&sSocket_ufd, ULOOP_READ);

    LOG_EXIT();
}

static void
listening_socket_handler(struct uloop_fd * u, unsigned int events)
{
    LOG_ENTER();

    if (u->eof || u->error)
    {
        LOG(LOG_DEBUG, "Listening socket had error. Exiting");
        uloop_done();
        goto done;
    }

    int i;

    LOG(LOG_DEBUG, "Incoming connection pending");
    // first try for a modem that is listening.
    for (i = 0; i < modem_count; i++)
    {
        if (cfgs[i].s[0] != 0 && !cfgs[i].line_data.is_connected)
        {
            LOG(LOG_DEBUG, "listening modem #%d accepting connection", i);
            accept_connection(&cfgs[i]);
            break;
        }
    }
    // now, send to any non-active modem that isn't already connected.
    for (i = 0; i < modem_count; i++)
    {
        if (!cfgs[i].line_data.is_connected)
        {
            LOG(LOG_DEBUG, "non-active modem #%d accepting connection", i);
            accept_connection(&cfgs[i]);
            break;
        }
    }
    if (i == modem_count)
    {
        LOG(LOG_DEBUG, "No open modem to send to, send notice and close");
        // no connections.., accept and print error
        int const cSocket = ip_accept(sSocket);

        if (cSocket > -1)
        {
            // No tracing on this data output
            if (strlen(all_busy) < 1)
            {
                ip_write(cSocket, (unsigned char *)MDM_BUSY, strlen(MDM_BUSY));
            }
            else
            {
                writeFile(all_busy, cSocket);
            }
            close(cSocket);
        }
    }

done:
    LOG_EXIT();
}

static void
cleanup(void)
{
    for (int i = 0; i < modem_count; i++)
    {
        bridge_close(&cfgs[i]);
    }
    if (sSocket >= 0)
    {
        uloop_fd_delete(&sSocket_ufd);
        close(sSocket);
        sSocket = -1;
    }
}

int main(int argc, char * argv[])
{
    log_init();

    LOG_ENTER();

    log_set_level(LOG_FATAL);

    mdm_init();

    pb_init();

    signal(SIGIO, SIG_IGN); /* Some Linux variant term on SIGIO by default */

    modem_count = init(argc, argv, cfgs, MAX_MODEMS, &ip_addr, all_busy, sizeof(all_busy));
    if (ip_addr != NULL)
    {
        sSocket = ip_init_server_conn(ip_addr, 6400);
        if (-1 == sSocket)
        {
            ELOG(LOG_FATAL, "Could not listen on %s", ip_addr);
            exit(-1);
        }
    }

    uloop_init();

    for (int i = 0; i < modem_count; i++)
    {
        LOG(LOG_INFO, "Creating modem #%d", i);
        LOG(LOG_DEBUG, "serial device %s ip %s", cfgs[i].dce_data.tty, ip_addr);

        cfgs[i].line_data.sfd = sSocket;

        bridge_init(&cfgs[i]);
    }

    monitor_listening_socket();

    LOG(LOG_ALL, "Waiting for incoming connections and/or indicators");

    uloop_run();

    cleanup();

    LOG_EXIT();

    return EXIT_SUCCESS;
}
