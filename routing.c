#include <stdlib.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "debug.h"

#include "routing.h"
#include "dpeer.h"
#include "util.h"
#include "pkt.h"


#include "stdparam.h"
#include "darray.h"

#define RT_HOST_INIT 8
#define RT_LINK_INIT 8
#define RT_HOST_MULT 2
#define RT_LINK_MULT 2

#include <inttypes.h>

#define NEXT_DISCON SIZE_MAX
#define PATH_DISCON 0
#define PATH_MAX    UINT32_MAX
#define PATH_MIN    1

static int ipv4_cmp_mac(struct ipv4_host *h1, struct ipv4_host *h2)
{
	return memcmp(&h1->mac, &h2->mac, ETH_ALEN);
}

/* host_cmp - compare (struct _rt_host **) */
static int host_cmp_addr(const void *kp1_v, const void *kp2_v)
{
	struct _rt_host const *const *h1 = kp1_v;
	struct _rt_host const *const *h2 = kp2_v;

	const ether_addr_t *a1 = &((*h1)->host->mac);
	const ether_addr_t *a2 = &((*h2)->host->mac);
	return memcmp(a1, a2, ETH_ALEN);
}

/* link_cmp - compare (struct _rt_link *) */
static int link_cmp_addr(const void *kp1_v, const void *kp2_v)
{
	struct _rt_link const *l1 = kp1_v;
	struct _rt_link const *l2 = kp2_v;

	const ether_addr_t *a1 = &(l1->dst->host->mac);
	const ether_addr_t *a2 = &(l2->dst->host->mac);
	return memcmp(a1, a2, ETH_ALEN);
}


static DEF_BSEARCH(host, struct _rt_host *, host_cmp_addr)
static DEF_BSEARCH(link, struct _rt_link, link_cmp_addr)

static struct _rt_host **find_host_via_host(
		struct _rt_host **hosts,
		size_t host_ct,
		struct _rt_host *key)
{
	return bsearch_host(&key, hosts, host_ct);
}

static struct _rt_host **find_host_by_addr(
		da_t(rt_host) *da,
		ether_addr_t mac)
{
	struct ipv4_host ip_host = { .mac = mac };
	struct _rt_host h = { .host = &ip_host };
	struct _rt_host *key = &h;

	return bsearch_host(&key, da->items, da->ct);
}

static struct _rt_link *find_link_by_addr(
		da_t(rt_link) *da,
		ether_addr_t dst_mac)
{
	struct ipv4_host ip_host = { .mac = dst_mac };
	struct _rt_host h = { .host = &ip_host };
	struct _rt_link l = { .dst = &h };

	struct _rt_link *nl = bsearch_link(&l, da->items, da->ct);

	return nl;
}

static size_t host_to_index(routing_t *rd, struct _rt_host **host)
{
	return host - rd->hosts.items;
}

static struct _rt_host **index_to_host(routing_t *rd, size_t host_i)
{
	return rd->hosts.items + host_i;
}

