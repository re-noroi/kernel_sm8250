#ifndef _TCP_PLB_H
#define _TCP_PLB_H

#define TCP_PLB_SCALE 8

struct tcp_plb_state {
	u8 consec_cong_rounds:5,
	   unused:3;
	u32 pause_until;
};

static inline void tcp_plb_update_state(const struct sock *sk, struct tcp_plb_state *plb, const int cong_ratio)
{
}

static inline void tcp_plb_check_rehash(struct sock *sk, struct tcp_plb_state *plb)
{
}

static inline void tcp_plb_update_state_upon_rto(const struct sock *sk, struct tcp_plb_state *plb)
{
}

#endif /* _TCP_PLB_H */
