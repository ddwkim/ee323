/*
 * transport.c
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file.
 *
 */

#include "transport.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mysock.h"
#include "stcp_api.h"

#define WIN_SIZE 3072

enum {
  CSTATE_ESTABLISHED,
  CSTATE_CLOSED,
  CSTATE_CLOSE_WAIT,
  CSTATE_CLOSING,
  CSTATE_LISTEN,
  CSTATE_SYN_SENT,
  CSTATE_SYN_RCVD,
  CSTATE_FIN_WAIT_1,
  CSTATE_FIN_WAIT_2,
  CSTATE_LAST_ACK,
  CSTATE_TIME_WAIT
}; /* obviously you should have more states */

/* this structure is global to a mysocket descriptor */
typedef struct {
  bool_t done; /* TRUE once connection is closed */

  int connection_state; /* state of the connection (established, etc.) */
  tcp_seq initial_sequence_num;

  /* any other connection-wide global variables go here */
  u_int16_t my_window;
  u_int16_t peer_window;

  tcp_seq my_seq;
  tcp_seq my_acked;
  tcp_seq peer_acked;

  char *send_buf;
  STCPHeader *send_header;
  u_int16_t send_buf_len;
  char *recv_buf;
  STCPHeader *recv_header;
  u_int16_t recv_buf_len;

} context_t;