/* updates the internal tracking of paths */
static int compute_paths(routing_t *rd)
{
	uint32_t **path;
	size_t **next;

	/* allocations */
	{
		path = rd->path;
		next = rd->next;


		size_t i;
		for (i = 0; i < rd->m_ct; i++) {
			free(path[i]);
			free(next[i]);
		}

		path = realloc(rd->path, sizeof(*path) * (rd->hosts.ct));
		if (!path)
			return -1;

		next = realloc(rd->next, sizeof(*next) * (rd->hosts.ct));
		if (!next) {
			free(path);
			return -2;
		}

		for (i = 0; i < rd->hosts.ct; i++) {
			path[i] = malloc(rd->hosts.ct * sizeof(*path[i]));
			if (!path[i]) {
				rd->m_ct = 0;
				return -3;
			}

			next[i] = malloc(rd->hosts.ct * sizeof(*next[i]));
			if (!next[i]) {
				rd->m_ct = 0;
				return -4;
			}

			size_t j;
			for (j = 0; j < rd->hosts.ct; j++) {
				next[i][j] = NEXT_DISCON;
				path[i][j] = PATH_DISCON;
			}
		}
		rd->m_ct = rd->hosts.ct;;
	}


	/* setup data in adjacency matrix */
	{
		size_t i;
		for (i = 0; i < rd->hosts.ct; i++) {
			struct _rt_host *host = rd->hosts.items[i];
			size_t j;

			path[i][i] = PATH_MIN; /* add minimal path to self */
			for (j = 0; j < host->links.ct; j++) {
				struct _rt_link *l =
					&rd->hosts.items[i]->links.items[j];
				struct _rt_host *dst = l->dst;
				struct _rt_host **dstp = find_host_by_addr(
								&rd->hosts,
								dst->host->mac);

				size_t dst_i = host_to_index(rd, dstp);
				path[i][dst_i] = l->rtt_us;

				/* TODO: make bidirectionality assumption in
				 * the case where we lack information.
				 */
			}
		}
	}

	/* use Floyd-Warshal to find all pairs of shortest paths */
#if 0
 1 procedure FloydWarshallWithPathReconstruction ()
 2    for k := 1 to n
 3       for i := 1 to n
 4          for j := 1 to n
 5             if path[i][k] + path[k][j] < path[i][j] then
 6                path[i][j] := path[i][k]+path[k][j];
 7                next[i][j] := k;
 8
 9 procedure GetPath (i,j)
10    if path[i][j] equals infinity then
11      return "no path";
12    int intermediate := next[i][j];
13    if intermediate equals 'null' then
14      return " ";   /* there is an edge from i to j, with no vertices between */
15   else
16      return GetPath(i,intermediate) + intermediate + GetPath(intermediate,j);
#endif
	{
		size_t n = rd->hosts.ct;
		size_t k, j ,i;
		for (k = 0; k < n; k++)
		for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			//DEBUG("FW k:%zu i:%zu j:%zu",k,i,j);
			if (path[i][k] == PATH_DISCON ||
			    path[k][j] == PATH_DISCON) {
				/* skip items which are
				 * disconnected (== 0)
				 */
				//DEBUG("FW -- skipping.");
				continue;
			}
			uint32_t x = path[i][k] + path[k][j];
			if ((path[i][k] == 1) && (path[k][j] == 1)) {
				//DEBUG("FW -- len hack");
				x--;
			}

			/* overflow possible */
			if (x < path[i][k] || x < path[k][j]) {
				//WARN("FW -- path wieght overflow");
				x = PATH_MAX;
			}
			/*
			DEBUG("FW -- path[i][k]:%"PRIu32
				" path[k][j]:%"PRIu32
				" sum:%"PRIu32
				" path[i][j]:%"PRIu32,
				path[i][k],
				path[k][j],
				x,
				path[i][j]);
			*/
			if (x < path[i][j]) {
				//DEBUG("FW -- found better path");
				path[i][j] = x;
				next[i][j] = k;
			} else {
				//DEBUG("FW -- not using path");
			}
		}
	}

	rd->path = path;
	rd->next = next;

	return 0;
}

static int update_exported_edges(routing_t *rd)
{
	size_t i;
	for (i = 0; i < rd->hosts.ct; i++) {
		struct _rt_host *h = rd->hosts.items[i];
		struct _pkt_ipv4_host src;
		pkt_ipv4_pack(&src, h->host);

		size_t j;
		for (j = 0; j < h->links.ct; j ++) {
			if (rd->path[i][j] == 0)
				continue;

			struct _rt_link *link = &h->links.items[j];

			struct _pkt_edge new_edge;
			new_edge.src = src;

			pkt_ipv4_pack(&new_edge.dst, link->dst->host);

			EDGE_DEBUG(rd->edges.ct, h->host, link->dst->host,
					"rtt:%"PRIu32" ts:%"PRIu64,
					link->rtt_us, link->ts_ms);
			new_edge.rtt_us = htonl(link->rtt_us);
			new_edge.ts_ms = htonll(link->ts_ms);

			if (DA_ADD_TO_END(&rd->edges, new_edge))
				return -1;
		}
	}

	DEBUG("updated exported edges, ct:%zu mem:%zu", rd->edges.ct,
			rd->edges.mem);

	return 0;
}


static void link_remove(struct _rt_host *src, struct _rt_link *link)
{
	DA_REMOVE(src->links.items, src->links.ct, link);
}

static void free_host(struct _rt_host *h)
{
	free(h->host);
	DA_DESTROY(&h->links);
	free(h);
}

