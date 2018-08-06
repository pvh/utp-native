#include <node_api.h>
#include <napi-macros.h>
#include <uv.h>
#include <stdlib.h>
#include <string.h>
#include "./deps/libutp/utp.h"

#define UTP_NAPI_TIMEOUT_INTERVAL 500

#define UTP_NAPI_THROW(err) \
  { \
    napi_throw_error(env, uv_err_name(err), uv_strerror(err)); \
    return NULL; \
  }

#define UTP_NAPI_CALLBACK(fn, src) \
  napi_env env = self->env; \
  napi_handle_scope scope; \
  napi_open_handle_scope(env, &scope); \
  napi_value ctx; \
  napi_get_reference_value(env, self->ctx, &ctx); \
  napi_value callback; \
  napi_get_reference_value(env, fn, &callback); \
  src \
  napi_close_handle_scope(env, scope);

#define UTP_NAPI_BUFFER_ALLOC(self, ret, nread) \
  NAPI_BUFFER(buf, ret); \
  if (buf_len == 0) { \
    size_t size = nread <= 0 ? 0 : nread; \
    self->buf.base += size; \
    self->buf.len -= size; \
  } else { \
    self->buf.base = buf; \
    self->buf.len = buf_len; \
  }

typedef struct {
  uv_udp_t handle;
  utp_context *utp;
  int accept_connections;
  uv_timer_t timer;
  napi_env env;
  napi_ref ctx;
  uv_buf_t buf;
  napi_ref on_message;
  napi_ref on_send;
  napi_ref on_connection;
  napi_ref on_close;
} utp_napi_t;

typedef struct {
  uv_udp_send_t req;
  napi_ref ctx;
} utp_napi_send_request_t;

typedef struct {
  uint32_t min_packet_size;
  uint32_t packet_size;
  utp_socket *socket;
  napi_env env;
  napi_ref ctx;
  uv_buf_t buf;
  napi_ref on_read;
} utp_napi_connection_t;

static void
utp_napi_parse_address (struct sockaddr *name, char *ip, int *port) {
  struct sockaddr_in *name_in = (struct sockaddr_in *) name;
  *port = ntohs(name_in->sin_port);
  uv_ip4_name(name_in, ip, 17);
}

static void
on_uv_interval (uv_timer_t *req) {
  utp_napi_t *self = (utp_napi_t *) req->data;
  utp_check_timeouts(self->utp);
}

static void
on_uv_alloc (uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  utp_napi_t *self = (utp_napi_t *) handle->data;
  *buf = self->buf;
}

static void
on_uv_read (uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
  utp_napi_t *self = (utp_napi_t *) handle->data;

  if (nread == 0) {
    utp_issue_deferred_acks(self->utp);
    return;
  }

  if (nread > 0) {
    const unsigned char *base = (const unsigned char *) buf->base;
    if (utp_process_udp(self->utp, base, nread, addr, sizeof(struct sockaddr))) return;
  }

  int port;
  char ip[17];
  utp_napi_parse_address((struct sockaddr *) addr, ip, &port);

  UTP_NAPI_CALLBACK(self->on_message, {
    napi_value ret;
    napi_value argv[3];
    napi_create_int32(env, nread, &(argv[0]));
    napi_create_uint32(env, port, &(argv[1]));
    napi_create_string_utf8(env, ip, NAPI_AUTO_LENGTH, &(argv[2]));
    NAPI_MAKE_CALLBACK(env, NULL, ctx, callback, 3, argv, &ret)
    UTP_NAPI_BUFFER_ALLOC(self, ret, nread)
  })
}

static void
on_uv_close (uv_handle_t *handle) {
  utp_napi_t *self = (utp_napi_t *) handle->data;

  UTP_NAPI_CALLBACK(self->on_close, {
    NAPI_MAKE_CALLBACK(env, NULL, ctx, callback, 0, NULL, NULL);
  })
}

static void
on_uv_send (uv_udp_send_t *req, int status) {
  uv_udp_t *handle = req->handle;
  utp_napi_t *self = (utp_napi_t *) handle->data;
  utp_napi_send_request_t *send = (utp_napi_send_request_t *) req->data;

  UTP_NAPI_CALLBACK(self->on_send, {
    napi_value argv[2];
    napi_get_reference_value(env, send->ctx, &(argv[0]));
    napi_create_int32(env, status, &(argv[1]));
    NAPI_MAKE_CALLBACK(env, NULL, ctx, callback, 2, argv, NULL);
  })
}

static uint64
on_utp_firewall (utp_callback_arguments *a) {
  utp_napi_t *self = (utp_napi_t *) utp_context_get_userdata(a->context);

  return self->accept_connections ? 0 : 1;
}

static uint64
on_utp_state_change (utp_callback_arguments *a) {
  printf("on_utp_statechange\n");
  return 0;
}