static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active) {
  context_t *ctx;

  ctx = (context_t *)calloc(1, sizeof(context_t));
  assert(ctx);

  generate_initial_seq_num(ctx);
  ctx->my_seq = ctx->initial_sequence_num;

  ctx->recv_buf = (char *)calloc(1, STCP_MSS + sizeof(STCPHeader));
  ctx->recv_header = (STCPHeader *)(ctx->recv_buf);
  ctx->send_buf = (char *)calloc(1, STCP_MSS + sizeof(STCPHeader));
  ctx->send_header = (STCPHeader *)(ctx->send_buf);

  ctx->my_window = WIN_SIZE;
  ctx->peer_window = WIN_SIZE;

  /* XXX: you should send a SYN packet here if is_active, or wait for one
   * to arrive if !is_active.  after the handshake completes, unblock the
   * application with stcp_unblock_application(sd).  you may also use
   * this to communicate an error condition back to the application, e.g.
   * if connection fails; to do so, just set errno appropriately (e.g. to
   * ECONNREFUSED, etc.) before calling the function.
   */

  STCPHeader *syn_msg = (STCPHeader *)calloc(1, sizeof(STCPHeader));
  syn_msg->th_win = htons(WIN_SIZE);
  syn_msg->th_off = 5;
  ctx->connection_state = CSTATE_CLOSED;
  if (is_active) {
    // send SYN
    syn_msg->th_seq = htonl(ctx->my_seq);
    syn_msg->th_flags = TH_SYN;

    if (stcp_network_send(sd, syn_msg, sizeof(STCPHeader), NULL) < 0) {
      perror("stcp_network_send 1 active");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->connection_state = CSTATE_SYN_SENT;
    ctx->my_seq += 1;

    stcp_wait_for_event(sd, ANY_EVENT, NULL);

    if (stcp_network_recv(sd, syn_msg, sizeof(STCPHeader)) < 0) {
      perror("stcp_network_recv active");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    };

    if (syn_msg->th_flags != (TH_SYN | TH_ACK)) {
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->connection_state = CSTATE_ESTABLISHED;
    ctx->my_acked = ntohl(syn_msg->th_ack);

    // send ACK
    syn_msg->th_ack = htonl(ntohl(syn_msg->th_seq) + 1);
    syn_msg->th_seq = htonl(ctx->my_seq);
    syn_msg->th_flags = TH_ACK;
    if (stcp_network_send(sd, syn_msg, sizeof(STCPHeader), NULL) < 0) {
      perror("stcp_network_send 2 active");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->peer_acked = ntohl(syn_msg->th_ack);
  } else {
    // wait for SYN
    ctx->connection_state = CSTATE_LISTEN;
    if (stcp_network_recv(sd, syn_msg, sizeof(STCPHeader)) < 0) {
      perror("stcp_network_recv 1 passive");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    };
    if (syn_msg->th_flags != TH_SYN) {
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->connection_state = CSTATE_SYN_RCVD;
    // send SYN ACK
    syn_msg->th_seq = htonl(ctx->my_seq);
    syn_msg->th_ack = htonl(ntohl(syn_msg->th_seq) + 1);
    syn_msg->th_flags = TH_SYN | TH_ACK;
    if (stcp_network_send(sd, syn_msg, sizeof(STCPHeader), NULL) < 0) {
      perror("stcp_network_send passive");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->my_seq += 1;
    ctx->peer_acked = ntohl(syn_msg->th_ack);
    // wait for ACK
    if (stcp_network_recv(sd, syn_msg, sizeof(STCPHeader)) < 0) {
      perror("stcp_network_recv 2 passive");
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    // check ACK
    if (syn_msg->th_flags != TH_ACK) {
      errno = ECONNREFUSED;
      free(syn_msg);
      return;
    }
    ctx->my_acked = ntohl(syn_msg->th_ack);
    ctx->connection_state = CSTATE_ESTABLISHED;
  }
  free(syn_msg);

  stcp_unblock_application(sd);

  control_loop(sd, ctx);

  /* do any cleanup here */
  free(ctx->recv_buf);
  free(ctx->send_buf);
  free(ctx);
}

/* generate initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx) {
  assert(ctx);
  ctx->initial_sequence_num = 1;
}

/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx) {
  assert(ctx);
  ssize_t packet_size = 0;
  ssize_t payload_size = 0;

  while (!ctx->done) {
    unsigned int event;
    bool_t send_ready = FALSE;

    event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

    // initialize buffer
    memset(ctx->send_buf, 0, STCP_MSS + sizeof(STCPHeader));
    ctx->send_header->th_off = 5;
    ctx->send_header->th_flags |= TH_ACK;
    ctx->send_header->th_win = htons(WIN_SIZE);
    ctx->send_buf_len = sizeof(STCPHeader);

    if (event & NETWORK_DATA) {
      if ((packet_size = stcp_network_recv(
               sd, ctx->recv_buf, sizeof(STCPHeader) + STCP_MSS)) < 0) {
        perror("stcp_network_recv");
        return;
      }
      // ACK handling
      // ACK should be always set
      if (ctx->recv_header->th_flags & TH_ACK) {
        ctx->peer_window += ntohl(ctx->recv_header->th_ack) - ctx->my_acked;
        ctx->my_acked = ntohl(ctx->recv_header->th_ack);
      } else {
        // if ACK is not set, ignore the packet
        continue;
      }
      // waiting for the last ACK
      if ((ctx->connection_state == CSTATE_LAST_ACK) &&
          (ctx->my_acked == ctx->my_seq)) {
        ctx->done = TRUE;
        continue;
      }

      payload_size = packet_size - sizeof(STCPHeader);
      // data exists
      if (payload_size > 0) {
        // push data to application
        stcp_app_send(sd, ctx->recv_buf + sizeof(STCPHeader), payload_size);
        // fill ACK for data
        ctx->peer_acked += payload_size;
        send_ready = TRUE;
      }

      // FIN received
      if (ctx->recv_header->th_flags & TH_FIN) {
        stcp_fin_received(sd);
        // passive close, change state, wait for close call from above
        if (ctx->connection_state == CSTATE_ESTABLISHED) {
          ctx->connection_state = CSTATE_CLOSE_WAIT;
        }
        // active close
        if (ctx->connection_state == CSTATE_FIN_WAIT_1) {
          if (ctx->my_seq == ctx->my_acked) {
            // finish after sending ACK
            ctx->connection_state = CSTATE_CLOSED;
            ctx->done = TRUE;
          } else {
            // should wait for ACK
            ctx->connection_state = CSTATE_CLOSING;
          }
        }
        if (ctx->connection_state == CSTATE_FIN_WAIT_2) {
          // finish after sending ACK
          ctx->connection_state = CSTATE_CLOSED;
          ctx->done = TRUE;
        }

        // send ACK
        ctx->peer_acked += 1;
        send_ready = TRUE;
      } else if (ctx->connection_state == CSTATE_FIN_WAIT_1 &&
                 ctx->my_seq == ctx->my_acked) {
        ctx->connection_state = CSTATE_FIN_WAIT_2;
      } else if (ctx->connection_state == CSTATE_CLOSING &&
                 ctx->my_seq == ctx->my_acked) {
        ctx->connection_state = CSTATE_CLOSED;
        ctx->done = TRUE;
      }
    }
    if ((ctx->peer_window > 0) && (event & APP_DATA)) {
      if ((payload_size = stcp_app_recv(
               sd, ctx->send_buf + sizeof(STCPHeader),
               ctx->peer_window < STCP_MSS ? ctx->peer_window : STCP_MSS)) <
          0) {
        perror("stcp_app_recv");
        return;
      }
      ctx->send_buf_len += payload_size;
      send_ready = TRUE;
    }

    if (send_ready) {
      // assumes no partial send
      // if partial send occurs, return value should be recorded
      // fill headers
      ctx->send_header->th_seq = htonl(ctx->my_seq);
      ctx->send_header->th_ack = htonl(ctx->peer_acked);

      if (stcp_network_send(sd, ctx->send_buf, ctx->send_buf_len, NULL) < 0) {
        perror("stcp_network_send");
        return;
      }

      // adjust params
      ctx->my_seq += ctx->send_buf_len - sizeof(STCPHeader);
      ctx->peer_window -= ctx->send_buf_len - sizeof(STCPHeader);
    }

    // close requested by user
    if ((event & APP_CLOSE_REQUESTED) &&
        ((ctx->connection_state == CSTATE_ESTABLISHED) ||
         (ctx->connection_state == CSTATE_CLOSE_WAIT))) {
      // send FIN packet
      if (ctx->connection_state == CSTATE_CLOSE_WAIT)
        ctx->connection_state = CSTATE_LAST_ACK;
      if (ctx->connection_state == CSTATE_ESTABLISHED)
        ctx->connection_state = CSTATE_FIN_WAIT_1;
      ctx->send_header->th_seq = htonl(ctx->my_seq);
      ctx->send_header->th_ack = htonl(ctx->peer_acked);
      ctx->send_header->th_flags |= TH_FIN;

      // FIN consumes seq number
      if (stcp_network_send(sd, ctx->send_buf, sizeof(STCPHeader), NULL) < 0) {
        perror("stcp_network_send");
        return;
      }

      // adjust params
      ctx->my_seq += 1;
      ctx->peer_window -= 1;
    }
  }
}

/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 *
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format, ...) {
  va_list argptr;
  char buffer[1024];

  assert(format);
  va_start(argptr, format);
  vsnprintf(buffer, sizeof(buffer), format, argptr);
  va_end(argptr);
  fputs(buffer, stdout);
  fflush(stdout);
}