static void host_remove(routing_t *rd, struct _rt_host **h)
{
	DA_REMOVE(rd->hosts.items, rd->hosts.ct, h);
}

static void trim_host(routing_t *rd, struct _rt_host **h)
{
	if ((*h)->type != HT_NORMAL) {
		uint8_t *m = (*h)->host->mac.addr;
		WARN("attempt to trim abnormal peer "
			"%02x:%02x:%02x:%02x:%02x:%02x of type %d",
			m[0], m[1], m[2], m[3], m[4], m[5],
			(*h)->type);
		return;
	}


	size_t i;
	for (i = 0; i < rd->hosts.ct; i++) {
		struct _rt_host *pos_src = rd->hosts.items[i];

		struct _rt_link *link = find_link_by_addr(&pos_src->links,
							(*h)->host->mac);

		if (link) {
			link_remove(pos_src, link);
		}
	}

	host_remove(rd, h);
	free_host(*h);
}

static int trim_disjoint_hosts(routing_t *rd)
{
	/* find hosts which lack outgoing (and incomming?)
	 * links and remove them */
	struct _rt_host **src = find_host_by_addr(&rd->hosts,
					rd->local->host->mac);
	size_t src_i = host_to_index(rd, src);

	if (rd->m_ct != rd->hosts.ct)
		return 1;

	size_t dst_i;
	for (dst_i = 0; dst_i < rd->hosts.ct; dst_i++) {
		uint32_t path = rd->path[src_i][dst_i];
		if (path == 0 && dst_i != src_i) {
			struct _rt_host **h_to_trim = index_to_host(rd,
					dst_i);
			uint8_t *m = (*h_to_trim)->host->mac.addr;
			DEBUG("trimming host %02x:%02x:%02x"
					":%02x:%02x:%02x - %zu",
				       m[0],m[1],m[2],m[3],m[4],m[5],
				       dst_i);
			trim_host(rd, index_to_host(rd, dst_i));
		}
	}

	return 0;
}

static void print_matrix(routing_t *rd, FILE *out)
{
	size_t n = rd->m_ct;

	/* first row: size, 0, 1, 2 ... */
	fprintf(out, "%6zu | ", n);
	size_t i;
	for (i = 0; i < n; i ++) {
		fprintf(out, "%9zu", i);
	}
	fputc('\n', out);

	for (i = 0; i < (9 * (n + 1)); i++) {
		fputc('-',out);
	}
	fputc('\n', out);

	/* each following row: row_i w0, w1, w2 */
	for (i = 0; i < n; i ++) {
		fprintf(out, "%6zu | ", i);

		size_t j;
		for (j = 0; j < n; j ++) {
			uint32_t x = rd->path[j][i];
			fprintf(out, "%9"PRIu32, x);
		}
		fputc('\n', out);
	}

	for (i = 0; i < (9 * (n + 1)); i++) {
		fputc('-',out);
	}
	fputc('\n', out);

	for (i = 0; i < n; i ++) {
		fprintf(out, "%6zu | ", i);

		size_t j;
		for (j = 0; j < n; j ++) {
			size_t x = rd->next[j][i];
			if (x == SIZE_MAX) {
				fprintf(out, "%9c", 'x');
			} else {
				fprintf(out, "%9zu", rd->next[i][j]);
			}
		}
		fputc('\n', out);
	}
}

static int update_cache(routing_t *rd)
{
	int ret = trim_disjoint_hosts(rd);
	if (ret < 0) {
		WARN("trim_disjoint_hosts %d", ret);
		return ret;
	}

	ret = compute_paths(rd);
	if (ret < 0) {
		WARN("compute_paths %d", ret);
		return ret;
	}

	ret = update_exported_edges(rd);
	if (ret < 0) {
		WARN("update_exported_edges %d", ret);
		return ret;
	}

	print_matrix(rd, stderr);

	return 0;
}

static int host_alloc(struct ipv4_host *ip_host,
		enum host_type type, struct _rt_host **host)
{
	struct _rt_host *h = malloc(sizeof(*h));
	if (!h) {
		return -1;
	}

	if (DA_INIT(&h->links, RT_LINK_INIT)) {
		free(h);
		return -1;
	}

	h->l_max_ts_ms = 0;

