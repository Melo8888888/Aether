#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <net/tcp.h>
#include <linux/math64.h>

#define AETHER_GAMMA          5
#define AETHER_ALPHA_NUM      6
#define AETHER_ALPHA_DEN     10
#define AETHER_C_THRESH      35
#define AETHER_HIST_MAX       5

struct aether_sample {
	u32 qlen;
	u32 dwell;
};

struct aether {
	u8  bn_uplink;
	u8  draining;
	u8  active;
	u8  hist_count;
	u8  hist_idx;
	u8  _pad[3];
	u32 capacity;
	u32 prev_dwell;
	s32 prev_delta;
	u32 consec_neg;
	u32 consec_pos;
	u32 frozen_cwnd;
	u64 drain_start;
	struct aether_sample hist[AETHER_HIST_MAX];
};

static_assert(sizeof(struct aether) <= 104, "struct aether exceeds ICSK_CA_PRIV_SIZE");

static u32 sig_qlen;
static u32 sig_dwell;
static u32 sig_ts;
static DEFINE_SPINLOCK(sig_lock);
static struct proc_dir_entry *proc_entry;

static ssize_t proc_write(struct file *f, const char __user *buf,
			  size_t count, loff_t *pos)
{
	char kbuf[64];
	u32 q, d;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (sscanf(kbuf, "%u %u", &q, &d) != 2)
		return -EINVAL;

	spin_lock_bh(&sig_lock);
	sig_qlen = q;
	sig_dwell = d;
	sig_ts = tcp_jiffies32;
	spin_unlock_bh(&sig_lock);
	return count;
}

static ssize_t proc_read(struct file *f, char __user *buf,
			 size_t count, loff_t *pos)
{
	char kbuf[64];
	int len;

	if (*pos > 0)
		return 0;
	spin_lock_bh(&sig_lock);
	len = snprintf(kbuf, sizeof(kbuf), "%u %u\n", sig_qlen, sig_dwell);
	spin_unlock_bh(&sig_lock);
	if (len > count)
		return -EINVAL;
	if (copy_to_user(buf, kbuf, len))
		return -EFAULT;
	*pos += len;
	return len;
}

static const struct proc_ops aether_pops = {
	.proc_read  = proc_read,
	.proc_write = proc_write,
};

static void aether_get_signal(u32 *q, u32 *d, bool *fresh)
{
	spin_lock_bh(&sig_lock);
	*q = sig_qlen;
	*d = sig_dwell;
	*fresh = sig_ts != 0 &&
		 (tcp_jiffies32 - sig_ts) < msecs_to_jiffies(500);
	spin_unlock_bh(&sig_lock);
}

static void aether_init(struct sock *sk)
{
	struct aether *ca = inet_csk_ca(sk);

	*ca = (struct aether){};
	ca->bn_uplink = 0;
	tcp_sk(sk)->snd_ssthresh = TCP_INFINITE_SSTHRESH;
}

static u32 aether_ssthresh(struct sock *sk)
{
	return max_t(u32, tcp_sk(sk)->snd_cwnd >> 1, 2U);
}

static u32 aether_undo_cwnd(struct sock *sk)
{
	return tcp_sk(sk)->snd_cwnd;
}

static u32 estimate_capacity(struct aether *ca)
{
	u32 i, idx, valid = 0;
	u64 sum = 0;

	for (i = 0; i < min_t(u32, ca->hist_count, AETHER_HIST_MAX); i++) {
		idx = (ca->hist_idx + AETHER_HIST_MAX - 1 - i) % AETHER_HIST_MAX;
		if (ca->hist[idx].dwell > 0 && ca->hist[idx].qlen > 0) {
			sum += div64_u64((u64)ca->hist[idx].qlen * 1460 * 8000,
					 ca->hist[idx].dwell);
			valid++;
		}
	}
	if (valid > 0)
		ca->capacity = (u32)div64_u64(sum, valid);
	return ca->capacity;
}

