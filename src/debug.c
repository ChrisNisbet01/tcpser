#include <stdlib.h>       // for exit...
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#define DEBUG_VARS 1      // need this so we don't get extern defs
#include "debug.h"

int log_level = 0;
FILE *log_file;
int trace_flags = 0;
static char const * log_desc[LOG_LAST__] = {
    [LOG_FATAL] = "FATAL",
    [LOG_ERROR] = "ERROR",
    [LOG_WARN] = "WARN",
    [LOG_INFO] = "INFO",
    [LOG_DEBUG] = "DEBUG",
    [LOG_ENTER_EXIT] = "ENTER_EXIT",
    [LOG_ALL] = "DEBUG_X",
    [LOG_TRACE]=  "TRACE",
};

static char *get_trace_type(int type) {
  switch(type) {
  case TRACE_MODEM_IN:
    return "RS<-";
  case TRACE_MODEM_OUT:
    return "RS->";
  case TRACE_SERIAL_IN:
    return "SR<-";
  case TRACE_SERIAL_OUT:
    return "SR->";
  case TRACE_IP_IN:
    return "IP<-";
  case TRACE_IP_OUT:
    return "IP->";
  }
  return "NONE";
}

int log_init() {
  log_file = stderr;
  log_level = 0;
  trace_flags = 0;
  log_desc[LOG_FATAL] =           "FATAL";
  log_desc[LOG_ERROR] =           "ERROR";
  log_desc[LOG_WARN] =            "WARN";
  log_desc[LOG_INFO] =            "INFO";
  log_desc[LOG_DEBUG] =           "DEBUG";
  log_desc[LOG_ENTER_EXIT] =      "ENTER_EXIT";
  log_desc[LOG_ALL] =             "DEBUG_X";
  log_desc[LOG_TRACE]=            "TRACE";

  return 0;
}

void log_set_file(FILE *a) {
  log_file = a;
}

void log_set_level(int a) {
  log_level = a;
}

void log_set_trace_flags(int a) {
  trace_flags = a;
}

int log_get_trace_flags(void) {
  return trace_flags;
}

void log_trace(int type, unsigned char const *line, int len) {
  int i = 0;
  int ch;
  char data[64] = "\0";
  char *dptr = NULL;
  char text[17];

  if(len == 0)
    return;

  if((type & trace_flags) != 0) {
    text[16] = 0;
    for(i = 0; i < len; i++) {
      if((i % 16) == 0) {
        // beginning of line
        dptr = data;
        sprintf(dptr, "%4.4x|", i);
      }
      ch = line[i];
      sprintf(dptr + 5 + ((i % 16) * 3), "%2.2x", ch);
      if(ch > 31 && ch < 127) {
        text[i % 16] = ch;
      } else {
        text[i % 16] = '.';
      }
      if((i % 16) == 15) {
        log_start(LOG_TRACE);
        fprintf(log_file, "%s|%s|%s|", get_trace_type(type), data, text);
        log_end();
      } else {
        sprintf(dptr + 7 + ((i % 16) * 3), " ");
      }
    }
    i = i % 16;
    if(i > 0) {
      for(; i < 16; i++) {
        sprintf(dptr + 5 + ((i % 16) * 3), "  ");
        if((i % 16) != 15) {
          sprintf(dptr + 7 + ((i % 16) * 3), " ");
        }
        text[i % 16] = ' ';
      }
      log_start(LOG_TRACE);
      fprintf(log_file, "%s|%s|%s|", get_trace_type(type), data, text);
      log_end();
    }
  }
}

void log_start(int level) {
    fprintf(log_file, "%s: ", log_desc[level]);
}

void log_end(void) {
  fprintf(log_file, "\n");
  fflush(log_file);
}