	h->type = type;
	if (type == HT_DIRECT || type == HT_LOCAL) {
		h->host = ip_host;
	} else {
		h->host = malloc(sizeof(*(h->host)));
		if (!h->host) {
			DA_DESTROY(&h->links);
			free(h);
			return -4;
		}
		*h->host = *ip_host;
	}

	*host = h;
	return 0;
}

static int link_add(struct _rt_host *src, struct _rt_host *dst,
		uint32_t rtt_us, uint64_t ts_ms)
{
	EDGE_WARN((size_t)999, src->host, dst->host, "adding new link $$");

	struct _rt_link l = {
		.dst = dst,
		.ts_ms = ts_ms,
		.rtt_us = rtt_us
	};
	if(DA_ADD_TO_END(&src->links, l)) {
		return -1;
	}

	if (ts_ms > src->l_max_ts_ms) {
		src->l_max_ts_ms = ts_ms;
	}

	qsort(src->links.items, src->links.ct,
			sizeof(*src->links.items), link_cmp_addr);

	return 0;
}

static int host_insert_unchecked(da_t(rt_host) *da, struct _rt_host *new_host)
{

	if (DA_ADD_TO_END(da, new_host))
		return -1;

	qsort(da->items, da->ct, sizeof(*da->items), host_cmp_addr);

	return 0;
}

static int host_add(routing_t *rd,
		struct ipv4_host *ip_host, enum host_type type,
		struct _rt_host **res)
{
	struct _rt_host **dup = find_host_by_addr(&rd->hosts, ip_host->mac);

	/* dpeer already exsists. */
	if (dup) {
		return 1;
	}

	struct _rt_host *nh;
	int ret = host_alloc(ip_host, type, &nh);
	if (ret) {
		return -3;
	}

	ret = host_insert_unchecked(&rd->hosts, nh);
	if (ret < 0) {
		/* host dealloc,
		 * TODO: make safer. */
		DA_DESTROY(&nh->links);
		free(nh);
		return ret;
	}

	if (res) {
		*res = nh;
	}

	return 0;
}

static int host_insert(da_t(rt_host) *da, struct _rt_host *new_host)
{
	struct _rt_host **dup = find_host_via_host(da->items, da->ct, new_host);
	if (dup)
		return 1;

	return host_insert_unchecked(da, new_host);
}

int rt_init(routing_t *rd)
{
	int ret = pthread_rwlock_init(&rd->lock, NULL);
	if (ret < 0)
		return ret;

	if (DA_INIT(&rd->hosts, RT_HOST_INIT))
		return -1;

	if (DA_INIT(&rd->edges, DA_INIT_MEM))
		return -2;

	rd->path = NULL;
	rd->next = NULL;
	rd->m_ct = 0;

	return 0;
}

void rt_destroy(routing_t *rd)
{
	DA_DESTROY(&rd->hosts);
	DA_DESTROY(&rd->edges);
	pthread_rwlock_destroy(&rd->lock);
}

/*
 * on failure, returns < 0.
 * if a duplicate exsists, returns 1.
 * otherwise, returns 0.
 */
int rt_lhost_add(routing_t *rd, struct ipv4_host *ip_host)
{
	pthread_rwlock_wrlock(&rd->lock);

	struct _rt_host *h;
	int p = host_add(rd, ip_host, HT_LOCAL, &h);
	if (!p) {
		rd->local = h;
	}

	pthread_rwlock_unlock(&rd->lock);

	return p;
}

static void ihost_to_dhost(struct _rt_host *host, struct ipv4_host *ip_host)
{
	if (host->type == HT_NORMAL) {
		free(host->host);
		host->host = ip_host;
		host->type = HT_DIRECT;
	}
}