static void aether_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct aether *ca = inet_csk_ca(sk);
	u32 qlen, dwell, rtt_ms;
	s32 delta;
	bool fresh;

	aether_get_signal(&qlen, &dwell, &fresh);

	if (!fresh || dwell == 0) {
		ca->active = 0;
		ca->bn_uplink = 0;
		tcp_reno_cong_avoid(sk, ack, acked);
		return;
	}

	rtt_ms = max_t(u32, (tp->srtt_us >> 3) / 1000, 1);
	delta = (s32)dwell - (s32)ca->prev_dwell;

	if (delta < 0) {
		ca->consec_neg++;
		ca->consec_pos = 0;
	} else if (delta > 0) {
		ca->consec_pos++;
		ca->consec_neg = 0;
	} else {
		ca->consec_neg = 0;
		ca->consec_pos = 0;
	}

	if (ca->bn_uplink) {
		if (ca->consec_neg >= AETHER_GAMMA &&
		    dwell * 100 / rtt_ms < 50) {
			ca->bn_uplink = 0;
			ca->consec_neg = 0;
		}
	} else {
		if (ca->consec_pos >= AETHER_GAMMA &&
		    dwell * 100 / rtt_ms > 50) {
			ca->bn_uplink = 1;
			ca->consec_pos = 0;
		}
	}

	ca->hist[ca->hist_idx] = (struct aether_sample){ qlen, dwell };
	ca->hist_idx = (ca->hist_idx + 1) % AETHER_HIST_MAX;
	if (ca->hist_count < AETHER_HIST_MAX)
		ca->hist_count++;

	if (ca->prev_delta > 0 &&
	    delta >= (s32)((u32)ca->prev_delta * AETHER_C_THRESH / 10)) {
		ca->draining = 1;
		ca->frozen_cwnd = tp->snd_cwnd;
	}

	ca->prev_dwell = dwell;
	ca->prev_delta = delta;

	if (!ca->bn_uplink) {
		ca->active = 0;
		tcp_reno_cong_avoid(sk, ack, acked);
		return;
	}

	if (ca->draining) {
		if (qlen == 0) {
			ca->draining = 0;
			tp->snd_cwnd = max_t(u32, ca->frozen_cwnd >> 1, 2U);
		}
		return;
	}

	ca->active = 1;
	estimate_capacity(ca);

	if (ca->capacity == 0) {
		tcp_reno_cong_avoid(sk, ack, acked);
		return;
	}

	{
		u32 rate = div64_u64((u64)tp->snd_cwnd * 1460 * 8 * 1000, rtt_ms);
		u32 new_cwnd;
		u64 v;

		if (ca->capacity >= rate) {
			u32 r = div64_u64((u64)ca->capacity * 1024, rate);
			r = clamp_t(u32, r, 1024, 2048);
			new_cwnd = (u32)div64_u64((u64)tp->snd_cwnd *
				   (1024 + ((r - 1024) * 717) / 1024), 1024);
		} else {
			u32 r = div64_u64((u64)rate * 1024, ca->capacity);
			r = clamp_t(u32, r, 1024, 2048);
			new_cwnd = (u32)div64_u64((u64)tp->snd_cwnd *
				   (1024 - ((r - 1024) * 717) / 1024), 1024);
		}

		if (rate > ca->capacity && dwell > 0) {
			v = (u64)(rate - ca->capacity) * dwell * AETHER_ALPHA_NUM;
			do_div(v, AETHER_ALPHA_DEN * 1000 * 8 * 1460);
			if (new_cwnd > (u32)v + 2)
				new_cwnd -= (u32)v;
			else
				new_cwnd = 2;
		}

		tp->snd_cwnd = clamp_t(u32, new_cwnd, 2U, tp->snd_cwnd_clamp);
	}
}

static struct tcp_congestion_ops tcp_aether __read_mostly = {
	.init       = aether_init,
	.ssthresh   = aether_ssthresh,
	.cong_avoid = aether_cong_avoid,
	.undo_cwnd  = aether_undo_cwnd,
	.owner      = THIS_MODULE,
	.name       = "aether",
};

static int __init aether_module_init(void)
{
	BUILD_BUG_ON(sizeof(struct aether) > ICSK_CA_PRIV_SIZE);
	proc_entry = proc_create("aether_signal", 0666, NULL, &aether_pops);
	if (!proc_entry)
		return -ENOMEM;
	return tcp_register_congestion_control(&tcp_aether);
}

static void __exit aether_module_exit(void)
{
	proc_remove(proc_entry);
	tcp_unregister_congestion_control(&tcp_aether);
}

module_init(aether_module_init);
module_exit(aether_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aether");
MODULE_DESCRIPTION("Aether: Congestion Control for Cellular Uplink");