static uint64
on_utp_accept (utp_callback_arguments *a) {
  utp_napi_t *self = (utp_napi_t *) utp_context_get_userdata(a->context);

  struct sockaddr addr;
  socklen_t addr_len = sizeof(addr);
  utp_getpeername(a->socket, &addr, &addr_len);

  int port;
  char ip[17];
  utp_napi_parse_address(&addr, ip, &port);

  UTP_NAPI_CALLBACK(self->on_connection, {
    napi_value argv[2];
    napi_create_uint32(env, port, &(argv[0]));
    napi_create_string_utf8(env, ip, NAPI_AUTO_LENGTH, &(argv[1]));
    napi_value socket;
    NAPI_MAKE_CALLBACK(env, NULL, ctx, callback, 2, argv, &socket)

    NAPI_BUFFER_CAST(utp_napi_connection_t *, connection, socket)
    connection->socket = a->socket;
    utp_set_userdata(a->socket, connection);
  })

  return 0;
}

static uint64
on_utp_error (utp_callback_arguments *a) {
  printf("on_utp_error\n");
  return 0;
}

static uint64
on_utp_read (utp_callback_arguments *a) {
  utp_napi_connection_t *self = (utp_napi_connection_t *) utp_get_userdata(a->socket);

  memcpy(self->buf.base, a->buf, a->len);
  self->packet_size += a->len;

  if (self->packet_size < self->min_packet_size) return 0;

  UTP_NAPI_CALLBACK(self->on_read, {
    napi_value ret;
    napi_value argv[1];
    napi_create_uint32(env, self->packet_size, &(argv[0]));
    NAPI_MAKE_CALLBACK(env, NULL, ctx, callback, 1, argv, &ret)
    UTP_NAPI_BUFFER_ALLOC(self, ret, self->packet_size)
    self->packet_size = 0;
  })

  return 0;
}

static uint64
on_utp_sendto (utp_callback_arguments *a) {
  utp_napi_t *self = (utp_napi_t *) utp_context_get_userdata(a->context);

  uv_buf_t buf = uv_buf_init((char *) a->buf, a->len);
  uv_udp_try_send(&(self->handle), &buf, 1, a->address);

  return 0;
}

NAPI_METHOD(utp_napi_init) {
  NAPI_ARGV(7)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  self->env = env;
  napi_create_reference(env, argv[1], 1, &(self->ctx));

  uv_timer_t *timer = &(self->timer);
  timer->data = self;

  int err = uv_timer_init(uv_default_loop(), timer);
  if (err < 0) UTP_NAPI_THROW(err)

  NAPI_ARGV_BUFFER(buf, 2)
  self->buf.base = buf;
  self->buf.len = buf_len;

  uv_udp_t *handle = &(self->handle);
  handle->data = self;

  err = uv_udp_init(uv_default_loop(), handle);
  if (err < 0) UTP_NAPI_THROW(err)

  napi_create_reference(env, argv[3], 1, &(self->on_message));
  napi_create_reference(env, argv[4], 1, &(self->on_send));
  napi_create_reference(env, argv[5], 1, &(self->on_connection));
  napi_create_reference(env, argv[6], 1, &(self->on_close));

  self->utp = utp_init(2);
  utp_context_set_userdata(self->utp, self);

  utp_set_callback(self->utp, UTP_ON_STATE_CHANGE, &on_utp_state_change);
  utp_set_callback(self->utp, UTP_ON_READ, &on_utp_read);
  utp_set_callback(self->utp, UTP_ON_FIREWALL, &on_utp_firewall);
  utp_set_callback(self->utp, UTP_ON_ACCEPT, &on_utp_accept);
  utp_set_callback(self->utp, UTP_SENDTO, &on_utp_sendto);
  utp_set_callback(self->utp, UTP_ON_ERROR, &on_utp_error);

  self->accept_connections = 0;

  return NULL;
}

NAPI_METHOD(utp_napi_close) {
  NAPI_ARGV(1)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  int err;

  err = uv_timer_stop(&(self->timer));
  if (err < 0) UTP_NAPI_THROW(err)

  err = uv_udp_recv_stop(&(self->handle));
  if (err < 0) UTP_NAPI_THROW(err)

  uv_close((uv_handle_t *) &(self->handle), on_uv_close);

  return NULL;
}

NAPI_METHOD(utp_napi_destroy) {
  NAPI_ARGV(2)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)
  napi_value send_reqs = argv[1];

  self->buf.base = NULL;
  self->buf.len = 0;

  napi_delete_reference(env, self->ctx);
  napi_delete_reference(env, self->on_message);
  napi_delete_reference(env, self->on_send);

  NAPI_FOR_EACH(send_reqs, el) {
    NAPI_BUFFER_CAST(utp_napi_send_request_t *, send_req, el)
    napi_delete_reference(env, send_req->ctx);
  }

  utp_destroy(self->utp);
  self->utp = NULL;

  return NULL;
}