int rt_dhost_add_link(routing_t *rd, struct ipv4_host *dst_ip_host, uint32_t rtt_us)
{
	pthread_rwlock_wrlock(&rd->lock);

	struct _rt_host **src_host_p = find_host_by_addr(&rd->hosts,
						rd->local->host->mac);

	if (!src_host_p) {
		/* source host does not exsist */
		pthread_rwlock_unlock(&rd->lock);
		return -1;
	}
	uint8_t *m = rd->local->host->mac.addr;
	uint8_t *d = dst_ip_host->mac.addr;
	DEBUG("dhost_add_link %02x:%02x:%02x:%02x:%02x:%02x ->"
			" %02x:%02x:%02x:%02x:%02x:%02x",
		m[0],m[1],m[2],m[3],m[4],m[5],
		d[0],d[1],d[2],d[3],d[4],d[5]
		);

	struct _rt_host *sh = *src_host_p;

	if (!memcmp(sh->host->mac.addr, dst_ip_host->mac.addr, ETH_ALEN)) {
		WARN("error: dhost_add_link called with local as the destination.");
		pthread_rwlock_unlock(&rd->lock);
		return -2;
	}

	struct _rt_link *stod = find_link_by_addr(&sh->links, dst_ip_host->mac);

	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (!stod) {
		/* link does not exsist */

		struct _rt_host **dst_host_p = find_host_by_addr(
						&rd->hosts,
						dst_ip_host->mac);
		struct _rt_host *dst_host = NULL;
		if (!dst_host_p) {
			/* dst_host does not exsist, create */
			int ret = host_add(rd, dst_ip_host, HT_DIRECT, &dst_host);
			if (ret) {
				pthread_rwlock_unlock(&rd->lock);
				return -2;
			}
		} else {
			/* make dst a dhost if it is not already */
			dst_host = *dst_host_p;
			ihost_to_dhost(dst_host, dst_ip_host);
		}

		/* dst_host does exsist, link up */
		EDGE_DEBUG((size_t)777, sh->host, dst_host->host,
				"adding link %%");
		int ret = link_add(sh, dst_host, rtt_us, tv_ms(&tv));
		if (ret) {
			pthread_rwlock_unlock(&rd->lock);
			return -3;
		}

	} else {
		/* link  exsists. update rtt & ts */
		stod->rtt_us = rtt_us;
		stod->ts_ms = tv_ms(&tv);

		/* host should already be a dhost, ignoring */
	}

	int ret = update_cache(rd);
	if (ret) {
		pthread_rwlock_unlock(&rd->lock);
		return -5;
	}
	pthread_rwlock_unlock(&rd->lock);
	return 0;
}

static int pkt_edges_cmp_src(const void *v1, const void *v2)
{
	const struct _pkt_edge *e1 = v1;
	const struct _pkt_edge *e2 = v2;

	int ret = memcmp(e1->src.mac, e2->src.mac, ETH_ALEN);
	if (!ret) {
		if (e1->ts_ms > e2->ts_ms)
			return -1;
		else if (e1->ts_ms < e2->ts_ms)
			return 1;
		else
			return 0;
	}
	return ret;
}

int rt_update_edges(routing_t *rd, struct _pkt_edge *edges, size_t e_ct)
{
	pthread_rwlock_wrlock(&rd->lock);

	qsort(edges, e_ct, sizeof(*edges), pkt_edges_cmp_src);

	size_t i;

	struct ipv4_host cur_ip_src = {};
	struct _rt_host *cur_host_src = NULL;

	bool cont_to_new_src = false;

	for (i = 0; i < e_ct; i++) {
		struct _pkt_edge *e = &edges[i];
		struct _pkt_ipv4_host *psrc = &e->src;
		struct _pkt_ipv4_host *pdst = &e->dst;
		uint32_t rtt_us = ntohl(e->rtt_us);
		uint64_t ts_ms = ntohll(e->ts_ms);

		struct ipv4_host src, dst;

		pkt_ipv4_unpack(psrc, &src);
		pkt_ipv4_unpack(pdst, &dst);

		if (!ipv4_cmp_mac(&src, &dst)) {
			EDGE_WARN((size_t)666, &src, &dst,
				"recieved bad edge (doubled back)");
			continue;
		}


		if (cont_to_new_src) {
			if (!ipv4_cmp_mac(&src, &cur_ip_src))
				continue;
			else
				cont_to_new_src = false;
		}

		if ( !cur_host_src || ipv4_cmp_mac(&src, &cur_ip_src) ) {
			/* we are on a new source */
			cur_ip_src = src;

			struct _rt_host **hsrcp = find_host_by_addr(
							&rd->hosts,
							cur_ip_src.mac);

			if (!hsrcp) {
				/* new source host does not exsist, create it. */
				int ret = host_add(rd, &cur_ip_src,
						HT_NORMAL, &cur_host_src);
				if (ret) {
					pthread_rwlock_unlock(&rd->lock);
					return -1;
				}
			} else {
				/* new source host does exsist, if it is not
				 * a lhost, wipe it's links if the ones
				 * we have are newer. */
				cur_host_src = *hsrcp;

				if (cur_host_src->type != HT_LOCAL &&
						cur_host_src->l_max_ts_ms < ts_ms) {
					cur_host_src->links.ct = 0;
				} else {
					/* advance to a different src host */
					cont_to_new_src = true;
					continue;
				}
			}
		}

		/* find destination. */
		struct _rt_host **dst_hostp = find_host_by_addr(&rd->hosts,
						dst.mac);

		struct _rt_host *dst_host;
		if (!dst_hostp) {
			host_add(rd, &dst, HT_NORMAL, &dst_host);
		} else {
			dst_host = *dst_hostp;
		}

		/* add link */
		int ret = link_add(cur_host_src, dst_host, rtt_us, ts_ms);
		if (ret) {
			pthread_rwlock_unlock(&rd->lock);
			return -2;
		}
	}

	int ret = update_cache(rd);
	if (ret) {
		pthread_rwlock_unlock(&rd->lock);
		return -5;
	}
	pthread_rwlock_unlock(&rd->lock);
	return 0;
}


