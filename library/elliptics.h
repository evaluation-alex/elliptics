/*
 * 2008+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DNET_ELLIPTICS_H
#define __DNET_ELLIPTICS_H

#include <sys/time.h>
#include <sys/socket.h>

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef HAVE_UCHAR
typedef unsigned char u_char;
typedef unsigned short u_short;
#endif

#include <event.h>

#include "list.h"
#include "rbtree.h"
#include "dnet/packet.h"
#include "dnet/interface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef __unused
#define __unused	__attribute__ ((unused))
#endif

struct dnet_node;

/*
 * Initialize private logging system.
 */
int dnet_log_init(struct dnet_node *n, void *priv, uint32_t mask,
		void (* log)(void *priv, uint32_t mask, const char *msg));

#define NIP6(addr) \
	(addr).s6_addr[0], \
	(addr).s6_addr[1], \
	(addr).s6_addr[2], \
	(addr).s6_addr[3], \
	(addr).s6_addr[4], \
	(addr).s6_addr[5], \
	(addr).s6_addr[6], \
	(addr).s6_addr[7], \
	(addr).s6_addr[8], \
	(addr).s6_addr[9], \
	(addr).s6_addr[10], \
	(addr).s6_addr[11], \
	(addr).s6_addr[12], \
	(addr).s6_addr[13], \
	(addr).s6_addr[14], \
	(addr).s6_addr[15]
#define NIP6_FMT "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x"

static inline char *dnet_server_convert_addr(struct sockaddr *sa, unsigned int len)
{
	static char inet_addr[128];

	memset(&inet_addr, 0, sizeof(inet_addr));
	if (len == sizeof(struct sockaddr_in)) {
		struct sockaddr_in *in = (struct sockaddr_in *)sa;
		sprintf(inet_addr, "%s", inet_ntoa(in->sin_addr));
	} else if (len == sizeof(struct sockaddr_in6)) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *)sa;
		sprintf(inet_addr, NIP6_FMT, NIP6(in->sin6_addr));
	}
	return inet_addr;
}

static inline int dnet_server_convert_port(struct sockaddr *sa, unsigned int len)
{
	if (len == sizeof(struct sockaddr_in)) {
		struct sockaddr_in *in = (struct sockaddr_in *)sa;
		return ntohs(in->sin_port);
	} else if (len == sizeof(struct sockaddr_in6)) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *)sa;
		return ntohs(in->sin6_port);
	}
	return 0;
}

static inline char *dnet_server_convert_dnet_addr(struct dnet_addr *sa)
{
	static char inet_addr[128];

	memset(&inet_addr, 0, sizeof(inet_addr));
	if (sa->addr_len == sizeof(struct sockaddr_in)) {
		struct sockaddr_in *in = (struct sockaddr_in *)sa;
		sprintf(inet_addr, "%s:%d", inet_ntoa(in->sin_addr), ntohs(in->sin_port));
	} else if (sa->addr_len == sizeof(struct sockaddr_in6)) {
		struct sockaddr_in6 *in = (struct sockaddr_in6 *)sa;
		sprintf(inet_addr, NIP6_FMT":%d", NIP6(in->sin6_addr), ntohs(in->sin6_port));
	}
	return inet_addr;
}

/*
 * Currently executed network state machine:
 * receives and sends command and data.
 */

/* Reading a command */
#define DNET_IO_CMD		(1<<0)

/* Attached data should be discarded */
#define DNET_IO_DROP		(1<<1)

struct dnet_net_state
{
	struct list_head	state_entry;

	struct dnet_node	*n;
	long			timeout;

	pthread_mutex_t		refcnt_lock;
	int			refcnt;
	int			s;

	int			join_state;
	unsigned char		id[DNET_ID_SIZE];

	struct dnet_addr	addr;