NAPI_METHOD(utp_napi_bind) {
  NAPI_ARGV(3)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)
  NAPI_ARGV_UINT32(port, 1)
  NAPI_ARGV_UTF8(ip, 17, 2)

  uv_udp_t *handle = &(self->handle);

  int err;
  struct sockaddr_in addr;

  err = uv_ip4_addr((char *) &ip, port, &addr);
  if (err < 0) UTP_NAPI_THROW(err)

  err = uv_udp_bind(handle, (const struct sockaddr*) &addr, 0);
  if (err < 0) UTP_NAPI_THROW(err)

  // TODO: We should close the handle here also if this fails
  err = uv_udp_recv_start(handle, on_uv_alloc, on_uv_read);
  if (err < 0) UTP_NAPI_THROW(err)

  // TODO: same as above
  err = uv_timer_start(&(self->timer), on_uv_interval, UTP_NAPI_TIMEOUT_INTERVAL, UTP_NAPI_TIMEOUT_INTERVAL);
  if (err < 0) UTP_NAPI_THROW(err)

  uv_unref((uv_handle_t *) &(self->timer));

  return NULL;
}

NAPI_METHOD(utp_napi_accept_connections) {
  NAPI_ARGV(1)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  self->accept_connections = 1;

  return NULL;
}

NAPI_METHOD(utp_napi_local_port) {
  NAPI_ARGV(1)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  int err;
  struct sockaddr name;
  int name_len = sizeof(name);

  err = uv_udp_getsockname(&(self->handle), &name, &name_len);
  if (err < 0) UTP_NAPI_THROW(err)

  struct sockaddr_in *name_in = (struct sockaddr_in *) &name;
  int port = ntohs(name_in->sin_port);

  NAPI_RETURN_UINT32(port)
}

NAPI_METHOD(utp_napi_send_request_init) {
  NAPI_ARGV(2)
  NAPI_ARGV_BUFFER_CAST(utp_napi_send_request_t *, send_req, 0)

  uv_udp_send_t *req = &(send_req->req);
  req->data = send_req;

  napi_create_reference(env, argv[1], 1, &(send_req->ctx));

  return NULL;
}

NAPI_METHOD(utp_napi_send) {
  NAPI_ARGV(7)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)
  NAPI_ARGV_BUFFER_CAST(utp_napi_send_request_t *, send_req, 1)
  NAPI_ARGV_BUFFER(buf, 2)
  NAPI_ARGV_UINT32(offset, 3)
  NAPI_ARGV_UINT32(len, 4)
  NAPI_ARGV_UINT32(port, 5)
  NAPI_ARGV_UTF8(ip, 17, 6)

  uv_udp_send_t *req = &(send_req->req);

  const uv_buf_t bufs = {
    .base = buf + offset,
    .len = len
  };

  struct sockaddr_in addr;
  int err;

  err = uv_ip4_addr((char *) &ip, port, &addr);
  if (err) UTP_NAPI_THROW(err)

  err = uv_udp_send(req, &(self->handle), &bufs, 1, (const struct sockaddr *) &addr, on_uv_send);
  if (err) UTP_NAPI_THROW(err)

  return NULL;
}

NAPI_METHOD(utp_napi_ref) {
  NAPI_ARGV(1)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  uv_ref((uv_handle_t *) &(self->handle));

  return NULL;
}

NAPI_METHOD(utp_napi_unref) {
  NAPI_ARGV(1)
  NAPI_ARGV_BUFFER_CAST(utp_napi_t *, self, 0)

  uv_unref((uv_handle_t *) &(self->handle));

  return NULL;
}

NAPI_METHOD(utp_napi_connection_init) {
  NAPI_ARGV(4)
  NAPI_ARGV_BUFFER_CAST(utp_napi_connection_t *, self, 0)

  self->env = env;

  napi_create_reference(env, argv[1], 1, &(self->ctx));

  NAPI_ARGV_BUFFER(buf, 2)
  self->buf.base = buf;
  self->buf.len = buf_len;

  napi_create_reference(env, argv[3], 1, &(self->on_read));

  return NULL;
}

NAPI_INIT() {
  NAPI_EXPORT_SIZEOF(utp_napi_t)
  NAPI_EXPORT_SIZEOF(utp_napi_send_request_t)
  NAPI_EXPORT_SIZEOF(utp_napi_connection_t)
  NAPI_EXPORT_FUNCTION(utp_napi_init)
  NAPI_EXPORT_FUNCTION(utp_napi_bind)
  NAPI_EXPORT_FUNCTION(utp_napi_accept_connections)
  NAPI_EXPORT_FUNCTION(utp_napi_local_port)
  NAPI_EXPORT_FUNCTION(utp_napi_send_request_init)
  NAPI_EXPORT_FUNCTION(utp_napi_send)
  NAPI_EXPORT_FUNCTION(utp_napi_close)
  NAPI_EXPORT_FUNCTION(utp_napi_destroy)
  NAPI_EXPORT_FUNCTION(utp_napi_ref)
  NAPI_EXPORT_FUNCTION(utp_napi_unref)
  NAPI_EXPORT_FUNCTION(utp_napi_connection_init)
}