int rt_dhost_remove(routing_t *rd, ether_addr_t *dmac)
{
	pthread_rwlock_wrlock(&rd->lock);

	struct _rt_host **sh_ = find_host_by_addr(&rd->hosts,
						rd->local->host->mac);
	if (!sh_) {
		pthread_rwlock_unlock(&rd->lock);
		return -10;
	}
	struct _rt_host *sh = *sh_;

	struct _rt_link *l = find_link_by_addr(&sh->links, *dmac);
	if (!l) {
		pthread_rwlock_unlock(&rd->lock);
		return 1;
	}

	struct _rt_host *h = l->dst;

	if (h->type != HT_DIRECT) {
		pthread_rwlock_unlock(&rd->lock);
		return -5;
	}

	struct ipv4_host *iph = malloc(sizeof(*iph));
	if (!iph) {
		pthread_rwlock_unlock(&rd->lock);
		return -3;
	}

	*iph = *h->host;
	h->host = iph;
	h->type = HT_NORMAL;

	link_remove(sh, l);

	int ret = update_cache(rd);
	if (ret) {
		pthread_rwlock_unlock(&rd->lock);
		return -2;
	}
	pthread_rwlock_unlock(&rd->lock);
	return 0;
}

DA_DEF_TYPE(size_t, size_t);