	struct event		rcv_ev;
	struct dnet_cmd		rcv_cmd;
	off_t			rcv_offset;
	size_t			rcv_size;
	unsigned int		rcv_flags;
	void			*rcv_data;
	struct dnet_trans	*rcv_trans;
	
	struct event		snd_ev;
	struct list_head	snd_list;
	pthread_mutex_t		snd_lock;
	off_t			snd_offset;
	size_t			snd_size;
};

struct dnet_net_state *dnet_state_create(struct dnet_node *n, unsigned char *id,
		struct dnet_addr *addr, int s);

static inline struct dnet_net_state *dnet_state_get(struct dnet_net_state *st)
{
	pthread_mutex_lock(&st->refcnt_lock);
	st->refcnt++;
	pthread_mutex_unlock(&st->refcnt_lock);
	return st;
}
void dnet_state_put(struct dnet_net_state *st);
void *dnet_state_process(void *data);

int dnet_state_insert(struct dnet_net_state *new);
void dnet_state_remove(struct dnet_net_state *st);
struct dnet_net_state *dnet_state_search(struct dnet_node *n, unsigned char *id, struct dnet_net_state *self);
struct dnet_net_state *dnet_state_get_first(struct dnet_node *n, unsigned char *id, struct dnet_net_state *self);
int dnet_state_move(struct dnet_net_state *st);

struct dnet_wait
{
	pthread_cond_t		wait;
	pthread_mutex_t		wait_lock;
	int			cond;
	int			status;

	int			refcnt;
};

#define dnet_wait_event(w, condition, wts)						\
({											\
	int err = 0;									\
	struct timespec ts;								\
 	struct timeval tv;								\
	gettimeofday(&tv, NULL);							\
	ts.tv_nsec = tv.tv_usec * 1000 + (wts)->tv_nsec;				\
	ts.tv_sec = tv.tv_sec + (wts)->tv_sec;						\
	pthread_mutex_lock(&(w)->wait_lock);						\
	while (!(condition) && !err)							\
		err = pthread_cond_timedwait(&(w)->wait, &(w)->wait_lock, &ts);		\
	pthread_mutex_unlock(&(w)->wait_lock);						\
	-err;										\
})

#define dnet_wakeup(w, task)								\
({											\
	pthread_mutex_lock(&(w)->wait_lock);						\
	task;										\
	pthread_cond_broadcast(&(w)->wait);						\
	pthread_mutex_unlock(&(w)->wait_lock);						\
})

struct dnet_wait *dnet_wait_alloc(int cond);
void dnet_wait_destroy(struct dnet_wait *w);

static inline struct dnet_wait *dnet_wait_get(struct dnet_wait *w)
{
	pthread_mutex_lock(&w->wait_lock);
	w->refcnt++;
	pthread_mutex_unlock(&w->wait_lock);

	return w;
}

static inline void dnet_wait_put(struct dnet_wait *w)
{
	int freeing = 0;

	pthread_mutex_lock(&w->wait_lock);
	w->refcnt--;
	freeing = !!w->refcnt;
	pthread_mutex_unlock(&w->wait_lock);

	if (freeing)
		dnet_wait_destroy(w);
}

struct dnet_io_thread
{
	struct list_head	thread_entry;

	int			need_exit;

	pthread_t		tid;

	struct dnet_node	*node;

	struct event_base	*base;
};

struct dnet_node
{
	unsigned char		id[DNET_ID_SIZE];

	pthread_mutex_t		tlock;
	struct list_head	tlist;

	int			need_exit;
	pthread_t		tid;

	int			listen_socket;

	struct dnet_addr	addr;
	int			sock_type, proto;

	pthread_mutex_t		state_lock;
	struct list_head	state_list;
	struct list_head	empty_state_list;

	int			trans_num;
	pthread_mutex_t		trans_lock;
	struct rb_root		trans_root;
	uint64_t		trans;

	struct dnet_net_state	*st;

	int			error;