/* locking paired with rt_hosts_free due to dual owner of dpeer's mac */
int rt_dhosts_to_host(routing_t *rd, ether_addr_t src_mac,
		ether_addr_t dst_mac, struct rt_hosts **res)
{
	pthread_rwlock_rdlock(&rd->lock);
	int ret = -1;
	uint8_t *d = dst_mac.addr;
	uint8_t *s = src_mac.addr;
	DEBUG("sending from %02x:%02x:%02x:%02x:%02x:%02x to "
			"%02x:%02x:%02x:%02x:%02x:%02x",
			s[0],s[1],s[2],s[3],s[4],s[5],
			d[0],d[1],d[2],d[3],d[4],d[5]);

	if (rd->m_ct != rd->hosts.ct) {
		ret = compute_paths(rd);
		if (ret) {
			goto error_ret;
		}
	}

	struct _rt_host **cur = find_host_by_addr(&rd->hosts,
						rd->local->host->mac);

	if (!cur) {
		WARN("unable to locate self (serious).");
		ret = -13;
		goto error_ret;
	}

	size_t cur_i = host_to_index(rd, cur);

	if (ether_addr_is_mcast(&dst_mac)) {
		/* The following algorithm is used to determine to
		 *		whom the packet is to be forwarded:
		 *	determine the path from src to cur (path A)
		 *	any destination from src which travels completely
		 *		along path A is a destination cur needs
		 *		to send a packet to.
		 */
		/* find the path from src to cur */
		struct _rt_host **src = find_host_by_addr(&rd->hosts,
						src_mac);

		if (!src) {
			WARN("unable to locate src");
			ret = -2;
			goto error_ret;
		}

		size_t src_i = host_to_index(rd, src);

		da_t(size_t) src_to_cur;
		if (DA_INIT(&src_to_cur, 8)) {
			WARN("da init failed");
			ret = -33;
			goto error_ret;
		}

		if (rd->path[src_i][cur_i] == PATH_DISCON) {
			/* we are unable to reach ourselves from src,
			 * drop this packet. */
			ret = 0;
			goto error_ret;
		}

		/* follow the src to cur path */
		size_t next_i = src_i;
		for(;;) {
			next_i = rd->next[next_i][cur_i];
			if (next_i == NEXT_DISCON) {
				da_append(&src_to_cur, cur_i);
				break;
			}
			da_append(&src_to_cur, next_i);
		}

		/* for each destination from src_i, compare against
		 * the path src_to_cur */
		da_t(size_t) dsts;
		if (DA_INIT(&dsts, rd->m_ct)) {
			WARN("da init failed");
			ret = -34;
			goto error_ret;
		}

		size_t i;
		for(i = 0; i < rd->m_ct; i++) {
			size_t next_i = src_i;
			size_t j = 0;
			for(;;) {
				next_i = rd->next[next_i][i];
				if (next_i != src_to_cur.items[j]) {
					/* dst does  not pass through cur */
					break;
				}
				j++;
				if (j >= src_to_cur.ct) {
					/* success */
					da_append(&dsts, i);
					break;
				}

				if (next_i == NEXT_DISCON) {
					/* dst is on the path to cur. */
					break;
				}
			}
		}

		DA_DESTROY(&src_to_cur);

		struct rt_hosts *hs = NULL;
		struct rt_hosts **hp = &hs;
		for(i = 0; i < dsts.ct; i++) {
			*hp = malloc(sizeof(**hp));
			if (!*hp) {
				/* error */
			}

			(*hp)->addr = (*index_to_host(rd, dsts.items[i]))->host;
			(*hp)->next = NULL;
			hp = &((*hp)->next);
		}

		*res = hs;
		return 0;
	} else {
		struct _rt_host **dst = find_host_by_addr(&rd->hosts, dst_mac);

		if (!dst) {
			WARN("unable to locate destination");
			ret = -1;
			goto error_ret;
		}

		size_t dst_i = host_to_index(rd, dst);
		size_t next_i = rd->next[cur_i][dst_i];
		if (!rd->path[cur_i][dst_i]) {
			/* no path */
			*res = NULL;
			return 0;
		}

		struct _rt_host **next = index_to_host(rd, next_i);
		if (next_i == NEXT_DISCON) {
			/* direct route. */
			next = dst;
		} else {
			/* indirect route. */
			next = index_to_host(rd, next_i);
		}

		*res = malloc(sizeof(**res));
		if (!*res) {
			ret = -3;
			goto error_ret;
		}

		(*res)->addr = (*next)->host;
		(*res)->next = NULL;
		return 0;
	}

error_ret:
	*res = NULL;
	pthread_rwlock_unlock(&rd->lock);
	return ret;
}

void rt_hosts_free(routing_t *rd, struct rt_hosts *hosts)
{
	pthread_rwlock_unlock(&rd->lock);
	while(hosts != NULL) {
		struct rt_hosts *next = hosts->next;
		free(hosts);
		hosts = next;
	}
}

/**
 * rt_get_edges - gives the packed representation of the graph to the
 *                caller.
 * @rd            routing data from with the info is extracted
 * @edges         is set to the edges on success.
 * @e_ct          the count of edges (on success).
 *
 * return         negative on error. otherwise zero.
 */
int rt_get_edges(routing_t *rd, struct _pkt_edge **edges, size_t *e_ct)
{
	pthread_rwlock_rdlock(&rd->lock);
	*edges = rd->edges.items;
	*e_ct = rd->edges.ct;
	DEBUG("rt: gave edges %p ct %zu", *edges, *e_ct);
	return 0;
}

/**
 * rt_edges_free - informs routing that we no longer require the edges it
 *		   gave us
 * @rd		routing data
 * @edges	edges returned by rt_get_edges.
 * @e_ct	number of edges
 */
void rt_edges_free(routing_t *rd, struct _pkt_edge *edges, size_t e_ct)
{
	pthread_rwlock_unlock(&rd->lock);
}