	uint32_t		log_mask;
	void			*log_private;
	void			(*log)(void *priv, uint32_t mask, const char *msg);

	struct dnet_wait	*wait;
	struct timespec		wait_ts;

	uint64_t		total_synced_files;

	int			join_state;
	
	int			(* command_handler)(void *state, void *priv,
			struct dnet_cmd *cmd, struct dnet_attr *attr, void *data);
	void			*command_private;

	struct list_head	io_thread_list;
	pthread_mutex_t		io_thread_lock;
	int			io_thread_num, io_thread_pos;
};

static inline char *dnet_dump_node(struct dnet_node *n)
{
	static char buf[128];

	snprintf(buf, sizeof(buf), "%s", dnet_server_convert_dnet_addr(&n->addr));
	return buf;
}

struct dnet_trans;
int dnet_process_cmd(struct dnet_trans *t);

int dnet_send(struct dnet_net_state *st, void *data, unsigned int size);
int dnet_recv(struct dnet_net_state *st, void *data, unsigned int size);
int dnet_wait(struct dnet_net_state *st);
int dnet_sendfile_data(struct dnet_net_state *st,
		int fd, off_t offset, size_t size,
		void *header, unsigned int hsize);
int dnet_sendfile(struct dnet_net_state *st, int fd, off_t *offset, size_t size);

struct dnet_config;
int dnet_socket_create(struct dnet_node *n, struct dnet_config *cfg,
		struct sockaddr *sa, unsigned int *addr_len, int listening);
int dnet_socket_create_addr(struct dnet_node *n, int sock_type, int proto,
		struct sockaddr *sa, unsigned int salen, int listening);

enum dnet_join_state {
	DNET_CLIENT,		/* Node did not not join the network */
	DNET_JOINED,		/* Node joined the network */
	DNET_REJOIN,		/* Some of the states reconnected and node needs to rejoin */
};
int dnet_rejoin(struct dnet_node *n, int all);

struct dnet_data_req
{
	struct list_head	req_entry;

	void			*header;
	size_t			hsize;

	void			*data;
	size_t			dsize;

	unsigned int		flags;

	int			fd;
	off_t			offset;
	size_t			size;
};

struct dnet_trans
{
	struct rb_node			trans_entry;
	struct dnet_net_state		*st;
	uint64_t			trans, recv_trans;
	struct dnet_cmd			cmd;
	void				*data;

	struct dnet_data_req		r;

	void				*priv;
	int				(* complete)(struct dnet_net_state *st,
						     struct dnet_cmd *cmd,
						     struct dnet_attr *attr,
						     void *priv);
};

void dnet_trans_destroy(struct dnet_trans *t);
struct dnet_trans *dnet_trans_alloc(struct dnet_node *n, size_t size);

void dnet_trans_remove(struct dnet_trans *t);
void dnet_trans_remove_nolock(struct rb_root *root, struct dnet_trans *t);
int dnet_trans_insert(struct dnet_trans *t);
struct dnet_trans *dnet_trans_search(struct rb_root *root, uint64_t trans);

int dnet_recv_list(struct dnet_node *n, struct dnet_net_state *st);

struct dnet_io_completion
{
	struct dnet_wait	*wait;
	char			*file;
	off_t			offset;
	size_t			size;
};

int dnet_read_complete(struct dnet_net_state *st __unused, struct dnet_cmd *cmd,
		struct dnet_attr *attr, void *priv);

struct dnet_transform
{
	struct list_head	tentry;

	char			name[DNET_MAX_NAME_LEN];

	void			*priv;

	int			(* init)(void *priv);
	int 			(* update)(void *priv, void *src, uint64_t size,
					void *dst, unsigned int *dsize, unsigned int flags);
	int 			(* final)(void *priv, void *dst, unsigned int *dsize, unsigned int flags);
};

#ifdef __cplusplus
}
#endif

#endif /* __DNET_ELLIPTICS_H */
