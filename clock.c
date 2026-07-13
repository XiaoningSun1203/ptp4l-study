/**
 * @file clock.c
 * @note Copyright (C) 2011 Richard Cochran <richardcochran@gmail.com>
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <errno.h>
#include <time.h>
#include <linux/net_tstamp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/queue.h>

#include "address.h"
#include "bmc.h"
#include "clock.h"
#include "clockadj.h"
#include "clockcheck.h"
#include "foreign.h"
#include "filter.h"
#include "missing.h"
#include "msg.h"
#include "phc.h"
#include "port.h"
#include "servo.h"
#include "stats.h"
#include "print.h"
#include "rtnl.h"
#include "tlv.h"
#include "tsproc.h"
#include "uds.h"
#include "util.h"

#define N_CLOCK_PFD (N_POLLFD + 1) /* one extra per port, for the fault timer */
#define POW2_41 ((double)(1ULL << 41))

struct interface {
	STAILQ_ENTRY(interface) list;
};

struct port {
	LIST_ENTRY(port) list;
};

struct freq_estimator {
	tmv_t origin1;
	tmv_t ingress1;
	unsigned int max_count;
	unsigned int count;
};

struct clock_stats {
	struct stats *offset;
	struct stats *freq;
	struct stats *delay;
	unsigned int max_count;
};

struct clock_subscriber {
	LIST_ENTRY(clock_subscriber) list;
	uint8_t events[EVENT_BITMASK_CNT];
	struct PortIdentity targetPortIdentity;
	struct address addr;
	UInteger16 sequenceId;
	time_t expiration;
};

struct clock {
	enum clock_type type;  // 这个实例是什么角色：OC/BC/TC（普通钟/边界钟/透明钟）
	struct config *config;  // 配置总表：所有你 conf/CLI 设置的东西都在这里
	clockid_t clkid;  // Linux 内核里的 clock 句柄.通常是 PHC（/dev/ptpX 的 clockid），或者 CLOCK_REALTIME，free-running 时可能无效。
	struct servo *servo;  // 锁定控制器对象：输入 offset/delay，输出调频/step 决策。
	enum servo_type servo_type;  // servo 是哪一种算法：比如 PI、linreg、ntpshm 等
	int (*dscmp)(struct dataset *a, struct dataset *b);  // 数据集比较函数指针：BMC 比较时用哪个规则（普通 1588 vs G.8275 telecom 比较）
	struct defaultDS dds;  // Default Dataset：clockIdentity、clockClass、priority1/2、domainNumber、twoStepFlag 等——最核心的“简历”。
	struct dataset default_dataset;  // 本地默认数据集的封装版本：通常用于统一比较/发布（实现细节上比 dds 更“通用结构”）。
	struct currentDS cur;  // Current Dataset：当前同步状态相关，比如 stepsRemoved、offsetFromMaster 等（“我现在跟 GM 的关系怎样”）
	struct parent_ds dad;  // Parent Dataset：我当前的“上游父节点/GM 是谁”的信息（被谁同步、上游属性）。
	struct timePropertiesDS tds;  // 时间属性：UTC offset、闰秒、timescale（TAI/PTP/UTC）等（时间语义）。  
	struct ClockIdentity ptl[PATH_TRACE_MAX];
	struct foreign_clock *best;  // 当前选出来的“最佳外来时钟”对象（收到的 Announce 们里最强的那个）。
	struct ClockIdentity best_id;  // best 的 clockIdentity（快速比对/记录）
	LIST_HEAD(ports_head, port) ports;
	struct port *uds_rw_port;
	struct port *uds_ro_port;
	struct pollfd *pollfd;
	int pollfd_valid;
	int nports; /* does not include the two UDS ports */
	int last_port_number;  // 端口编号分配器：新建 port 时用来给 portNumber。
	int sde;
	int free_running;  // 是否“只算不调”：free_running=1 时基本不去真正调整时钟（相当于只监听/统计）
	int freq_est_interval;  // 多久估一次频偏：用于频率估计/稳定性判断（让 servo 更平稳）。
	int local_sync_uncertain;  // 本地同步不确定性标志：遇到抖动/异常时告诉系统“我现在可能不靠谱”（不同版本用途略不同）
	int write_phase_mode;  // 用不用“相位直接写入”：某些 PHC 驱动支持 writephase，可以更直接地修相位（更快/更干净）
	int grand_master_capable; /* for 802.1AS only */
	int utc_timescale;
	int utc_offset_set;
	int leap_set;
	int kernel_leap;
	int utc_offset;
	int time_flags;  /* grand master role */
	int time_source; /* grand master role */
	UInteger8 clock_class_threshold;  // clockClass 门槛：选主/接受上游时的阈值策略（telecom profile 常用）。
	UInteger8 max_steps_removed;  // stepsRemoved 上限：路径太“长”的 GM 可能拒绝（避免层级太深）
	enum servo_state servo_state;  // 当前servo状态）
	enum timestamp_type timestamping;  // 时间戳模式：硬件/软件/legacy 硬件/1step/2step。
	tmv_t master_offset;  // 我比 master 差多少（offset，servo 最核心输入）
	tmv_t path_delay;  // 链路时延估计（delay，用于校正 offset）
	tmv_t ingress_ts;  // 最近一次收到包的入口时间戳（你可以理解成“这次测量的本地时间点”）。
	tmv_t initial_delay;  // 启动时的初始 delay（在还没估出来稳定 delay 前先用一个“垫底值”）
	struct tsproc *tsproc;  // 时间戳处理器：对 raw timestamp 做滤波、去毛刺、算 delay，给 servo 提供更干净的输入。
	struct freq_estimator fest;  // 频率估计器的内部状态：servo 或统计用它来估“我现在快慢多少”。
	struct time_status_np status;
	double master_local_rr; /* maintained when free_running */
	double nrr;
	struct clock_description desc;
	struct clock_stats stats;
	int stats_interval;
	struct clockcheck *sanity_check;
	struct interface *uds_rw_if;
	struct interface *uds_ro_if;
	LIST_HEAD(clock_subscribers_head, clock_subscriber) subscribers;
	struct monitor *slave_event_monitor;
	int step_window_counter;  // 启动后前 N 个 sync 里允许大步跳一下，过了窗口就尽量只 slewing（调频慢慢追）
	int step_window;
	struct ptp_source_status_list aml;  // PTP 源状态列表,保存目前看到的候选源/状态（用于管理查询、监控、切换决策等）
};

struct clock the_clock;

static void handle_state_decision_event(struct clock *c);
static int clock_resize_pollfd(struct clock *c, int new_nports);
static void clock_remove_port(struct clock *c, struct port *p);
static void clock_stats_display(struct clock_stats *s);

static void remove_subscriber(struct clock_subscriber *s)
{
	LIST_REMOVE(s, list);
	free(s);
}

static void clock_update_subscription(struct clock *c, struct ptp_message *req,
				      uint8_t *bitmask, uint16_t duration)
{
	struct clock_subscriber *s, *tmp;
	struct timespec now;
	int i, remove = 1;

	for (i = 0; i < EVENT_BITMASK_CNT; i++) {
		if (bitmask[i]) {
			remove = 0;
			break;
		}
	}

	LIST_FOREACH_SAFE(s, &c->subscribers, list, tmp) {
		if (pid_eq(&s->targetPortIdentity,
			   &req->header.sourcePortIdentity)) {
			if (!remove) {
				/* Update transport address and event mask. */
				s->addr = req->address;
				memcpy(s->events, bitmask, EVENT_BITMASK_CNT);
				clock_gettime(CLOCK_MONOTONIC, &now);
				s->expiration = now.tv_sec + duration;
			} else {
				remove_subscriber(s);
			}
			return;
		}
	}
	if (remove)
		return;
	/* Not present yet, add the subscriber. */
	s = malloc(sizeof(*s));
	if (!s) {
		pr_err("failed to allocate memory for a subscriber");
		return;
	}
	s->targetPortIdentity = req->header.sourcePortIdentity;
	s->addr = req->address;
	memcpy(s->events, bitmask, EVENT_BITMASK_CNT);
	clock_gettime(CLOCK_MONOTONIC, &now);
	s->expiration = now.tv_sec + duration;
	s->sequenceId = 0;
	LIST_INSERT_HEAD(&c->subscribers, s, list);
}

static void clock_get_subscription(struct clock *c, struct ptp_message *req,
				   uint8_t *bitmask, uint16_t *duration)
{
	struct clock_subscriber *s;
	struct timespec now;

	LIST_FOREACH(s, &c->subscribers, list) {
		if (pid_eq(&s->targetPortIdentity,
			   &req->header.sourcePortIdentity)) {
			memcpy(bitmask, s->events, EVENT_BITMASK_CNT);
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (s->expiration < now.tv_sec)
				*duration = 0;
			else
				*duration = s->expiration - now.tv_sec;
			return;
		}
	}
	/* A client without entry means the client has no subscriptions. */
	memset(bitmask, 0, EVENT_BITMASK_CNT);
	*duration = 0;
}

static void clock_flush_subscriptions(struct clock *c)
{
	struct clock_subscriber *s, *tmp;

	LIST_FOREACH_SAFE(s, &c->subscribers, list, tmp) {
		remove_subscriber(s);
	}
}

static void clock_prune_subscriptions(struct clock *c)
{
	struct clock_subscriber *s, *tmp;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	LIST_FOREACH_SAFE(s, &c->subscribers, list, tmp) {
		if (s->expiration <= now.tv_sec) {
			pr_info("subscriber %s timed out",
				pid2str(&s->targetPortIdentity));
			remove_subscriber(s);
		}
	}
}

void clock_send_notification(struct clock *c, struct ptp_message *msg,
			     enum notification event)
{
	struct port *uds = c->uds_rw_port;
	struct clock_subscriber *s;

	LIST_FOREACH(s, &c->subscribers, list) {
		if (!event_bitmask_get(s->events, event))
			continue;
		/* send event */
		msg->header.sequenceId = htons(s->sequenceId);
		s->sequenceId++;
		msg->management.targetPortIdentity.clockIdentity =
			s->targetPortIdentity.clockIdentity;
		msg->management.targetPortIdentity.portNumber =
			htons(s->targetPortIdentity.portNumber);
		msg->address = s->addr;
		port_forward_to(uds, msg);
	}
}

void clock_destroy(struct clock *c)
{
	struct port *p, *tmp;

	interface_destroy(c->uds_rw_if);
	interface_destroy(c->uds_ro_if);
	clock_flush_subscriptions(c);
	LIST_FOREACH_SAFE(p, &c->ports, list, tmp) {
		clock_remove_port(c, p);
	}
	monitor_destroy(c->slave_event_monitor);
	port_close(c->uds_rw_port);
	port_close(c->uds_ro_port);
	free(c->pollfd);
	if (c->clkid != CLOCK_REALTIME) {
		phc_close(c->clkid);
	}
	servo_destroy(c->servo);
	tsproc_destroy(c->tsproc);
	stats_destroy(c->stats.offset);
	stats_destroy(c->stats.freq);
	stats_destroy(c->stats.delay);
	if (c->sanity_check) {
		clockcheck_destroy(c->sanity_check);
	}
	memset(c, 0, sizeof(*c));
	msg_cleanup();
	tc_cleanup();
}

static int clock_fault_timeout(struct port *port, int set)
{
	struct fault_interval i;

	if (!set) {
		pr_debug("clearing fault on %s", port_log_name(port));
		return port_set_fault_timer_lin(port, 0);
	}

	fault_interval(port, last_fault_type(port), &i);

	if (i.type == FTMO_LINEAR_SECONDS) {
		pr_debug("waiting %d seconds to clear fault on %s",
			 i.val, port_log_name(port));
		return port_set_fault_timer_lin(port, i.val);
	} else if (i.type == FTMO_LOG2_SECONDS) {
		pr_debug("waiting 2^{%d} seconds to clear fault on %s",
			 i.val, port_log_name(port));
		return port_set_fault_timer_log(port, 1, i.val);
	}

	pr_err("Unsupported fault interval type %d", i.type);
	return -1;
}

static void clock_freq_est_reset(struct clock *c)
{
	c->fest.origin1 = tmv_zero();
	c->fest.ingress1 = tmv_zero();
	c->fest.count = 0;
}

static void clock_management_send_error(struct port *p,
					struct ptp_message *msg, int error_id)
{
	if (port_management_error(port_identity(p), p, msg, error_id))
		pr_err("failed to send management error status");
}

/* The 'p' and 'req' paremeters are needed for the GET actions that operate
 * on per-client datasets. If such actions do not apply to the caller, it is
 * allowed to pass both of them as NULL.
 */
static int clock_management_fill_response(struct clock *c, struct port *p,
					  struct ptp_message *req,
					  struct ptp_message *rsp, int id)
{
	struct grandmaster_settings_np *gsn;
	struct management_tlv_datum *mtd;
	struct subscribe_events_np *sen;
	struct management_tlv *tlv;
	struct time_status_np *tsn;
	struct tlv_extra *extra;
	struct PTPText *text;
	int datalen = 0;

	extra = tlv_extra_alloc();
	if (!extra) {
		pr_err("failed to allocate TLV descriptor");
		return 0;
	}
	extra->tlv = (struct TLV *) rsp->management.suffix;

	tlv = (struct management_tlv *) rsp->management.suffix;
	tlv->type = TLV_MANAGEMENT;
	tlv->id = id;

	switch (id) {
	case TLV_USER_DESCRIPTION:
		text = (struct PTPText *) tlv->data;
		text->length = c->desc.userDescription.length;
		memcpy(text->text, c->desc.userDescription.text, text->length);
		datalen = 1 + text->length;
		break;
	case TLV_DEFAULT_DATA_SET:
		memcpy(tlv->data, &c->dds, sizeof(c->dds));
		datalen = sizeof(c->dds);
		break;
	case TLV_CURRENT_DATA_SET:
		memcpy(tlv->data, &c->cur, sizeof(c->cur));
		datalen = sizeof(c->cur);
		break;
	case TLV_PARENT_DATA_SET:
		memcpy(tlv->data, &c->dad.pds, sizeof(c->dad.pds));
		datalen = sizeof(c->dad.pds);
		break;
	case TLV_PTP_SOURCE_STATUS_LIST:
		memcpy(tlv->data, &c->aml, sizeof(c->aml));
		datalen = sizeof(c->aml);
		//pr_info("BR-DEBUG: get GET message from clock_management_fill_response");
    	//pr_info("BR-DEBUG: pri1=%d, flag0=%d, flag1=%d, stepsRemoved=%d", c->aml.pri1, c->aml.annMsgFlagField[0], c->aml.annMsgFlagField[1], c->aml.stepsRemoved);
	    //pr_info("BR-DEBUG: pri2=%d, currentUtcOffset=%d", c->aml.pri2, c->aml.currentUtcOffset);
		break;
	case TLV_TIME_PROPERTIES_DATA_SET:
		memcpy(tlv->data, &c->tds, sizeof(c->tds));
		datalen = sizeof(c->tds);
		break;
	case TLV_PRIORITY1:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->dds.priority1;
		datalen = sizeof(*mtd);
		break;
	case TLV_PRIORITY2:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->dds.priority2;
		datalen = sizeof(*mtd);
		break;
	case TLV_DOMAIN:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->dds.domainNumber;
		datalen = sizeof(*mtd);
		break;
	case TLV_SLAVE_ONLY:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->dds.flags & DDS_SLAVE_ONLY;
		datalen = sizeof(*mtd);
		break;
	case TLV_CLOCK_ACCURACY:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->dds.clockQuality.clockAccuracy;
		datalen = sizeof(*mtd);
		break;
	case TLV_TRACEABILITY_PROPERTIES:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->tds.flags & (TIME_TRACEABLE|FREQ_TRACEABLE);
		datalen = sizeof(*mtd);
		break;
	case TLV_TIMESCALE_PROPERTIES:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->tds.flags & PTP_TIMESCALE;
		datalen = sizeof(*mtd);
		break;
	case TLV_TIME_STATUS_NP:
		tsn = (struct time_status_np *) tlv->data;
		tsn->master_offset = tmv_to_nanoseconds(c->master_offset);
		tsn->ingress_time = tmv_to_nanoseconds(c->ingress_ts);
		tsn->cumulativeScaledRateOffset =
			(Integer32) (c->status.cumulativeScaledRateOffset +
				      c->nrr * POW2_41 - POW2_41);
		tsn->scaledLastGmPhaseChange = c->status.scaledLastGmPhaseChange;
		tsn->gmTimeBaseIndicator = c->status.gmTimeBaseIndicator;
		tsn->lastGmPhaseChange = c->status.lastGmPhaseChange;
		if (cid_eq(&c->dad.pds.grandmasterIdentity, &c->dds.clockIdentity))
			tsn->gmPresent = 0;
		else
			tsn->gmPresent = 1;
		tsn->gmIdentity = c->dad.pds.grandmasterIdentity;
		datalen = sizeof(*tsn);
		break;
	case TLV_GRANDMASTER_SETTINGS_NP:
		gsn = (struct grandmaster_settings_np *) tlv->data;
		gsn->clockQuality = c->dds.clockQuality;
		gsn->utc_offset = c->utc_offset;
		gsn->time_flags = c->time_flags;
		gsn->time_source = c->time_source;
		datalen = sizeof(*gsn);
		break;
	case TLV_SUBSCRIBE_EVENTS_NP:
		if (p != c->uds_rw_port) {
			/* Only the UDS-RW port allowed. */
			break;
		}
		sen = (struct subscribe_events_np *)tlv->data;
		clock_get_subscription(c, req, sen->bitmask, &sen->duration);
		datalen = sizeof(*sen);
		break;
	case TLV_SYNCHRONIZATION_UNCERTAIN_NP:
		mtd = (struct management_tlv_datum *) tlv->data;
		mtd->val = c->local_sync_uncertain;
		datalen = sizeof(*mtd);
		break;
	default:
		/* The caller should *not* respond to this message. */
		tlv_extra_recycle(extra);
		return 0;
	}
	if (datalen % 2) {
		tlv->data[datalen] = 0;
		datalen++;
	}
	tlv->length = sizeof(tlv->id) + datalen;
	rsp->header.messageLength += sizeof(*tlv) + datalen;
	msg_tlv_attach(rsp, extra);

	/* The caller can respond to this message. */
	return 1;
}

static int clock_management_get_response(struct clock *c, struct port *p,
					 int id, struct ptp_message *req)
{
	struct PortIdentity pid = port_identity(p);
	struct ptp_message *rsp;
	int respond;

	rsp = port_management_reply(pid, p, req);
	if (!rsp) {
		return 0;
	}
	respond = clock_management_fill_response(c, p, req, rsp, id);
	if (respond)
		port_prepare_and_send(p, rsp, TRANS_GENERAL);
	msg_put(rsp);
	return respond;
}

static int clock_management_set(struct clock *c, struct port *p,
				int id, struct ptp_message *req, int *changed)
{
	int respond = 0;
	struct management_tlv *tlv;
	struct management_tlv_datum *mtd;
	struct grandmaster_settings_np *gsn;
	struct subscribe_events_np *sen;

	tlv = (struct management_tlv *) req->management.suffix;

	switch (id) {
	case TLV_PRIORITY1:
		mtd = (struct management_tlv_datum *) tlv->data;
		c->dds.priority1 = mtd->val;
		*changed = 1;
		respond = 1;
		break;
	case TLV_PRIORITY2:
		mtd = (struct management_tlv_datum *) tlv->data;
		c->dds.priority2 = mtd->val;
		*changed = 1;
		respond = 1;
		break;
	case TLV_GRANDMASTER_SETTINGS_NP:
		gsn = (struct grandmaster_settings_np *) tlv->data;
		c->dds.clockQuality = gsn->clockQuality;
		c->utc_offset = gsn->utc_offset;
		c->time_flags = gsn->time_flags;
		c->time_source = gsn->time_source;
		*changed = 1;
		respond = 1;
		break;
	case TLV_SUBSCRIBE_EVENTS_NP:
		sen = (struct subscribe_events_np *)tlv->data;
		clock_update_subscription(c, req, sen->bitmask, sen->duration);
		respond = 1;
		break;
	case TLV_SYNCHRONIZATION_UNCERTAIN_NP:
		mtd = (struct management_tlv_datum *) tlv->data;
		switch (mtd->val) {
		case SYNC_UNCERTAIN_DONTCARE:
		case SYNC_UNCERTAIN_FALSE:
		case SYNC_UNCERTAIN_TRUE:
			/* Display stats on change of local_sync_uncertain */
			if (c->local_sync_uncertain != mtd->val
			    && stats_get_num_values(c->stats.offset))
				clock_stats_display(&c->stats);
			c->local_sync_uncertain = mtd->val;
			respond = 1;
			break;
		}
		break;
	}
	if (respond && !clock_management_get_response(c, p, id, req))
		pr_err("failed to send management set response");
	return respond ? 1 : 0;
}

static void clock_stats_update(struct clock_stats *s,
			       double offset, double freq)
{
	stats_add_value(s->offset, offset);
	stats_add_value(s->freq, freq);

	if (freq != 0)//Z
		pr_debug("offset|freq:%6.0f|%6.0f", offset, freq);
	else
		pr_debug("offset:%6.0f", offset);

	if (stats_get_num_values(s->offset) < s->max_count)
		return;

	clock_stats_display(s);
}

static void clock_stats_display(struct clock_stats *s)
{
	struct stats_result offset_stats, freq_stats, delay_stats;

	stats_get_result(s->offset, &offset_stats);
	stats_get_result(s->freq, &freq_stats);
	static uint32_t printCountWhenLock = 0;
	static int _printEn = -1;
	const uint32_t maxPrintTimes = 20;

	if (_printEn == -1)//only get ont time
	{
		_printEn = config_get_int(the_clock.config, NULL, "printEn");
	}

	bool enNeedPrint = false;
	if (!_printEn)//only print 20 times after max_abs less than 100.
	{
		if (printCountWhenLock == 0)
		{
			if (offset_stats.max_abs < 100)
			{
				printCountWhenLock = 1;
			}
		}
		else
		{
			if (offset_stats.max_abs < 100)
			{
				if (printCountWhenLock < maxPrintTimes)
				{
					printCountWhenLock++;
				}
			}
			else
			{
				printCountWhenLock = 0;
			}
		}

		if (printCountWhenLock < maxPrintTimes)
		{
			enNeedPrint = true;
		}
	}
	else
	{
		enNeedPrint = true;
	}

	if (enNeedPrint)
	{
		/* Path delay stats are updated separately, they may be empty. */
		if (!stats_get_result(s->delay, &delay_stats)) {
			pr_info("rms %4.0f max %4.0f "
				"freq %+6.0f +/- %3.0f "
				"delay %5.0f +/- %3.0f ",
				offset_stats.rms, offset_stats.max_abs,
				freq_stats.mean, freq_stats.stddev,
				delay_stats.mean, delay_stats.stddev);
		} else {
			pr_info("rms %4.0f max %4.0f "
				"freq %+6.0f +/- %3.0f ",
				offset_stats.rms, offset_stats.max_abs,
				freq_stats.mean, freq_stats.stddev);
		}
	}

	stats_reset(s->offset);
	stats_reset(s->freq);
	stats_reset(s->delay);
}

static enum servo_state clock_no_adjust(struct clock *c, tmv_t ingress,
					tmv_t origin)
{
	struct freq_estimator *f = &c->fest;
	double freq, fui, ratio;
	enum servo_state state;

	if (c->local_sync_uncertain == SYNC_UNCERTAIN_FALSE) {
		state = SERVO_LOCKED;
	} else {
		state = SERVO_UNLOCKED;
	}

	/*
	 * The ratio of the local clock freqency to the master clock
	 * is estimated by:
	 *
	 *    (ingress_2 - ingress_1) / (origin_2 - origin_1)
	 *
	 * Both of the origin time estimates include the path delay,
	 * but we assume that the path delay is in fact constant.
	 * By leaving out the path delay altogther, we can avoid the
	 * error caused by our imperfect path delay measurement.
	 */
	if (tmv_is_zero(f->ingress1)) {
		f->ingress1 = ingress;
		f->origin1 = origin;
		return state;
	}
	f->count++;
	if (f->count < f->max_count) {
		return state;
	}
	if (tmv_cmp(ingress, f->ingress1) == 0) {
		pr_warning("bad timestamps in rate ratio calculation");
		return state;
	}

	ratio = tmv_dbl(tmv_sub(origin, f->origin1)) /
		tmv_dbl(tmv_sub(ingress, f->ingress1));
	freq = (1.0 - ratio) * 1e9;

	if (c->stats.max_count > 1) {
		clock_stats_update(&c->stats, tmv_dbl(c->master_offset), freq);
	} else {
		pr_info("master offset %10" PRId64 " s%d freq %+7.0f "
			"path delay %9" PRId64,
			tmv_to_nanoseconds(c->master_offset), state, freq,
			tmv_to_nanoseconds(c->path_delay));
	}

	fui = 1.0 + (c->status.cumulativeScaledRateOffset + 0.0) / POW2_41;

	pr_debug("peer/local    %.9f", c->nrr);
	pr_debug("fup_info      %.9f", fui);
	pr_debug("product       %.9f", fui * c->nrr);
	pr_debug("sum-1         %.9f", fui + c->nrr - 1.0);
	pr_debug("master/local  %.9f", ratio);
	pr_debug("diff         %+.9f", ratio - (fui + c->nrr - 1.0));

	f->ingress1 = ingress;
	f->origin1 = origin;
	f->count = 0;

	c->master_local_rr = ratio;

	return state;
}

static void clock_update_grandmaster(struct clock *c)
{
	struct parentDS *pds = &c->dad.pds;
	memset(&c->cur, 0, sizeof(c->cur));
	memset(c->ptl, 0, sizeof(c->ptl));
	pds->parentPortIdentity.clockIdentity   = c->dds.clockIdentity;
	pds->parentPortIdentity.portNumber      = 0;
	pds->grandmasterIdentity                = c->dds.clockIdentity;
	pds->grandmasterClockQuality            = c->dds.clockQuality;
	pds->grandmasterPriority1               = c->dds.priority1;
	pds->grandmasterPriority2               = c->dds.priority2;
	c->dad.path_length                      = 0;
	c->tds.currentUtcOffset                 = c->utc_offset;
	c->tds.flags                            = c->time_flags;
	c->tds.timeSource                       = c->time_source;
}

static void clock_update_slave(struct clock *c)
{
	struct parentDS *pds = &c->dad.pds;
	struct ptp_message *msg;

	if (!c->best)
		return;

	msg                            = TAILQ_FIRST(&c->best->messages);
	c->cur.stepsRemoved            = 1 + c->best->dataset.stepsRemoved;
	pds->parentPortIdentity        = c->best->dataset.sender;
	pds->grandmasterIdentity       = msg->announce.grandmasterIdentity;
	pds->grandmasterClockQuality   = msg->announce.grandmasterClockQuality;
	pds->grandmasterPriority1      = msg->announce.grandmasterPriority1;
	pds->grandmasterPriority2      = msg->announce.grandmasterPriority2;
	c->tds.currentUtcOffset        = msg->announce.currentUtcOffset;
	c->tds.flags                   = msg->header.flagField[1];
	c->tds.timeSource              = msg->announce.timeSource;
	if (!(c->tds.flags & PTP_TIMESCALE)) {
		pr_warning("foreign master not using PTP timescale");
	}
	if (c->tds.currentUtcOffset < c->utc_offset) {
		pr_warning("running in a temporal vortex");
	}
	if ((c->tds.flags & UTC_OFF_VALID && c->tds.flags & TIME_TRACEABLE) ||
	    (c->tds.currentUtcOffset > c->utc_offset)) {
		pr_info("updating UTC offset to %d", c->tds.currentUtcOffset);
		c->utc_offset = c->tds.currentUtcOffset;
	}
}

static int clock_utc_correct(struct clock *c, tmv_t ingress)
{
	struct timespec offset;
	int utc_offset, leap, clock_leap;
	uint64_t ts;

	if (!c->utc_timescale)
		return 0;

	utc_offset = c->utc_offset;

	if (c->tds.flags & LEAP_61) {
		leap = 1;
	} else if (c->tds.flags & LEAP_59) {
		leap = -1;
	} else {
		leap = 0;
	}

	/* Handle leap seconds. */
	if ((leap || c->leap_set) && c->clkid == CLOCK_REALTIME) {
		/* If the clock will be stepped, the time stamp has to be the
		   target time. Ignore possible 1 second error in utc_offset. */
		if (c->servo_state == SERVO_UNLOCKED) {
			ts = tmv_to_nanoseconds(tmv_sub(ingress,
							c->master_offset));
			if (c->tds.flags & PTP_TIMESCALE)
				ts -= utc_offset * NS_PER_SEC;
		} else {
			ts = tmv_to_nanoseconds(ingress);
		}

		/* Suspend clock updates in the last second before midnight. */
		if (is_utc_ambiguous(ts)) {
			pr_info("clock update suspended due to leap second");
			return -1;
		}

		clock_leap = leap_second_status(ts, c->leap_set,
						&leap, &utc_offset);
		if (c->leap_set != clock_leap) {
			if (c->kernel_leap)
				sysclk_set_leap(clock_leap);
			else
				servo_leap(c->servo, clock_leap);
			c->leap_set = clock_leap;
		}
	}

	/* Update TAI-UTC offset of the system clock if valid and traceable. */
	if (c->tds.flags & UTC_OFF_VALID && c->tds.flags & TIME_TRACEABLE &&
	    c->utc_offset_set != utc_offset && c->clkid == CLOCK_REALTIME) {
		sysclk_set_tai_offset(utc_offset);
		c->utc_offset_set = utc_offset;
	}

	if (!(c->tds.flags & PTP_TIMESCALE))
		return 0;

	offset.tv_sec = utc_offset;
	offset.tv_nsec = 0;
	/* Local clock is UTC, but master is TAI. */
	c->master_offset = tmv_add(c->master_offset, timespec_to_tmv(offset));
	return 0;
}

static int forwarding(struct clock *c, struct port *p)
{
	enum port_state ps = port_state(p);

	if (p == c->uds_ro_port)
		return 0;

	switch (ps) {
	case PS_MASTER:
	case PS_GRAND_MASTER:
	case PS_SLAVE:
	case PS_UNCALIBRATED:
	case PS_PRE_MASTER:
		return 1;
	default:
		break;
	}
	if (p == c->uds_rw_port && ps != PS_FAULTY) {
		return 1;
	}
	return 0;
}

/* public methods */

UInteger8 clock_class(struct clock *c)
{
	return c->dds.clockQuality.clockClass;
}

struct config *clock_config(struct clock *c)
{
	return c->config;
}

int (*clock_dscmp(struct clock *c))(struct dataset *a, struct dataset *b)
{
	return c->dscmp;
}

struct currentDS *clock_current_dataset(struct clock *c)
{
	return &c->cur;
}

// 创建一个 struct port
// 打开 socket（L2/L4）
// 绑定接口（eth0）
// 设置时间戳模式
// 注册定时器
// 加入 pollfd
// 关联回这个 clock
static int clock_add_port(struct clock *c, const char *phc_device,
			  int phc_index, enum timestamp_type timestamping,
			  struct interface *iface)
{
	struct port *p, *piter, *lastp = NULL;

	if (clock_resize_pollfd(c, c->nports + 2)) {
		return -1;
	}
	p = port_open(phc_device, phc_index, timestamping,
		      ++c->last_port_number, iface, c);
	if (!p) {
		/* No need to shrink pollfd */
		return -1;
	}
	LIST_FOREACH(piter, &c->ports, list) {
		lastp = piter;
	}
	if (lastp) {
		LIST_INSERT_AFTER(lastp, p, list);
	} else {
		LIST_INSERT_HEAD(&c->ports, p, list);
	}
	c->nports++;
	clock_fda_changed(c);

	return 0;
}

static void clock_remove_port(struct clock *c, struct port *p)
{
	/* Do not call clock_resize_pollfd, it's pointless to shrink
	 * the allocated memory at this point, clock_destroy will free
	 * it all anyway. This function is usable from other parts of
	 * the code, but even then we don't mind if pollfd is larger
	 * than necessary. */
	LIST_REMOVE(p, list);
	c->nports--;
	clock_fda_changed(c);
	port_close(p);
}

int clock_required_modes(struct clock *c)
{
	int required_modes = 0;

	switch (c->timestamping) {
	case TS_SOFTWARE:
		required_modes |= SOF_TIMESTAMPING_TX_SOFTWARE |
			SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE;
		break;
	case TS_LEGACY_HW:
		required_modes |= SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_SYS_HARDWARE;
		break;
	case TS_HARDWARE:
	case TS_ONESTEP:
	case TS_P2P1STEP:
		required_modes |= SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;
		break;
	default:
		break;
	}

	return required_modes;
}

// clock_create() 的作用就是：
// 把“配置”变成一个可以运行的 PTP 时钟实例(ptp4l 进程里通常只有一个 clock 实例)：选定要锁的时钟（PHC/系统时钟）、创建 servo/tsproc、创建所有端口（含 UDS 管理端口）,
// 然后触发端口状态机开始运行.
// 确定类型 → 填默认数据集 → 校验网卡时间戳能力 → 选 PHC → 打开时钟并建 servo → 建 tsproc/stats → 建 UDS 管理端口 → 建业务端口 → 触发端口状态机初始化.
struct clock *clock_create(enum clock_type type, struct config *config,
			   const char *phc_device)
{
// 1) 选 servo 类型 + 一些局部变量
	enum servo_type servo = config_get_int(config, NULL, "clock_servo");
	char ts_label[IF_NAMESIZE], phc[32], *tmp;
	enum timestamp_type timestamping;
	int fadj = 0, max_adj = 0, sw_ts;
	int phc_index, required_modes = 0;
	struct clock *c = &the_clock;
	const char *uds_ifname;
	struct port *p;
	unsigned char oui[OUI_LEN];
	struct interface *iface;
	struct timespec ts;
	int sfl;

//step2: 初始化随机数 + 如果已有 ports 就先销毁（重建场景）
	clock_gettime(CLOCK_REALTIME, &ts);
	// 避免多个 PTP 实例行为完全同步导致的网络冲突和选主震荡。
	srandom(ts.tv_sec ^ ts.tv_nsec);  // 目的：避免“所有设备同一时刻做同一件事”。当前时间秒XOR当前时间纳秒，保证不同进程不一样。random 不参与时间计算，不影响锁定精度。

	if (c->nports) {
		clock_destroy(c);
	}

//step3: 校验并设置 clock 类型(OC/BC/...)
	switch (type) {
	case CLOCK_TYPE_ORDINARY:
	case CLOCK_TYPE_BOUNDARY:
	case CLOCK_TYPE_P2P:
	case CLOCK_TYPE_E2E:
		c->type = type;
		break;
	case CLOCK_TYPE_MANAGEMENT:
		return NULL;
	}

//step4: 初始化 defaultDS(Default Data Set).
    //defaultDS = 我这台 PTP 设备的“个人档案”,用来告诉网络里的其他设备：“我是谁？我多准？我优先级多少？我能不能当 GM？”
	// 	这些字段直接参与 BMC 比较（时钟源选择）：
	//  clockClass/accuracy/variance/priority1/priority2/localPriority 是 announce 里带出去的“我的档案”。
	//  domainNumber 是 PTP 的“隔离编号”，相同 domain 才能选主和同步。
	//  clock_class_threshold/maxStepsRemoved 会影响选主/可接受 GM 的规则（看你们配置）。
	/* Initialize the defaultDS（Default Data Set）. */
	c->dds.clockQuality.clockClass =
		config_get_int(config, NULL, "clockClass");
	c->dds.clockQuality.clockAccuracy =
		config_get_int(config, NULL, "clockAccuracy");
	c->dds.clockQuality.offsetScaledLogVariance =
		config_get_int(config, NULL, "offsetScaledLogVariance");

	c->desc.productDescription.max_symbols = 64;
	c->desc.revisionData.max_symbols = 32;
	c->desc.userDescription.max_symbols = 128;

	tmp = config_get_string(config, NULL, "productDescription");
	if (count_char(tmp, ';') != 2 ||
	    static_ptp_text_set(&c->desc.productDescription, tmp)) {
		pr_err("invalid productDescription '%s'", tmp);
		return NULL;
	}
	tmp = config_get_string(config, NULL, "revisionData");
	if (count_char(tmp, ';') != 2 ||
	    static_ptp_text_set(&c->desc.revisionData, tmp)) {
		pr_err("invalid revisionData '%s'", tmp);
		return NULL;
	}
	tmp = config_get_string(config, NULL, "userDescription");
	if (static_ptp_text_set(&c->desc.userDescription, tmp)) {
		pr_err("invalid userDescription '%s'", tmp);
		return NULL;
	}
	tmp = config_get_string(config, NULL, "manufacturerIdentity");
	if (OUI_LEN != sscanf(tmp, "%hhx:%hhx:%hhx", &oui[0], &oui[1], &oui[2])) {
		pr_err("invalid manufacturerIdentity '%s'", tmp);
		return NULL;
	}
	memcpy(c->desc.manufacturerIdentity, oui, OUI_LEN);

	c->dds.domainNumber = config_get_int(config, NULL, "domainNumber");

	// 4-2) 处理 clientOnly / gmCapable（决定你能不能当 GM）
	//  这段直接影响“时钟源寻找”：你会看到 BMC 算法用这些字段来比较。
	if (config_get_int(config, NULL, "clientOnly")) {  // 	clientOnly：你永远不参选 GM（只当 slave）。
		c->dds.flags |= DDS_SLAVE_ONLY;
	}
	if (!config_get_int(config, NULL, "gmCapable") &&  //  gmCapable：802.1AS 语义里是否允许做 GM。
	    c->dds.flags & DDS_SLAVE_ONLY) {
		pr_err("Cannot mix 1588 clientOnly with 802.1AS !gmCapable");
		return NULL;
	}
	if (!config_get_int(config, NULL, "gmCapable") ||
	    c->dds.flags & DDS_SLAVE_ONLY) {
		c->dds.clockQuality.clockClass = 255;  //  如果不能当 GM，会把 clockClass 设成 255（基本等于“别选我当 GM”）。
	}
	c->default_dataset.localPriority =
		config_get_int(config, NULL, "G.8275.defaultDS.localPriority");
	c->max_steps_removed = config_get_int(config, NULL,"maxStepsRemoved");
	c->clock_class_threshold = config_get_int(config, NULL, "clock_class_threshold");

    // 4-3) 确保 “one-step / two-step 模式” 和 “时间戳方式（硬件/软件）” 是一致且可行的。保证配置不会自相矛盾.
	// Two-step（最常见）:几乎所有硬件支持. 'twoStepFlag'==1,可以不用管'time_stamping'是hardware还是software.
	// One-step:必须硬件支持 on-the-fly 时间戳修改. 'twoStepFlag'==0,'time_stamping'不能是software.
	/* Harmonize the 'twoStepFlag' with the 'time_stamping' option. */
	if (config_harmonize_onestep(config)) {
		return NULL;
	}
	if (config_get_int(config, NULL, "twoStepFlag")) {
		c->dds.flags |= DDS_TWO_STEP_FLAG;
	}
	timestamping = config_get_int(config, NULL, "time_stamping");
	if (timestamping == TS_SOFTWARE) {
		sw_ts = 1;
	} else {
		sw_ts = 0;
	}

	c->dds.priority1 = config_get_int(config, NULL, "priority1");
	c->dds.priority2 = config_get_int(config, NULL, "priority2");

//step5: 检查每个接口是否支持你要求的 timestamp 模式
	// 	这一段是“启动前把坑踩完”：
	// 通过 ethtool/netlink 拿每块网卡的时间戳能力（硬件/软件/one-step 等）。
	// 如果任何接口不支持你要求的模式，直接退出。
	// 这对LOCK也很关键：时间戳模式不对，offset/delay 会漂，servo 很难 lock。
	/* Check the time stamping mode on each interface. */
	c->timestamping = timestamping;
	required_modes = clock_required_modes(c);
	STAILQ_FOREACH(iface, &config->interfaces, list) {
		memset(ts_label, 0, sizeof(ts_label));
		rtnl_get_ts_device(interface_name(iface), ts_label);
		interface_set_label(iface, ts_label);
		interface_ensure_tslabel(iface);
		interface_get_tsinfo(iface);
		if (interface_tsinfo_valid(iface) &&
		    !interface_tsmodes_supported(iface, required_modes)) {
			pr_err("interface '%s' does not support requested timestamping mode",
			       interface_name(iface));
			return NULL;
		}
	}

	iface = STAILQ_FIRST(&config->interfaces);

//step6 选择 PHC（/dev/ptpX）索引：决定你锁哪块硬件钟
	// 	逻辑总结:
	// 你要找“本地需要被调的时钟源是哪个设备”：就是这里决定的。
	/* determine PHC Clock index */
	// free_running：不去调任何时钟（不锁），所以 phc_index=-1。
	if (config_get_int(config, NULL, "free_running")) {
		phc_index = -1;
	// software/legacy：没有可用 PHC，后面会走 CLOCK_REALTIME 或不调。
	} else if (timestamping == TS_SOFTWARE || timestamping == TS_LEGACY_HW) {
		phc_index = -1;
	// 如果 user 显式指定： -p /dev/ptp0：<--------用它。
	} else if (phc_device) {
		if (1 != sscanf(phc_device, "/dev/ptp%d", &phc_index)) {
			phc_index = -1;
		}
	// 否则从第一个接口自动推断它绑定的 PHC index。推断不出来就报错。
	} else if (interface_tsinfo_valid(iface)) {
		phc_index = interface_phc_index(iface);
	} else {
		pr_err("PTP device not specified and automatic determination"
		       " is not supported. Please specify PTP device.");
		return NULL;
	}
	if (phc_index >= 0) {
		pr_info("selected /dev/ptp%d as PTP clock", phc_index);  // <----------------------第一条打印
	}

//step7: 设置 clockIdentity（参与 announce + BMC）
    // clockIdentity 是一个 8 字节的唯一标识，用来区分每一台 PTP 设备。
	// MAC: aa:bb:cc:dd:ee:ff
	// 生成 clockIdentity: aa:bb:cc:ff:fe:dd:ee:ff  中间插入 FF:FE，变成 8 字节。
	if (strcmp(config_get_string(config, NULL, "clockIdentity"),
		   "000000.0000.000000") == 0) {
		if (generate_clock_identity(&c->dds.clockIdentity,
					    interface_name(iface))) {
			pr_err("failed to generate a clock identity");
			return NULL;
		}
	} else {
		if (str2cid(config_get_string(config, NULL, "clockIdentity"),
					      &c->dds.clockIdentity)) {
			pr_err("failed to set clock identity");
			return NULL;
		}
	}

//step8 配置 UDS（Unix Domain Socket）“管理端口”
	// 假设：
	//    网络端口 = 你对外说话的嘴巴
	//    UDS 端口 = 你和自己系统内部沟通的“耳机接口”

	// 网络端口用于：发 Announce发 Sync收 Delay_Resp

	// UDS 用于：本机查询,管理命令,监控
	// 这里创建了两个 interface：rw/ro,这是给 pmc / 管理客户端用的本地管理通道。为了让 UDS 不参与普通 PTP 选主/定时器行为，把 announceReceiptTimeout=0 等设置得很“无害”。
	/* Configure the UDS. */

	uds_ifname = config_get_string(config, NULL, "uds_address");
	c->uds_rw_if = interface_create(uds_ifname);
	if (config_set_section_int(config, interface_name(c->uds_rw_if),
				   "announceReceiptTimeout", 0)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_rw_if),
				    "delay_mechanism", DM_AUTO)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_rw_if),
				    "network_transport", TRANS_UDS)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_rw_if),
				   "delay_filter_length", 1)) {
		return NULL;
	}

	uds_ifname = config_get_string(config, NULL, "uds_ro_address");
	c->uds_ro_if = interface_create(uds_ifname);
	if (config_set_section_int(config, interface_name(c->uds_ro_if),
				   "announceReceiptTimeout", 0)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_ro_if),
				   "delay_mechanism", DM_AUTO)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_ro_if),
				   "network_transport", TRANS_UDS)) {
		return NULL;
	}
	if (config_set_section_int(config, interface_name(c->uds_ro_if),
				   "delay_filter_length", 1)) {
		return NULL;
	}

//step9 把一堆运行参数塞进 clock 对象
	c->config = config;
	c->free_running = config_get_int(config, NULL, "free_running");
	c->freq_est_interval = config_get_int(config, NULL, "freq_est_interval");
	c->local_sync_uncertain = SYNC_UNCERTAIN_DONTCARE;
	c->write_phase_mode = config_get_int(config, NULL, "write_phase_mode");  // write_phase_mode：是否使用驱动的 writephase（相位写入）能力。
	c->grand_master_capable = config_get_int(config, NULL, "gmCapable");
	c->kernel_leap = config_get_int(config, NULL, "kernel_leap");  // kernel_leap/utc_offset/utc_timescale/time_flags：决定你发布的 timescale（TAI/PTP vs UTC）以及闰秒相关。
	c->utc_offset = config_get_int(config, NULL, "utc_offset");
	c->time_source = config_get_int(config, NULL, "timeSource");
	c->step_window = config_get_int(config, NULL, "step_window");  // step_window：通常影响什么时候 step（跳变）vs slew（渐进调频）。

//step10 打开并初始化“被调的时钟”-----------clkid（分成三种情况：free_running / 有PHC / CLOCK_REALTIME）
	// case 1: free_running  -> 不调时钟
	if (c->free_running) {
		c->clkid = CLOCK_INVALID;
		if (timestamping == TS_SOFTWARE || timestamping == TS_LEGACY_HW) {
			c->utc_timescale = 1;
		}
	// case2: 有 PHC: (先找之前的phc_index，如果没有就用用户cmd里的phc_device)
	// 	打开 /dev/ptpX 得到 clockid
	//  读最大可调频范围 max_adj
	//  初始化 clockadj（后面真正调频/step 都靠它）
	} else if (phc_index >= 0) {
		snprintf(phc, sizeof(phc), "/dev/ptp%d", phc_index);
		c->clkid = phc_open(phc);
		if (c->clkid == CLOCK_INVALID) {
			pr_err("Failed to open %s: %m", phc);
			return NULL;
		}
		max_adj = phc_max_adj(c->clkid);
		if (!max_adj) {
			pr_err("clock is not adjustable");
			return NULL;
		}
		// 初始化 clockadj, 做一次能力检测 + 运行参数初始化, 为后续对这个时钟（PHC 或 CLOCK_REALTIME）进行调频/调相做准备.
		// 后面 servo 的所有输出都会打到 c->clkid。这就是“被锁定的设备”
		clockadj_init(c->clkid);
	} else if (phc_device) {  // phc_device:是用户显式 -p /dev/ptp0
		c->clkid = phc_open(phc_device);
		if (c->clkid == CLOCK_INVALID) {
			pr_err("Failed to open %s: %m", phc_device);
			return NULL;
		}
		max_adj = clockadj_max_freq(c->clkid);
		clockadj_init(c->clkid);
	// case 3: 没 PHC 用系统时钟,走系统时钟锁定路径
	} else {
		c->clkid = CLOCK_REALTIME;
		c->utc_timescale = 1;
		clockadj_init(c->clkid);
		max_adj = sysclk_max_freq();
		sysclk_set_leap(0);
	}
	c->utc_offset_set = 0;
	c->leap_set = 0;
	c->time_flags = c->utc_timescale ? 0 : PTP_TIMESCALE;

//step11: 读当前频偏 fadj，创建 servo（锁定环在这里“出生”）
	if (c->clkid != CLOCK_INVALID) {
		fadj = (int) clockadj_get_freq(c->clkid);  // fadj：当前时钟的频率偏移（ppb）
		/* Due to a bug in older kernels, the reading may silently fail
		   and return 0. Set the frequency back to make sure fadj is
		   the actual frequency of the clock. */
		if (!c->free_running) {
			clockadj_set_freq(c->clkid, fadj);
		}
		/* Disable write phase mode if not implemented by driver */
		if (c->write_phase_mode && !phc_has_writephase(c->clkid)) {
			pr_err("clock does not support write phase mode");
			return NULL;
		}
	}
	// 创建 servo
		// 	初始化 servo 的“初始频偏基准”（这里用 -fadj 是为了把 servo 的内部状态与当前硬件频率对齐）。
		//  给 servo 一个最大调频限制 max_adj，防止输出超过硬件能力。
	c->servo = servo_create(c->config, servo, -fadj, max_adj, sw_ts);
	if (!c->servo) {
		pr_err("Failed to create clock servo");
		return NULL;
	}
	c->servo_state = SERVO_UNLOCKED;  // 初始状态 SERVO_UNLOCKED：启动时肯定没锁。
	c->servo_type = servo;  // 这个函数最开始时获取了 default.cfg 里 clock_servo 是 pi

//step12: 选择数据集比较函数（BMC 比较策略）普通 IEEE 1588 vs G.8275 电信规则（telecom_dscmp）
	if (config_get_int(config, NULL, "dataset_comparison") == DS_CMP_G8275) {
		c->dscmp = telecom_dscmp;
	} else {
		c->dscmp = dscmp;
	}

//step13 创建时间戳处理器 tsproc + stats + sanity check.   (tsproc 是“时间戳滤波器”，stats 是“统计器”，sanity_check 是“安全阀”)
	// tsproc = 把原始 PTP 时间戳变成“干净的 offset/delay”再交给 servo 的滤波器。
	    // tsproc作用主要有 4 个：1. 过滤抖动（最重要）2.计算 mean path delay 3.处理双向比率（nrr）4.控制初始 delay
	c->tsproc = tsproc_create(config_get_int(config, NULL, "tsproc_mode"),
				  config_get_int(config, NULL, "delay_filter"),
				  config_get_int(config, NULL, "delay_filter_length"));
	if (!c->tsproc) {
		pr_err("Failed to create time stamp processor");
		return NULL;
	}
	c->initial_delay = dbl_tmv(config_get_int(config, NULL, "initial_delay"));
	c->master_local_rr = 1.0;
	c->nrr = 1.0;
	// stats 用来统计 offset/freq/delay 的平均值、最大值、标准差。
	c->stats_interval = config_get_int(config, NULL, "summary_interval");  // ptp4l 会定期打印 summary：rms offset/mean freq/mean delay
	c->stats.offset = stats_create();
	c->stats.freq = stats_create();
	c->stats.delay = stats_create();
	if (!c->stats.offset || !c->stats.freq || !c->stats.delay) {
		pr_err("failed to create stats");
		return NULL;
	}
	sfl = config_get_int(config, NULL, "sanity_freq_limit");  // 如果设置了sanity_freq_limit 200000, 如果 servo 输出频率超过这个范围，就认为异常。它可以防止 servo 把频率调到离谱值.
	if (sfl) {
		c->sanity_check = clockcheck_create(sfl);
		if (!c->sanity_check) {
			pr_err("Failed to create clock sanity check");
			return NULL;
		}
	}

//step14: 初始化 parentDS(parentDS：我当前跟随的 Master / GM 是谁) / grandmaster 信息 + 同步周期
	/* Initialize the parentDS. */
	clock_update_grandmaster(c);
	c->dad.pds.parentStats                           = 0;
	c->dad.pds.observedParentOffsetScaledLogVariance = 0xffff;
	c->dad.pds.observedParentClockPhaseChangeRate    = 0x7fffffff;
	c->dad.ptl = c->ptl;

	clock_sync_interval(c, 0);  // 设置 sync interval（0 这里通常意味着按配置初始化）

//step15: 初始化端口容器、pollfd、并创建 UDS 端口
	LIST_INIT(&c->subscribers);
	LIST_INIT(&c->ports);
	c->last_port_number = 0;

	if (clock_resize_pollfd(c, 0)) {
		pr_err("failed to allocate pollfd");
		return NULL;
	}

	/* Create the UDS interfaces. */

	c->uds_rw_port = port_open(phc_device, phc_index, timestamping, 0,
				   c->uds_rw_if, c);  // port_open() 会创建一个 port，并把它的 fd 纳入 clock 的 poll 集合。
	if (!c->uds_rw_port) {
		pr_err("failed to open the UDS-RW port");
		return NULL;
	}
	c->uds_ro_port = port_open(phc_device, phc_index, timestamping, 0,
				   c->uds_ro_if, c);
	if (!c->uds_ro_port) {
		pr_err("failed to open the UDS-RO port");
		return NULL;
	}
	clock_fda_changed(c);

	c->slave_event_monitor = monitor_create(config, c->uds_rw_port);  // monitor_create()：监听 slave 状态变化并通过 UDS 报告（比如给外部工具）
	if (!c->slave_event_monitor) {
		pr_err("failed to create slave event monitor");
		return NULL;
	}

//step16: 创建“真实网络接口”-------port,
    // “需要创建哪些 port”基本就靠 cmd 配置的 config->interfaces列表
	// 这一步为每个 -i ethX 创建 PTP 端口：绑定 transport、时间戳模式、定时器、状态机等。

	/* Create the ports. */
	STAILQ_FOREACH(iface, &config->interfaces, list) {
		if (clock_add_port(c, phc_device, phc_index, timestamping, iface)) {
			pr_err("failed to open port %s", interface_name(iface));
			return NULL;
		}
	}

	c->dds.numberPorts = c->nports;

	// 17) 触发每个 port 的状态机初始化事件:
	// 真正让端口 FSM（designated_fsm）进入初始状态并开始工作。
	LIST_FOREACH(p, &c->ports, list) {
		port_dispatch(p, EV_INITIALIZE, 0);
	}
	port_dispatch(c->uds_rw_port, EV_INITIALIZE, 0);
	port_dispatch(c->uds_ro_port, EV_INITIALIZE, 0);

	return c;
}

struct dataset *clock_best_foreign(struct clock *c)
{
	return c->best ? &c->best->dataset : NULL;
}

struct port *clock_best_port(struct clock *c)
{
	return c->best ? c->best->port : NULL;
}

struct dataset *clock_default_ds(struct clock *c)
{
	struct dataset *out = &c->default_dataset;
	struct defaultDS *in = &c->dds;

	out->priority1              = in->priority1;
	out->identity               = in->clockIdentity;
	out->quality                = in->clockQuality;
	out->priority2              = in->priority2;
	out->stepsRemoved           = 0;
	out->sender.clockIdentity   = in->clockIdentity;
	out->sender.portNumber      = 0;
	out->receiver.clockIdentity = in->clockIdentity;
	out->receiver.portNumber    = 0;

	return out;
}

UInteger8 clock_domain_number(struct clock *c)
{
	return c->dds.domainNumber;
}

struct port *clock_first_port(struct clock *c)
{
	return LIST_FIRST(&c->ports);
}

void clock_follow_up_info(struct clock *c, struct follow_up_info_tlv *f)
{
	c->status.cumulativeScaledRateOffset = f->cumulativeScaledRateOffset;
	c->status.scaledLastGmPhaseChange = f->scaledLastGmPhaseChange;
	c->status.gmTimeBaseIndicator = f->gmTimeBaseIndicator;
	memcpy(&c->status.lastGmPhaseChange, &f->lastGmPhaseChange,
	       sizeof(c->status.lastGmPhaseChange));
}

int clock_free_running(struct clock *c)
{
	return c->free_running ? 1 : 0;
}

int clock_gm_capable(struct clock *c)
{
	return c->grand_master_capable;
}

struct ClockIdentity clock_identity(struct clock *c)
{
	return c->dds.clockIdentity;
}

static int clock_resize_pollfd(struct clock *c, int new_nports)
{
	struct pollfd *new_pollfd;

	/* Need to allocate two whole extra blocks of fds for UDS ports. */
	new_pollfd = realloc(c->pollfd,
			     (new_nports + 2) * N_CLOCK_PFD *
			     sizeof(struct pollfd));
	if (!new_pollfd) {
		return -1;
	}
	c->pollfd = new_pollfd;
	return 0;
}

static void clock_fill_pollfd(struct pollfd *dest, struct port *p)
{
	struct fdarray *fda;
	int i;

	fda = port_fda(p);
	for (i = 0; i < N_POLLFD; i++) {
		dest[i].fd = fda->fd[i];
		dest[i].events = POLLIN|POLLPRI;
	}
	dest[i].fd = port_fault_fd(p);
	dest[i].events = POLLIN|POLLPRI;
}

static void clock_check_pollfd(struct clock *c)
{
	struct port *p;
	struct pollfd *dest = c->pollfd;

	if (c->pollfd_valid) {
		return;
	}
	LIST_FOREACH(p, &c->ports, list) {
		clock_fill_pollfd(dest, p);
		dest += N_CLOCK_PFD;
	}
	clock_fill_pollfd(dest, c->uds_rw_port);
	dest += N_CLOCK_PFD;
	clock_fill_pollfd(dest, c->uds_ro_port);
	c->pollfd_valid = 1;
}

void clock_fda_changed(struct clock *c)
{
	c->pollfd_valid = 0;
}

static int clock_do_forward_mgmt(struct clock *c,
				 struct port *in, struct port *out,
				 struct ptp_message *msg, int *pre_sent)
{
	if (in == out || !forwarding(c, out))
		return 0;

	/* Don't forward any requests to the UDS-RW port
	   (the UDS-RO port doesn't allow any forwarding). */
	if (out == c->uds_rw_port) {
		switch (management_action(msg)) {
		case GET:
		case SET:
		case COMMAND:
			return 0;
		}
	}

	if (!*pre_sent) {
		/* delay calling msg_pre_send until
		 * actually forwarding */
		msg_pre_send(msg);
		*pre_sent = 1;
	}
	return port_forward(out, msg);
}

static void clock_forward_mgmt_msg(struct clock *c, struct port *p, struct ptp_message *msg)
{
	struct port *piter;
	int pdulen = 0, msg_ready = 0;

	if (forwarding(c, p) && msg->management.boundaryHops) {
		pdulen = msg->header.messageLength;
		msg->management.boundaryHops--;
		LIST_FOREACH(piter, &c->ports, list) {
			if (clock_do_forward_mgmt(c, p, piter, msg, &msg_ready))
				pr_err("%s: management forward failed",
				       port_log_name(piter));
		}
		if (clock_do_forward_mgmt(c, p, c->uds_rw_port, msg, &msg_ready))
			pr_err("uds port: management forward failed");
		if (msg_ready) {
			msg_post_recv(msg, pdulen);
			msg->management.boundaryHops++;
		}
	}
}

tmv_t clock_ingress_time(struct clock *c)
{
	return c->ingress_ts;
}

int clock_manage(struct clock *c, struct port *p, struct ptp_message *msg)
{
	int changed = 0, res, answers;
	struct port *piter;
	struct management_tlv *mgt;
	struct ClockIdentity *tcid, wildcard = {
		{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
	};

	/* Forward this message out all eligible ports. */
	clock_forward_mgmt_msg(c, p, msg);

	/* Apply this message to the local clock and ports. */
	tcid = &msg->management.targetPortIdentity.clockIdentity;
	if (!cid_eq(tcid, &wildcard) && !cid_eq(tcid, &c->dds.clockIdentity)) {
		return changed;
	}
	if (msg_tlv_count(msg) != 1) {
		return changed;
	}
	mgt = (struct management_tlv *) msg->management.suffix;

	/*
	  The correct length according to the management ID is checked
	  in tlv.c, but management TLVs with empty bodies are also
	  received successfully to support GETs and CMDs. At this
	  point the TLV either has the correct length or length 2.
	*/
	switch (management_action(msg)) {
	case GET:
		if (clock_management_get_response(c, p, mgt->id, msg))
			return changed;
		break;
	case SET:
		if (mgt->length == 2 && mgt->id != TLV_NULL_MANAGEMENT) {
			clock_management_send_error(p, msg, TLV_WRONG_LENGTH);
			return changed;
		}
		if (p != c->uds_rw_port) {
			/* Sorry, only allowed on the UDS-RW port. */
			clock_management_send_error(p, msg, TLV_NOT_SUPPORTED);
			return changed;
		}
		if (clock_management_set(c, p, mgt->id, msg, &changed))
			return changed;
		break;
	case COMMAND:
		if (p != c->uds_rw_port) {
			/* Sorry, only allowed on the UDS-RW port. */
			clock_management_send_error(p, msg, TLV_NOT_SUPPORTED);
			return changed;
		}
		break;
	default:
		return changed;
	}

	switch (mgt->id) {
	case TLV_PORT_PROPERTIES_NP:
		if (p != c->uds_rw_port) {
			/* Only the UDS-RW port allowed. */
			clock_management_send_error(p, msg, TLV_NOT_SUPPORTED);
			return 0;
		}
	}

	switch (mgt->id) {
	case TLV_USER_DESCRIPTION:
	case TLV_SAVE_IN_NON_VOLATILE_STORAGE:
	case TLV_RESET_NON_VOLATILE_STORAGE:
	case TLV_INITIALIZE:
	case TLV_FAULT_LOG:
	case TLV_FAULT_LOG_RESET:
	case TLV_DEFAULT_DATA_SET:
	case TLV_CURRENT_DATA_SET:
	case TLV_PARENT_DATA_SET:
	case TLV_PTP_SOURCE_STATUS_LIST:
	case TLV_TIME_PROPERTIES_DATA_SET:
	case TLV_PRIORITY1:
	case TLV_PRIORITY2:
	case TLV_DOMAIN:
	case TLV_SLAVE_ONLY:
	case TLV_TIME:
	case TLV_CLOCK_ACCURACY:
	case TLV_UTC_PROPERTIES:
	case TLV_TRACEABILITY_PROPERTIES:
	case TLV_TIMESCALE_PROPERTIES:
	case TLV_PATH_TRACE_LIST:
	case TLV_PATH_TRACE_ENABLE:
	case TLV_GRANDMASTER_CLUSTER_TABLE:
	case TLV_ACCEPTABLE_MASTER_TABLE:
	case TLV_ACCEPTABLE_MASTER_MAX_TABLE_SIZE:
	case TLV_ALTERNATE_TIME_OFFSET_ENABLE:
	case TLV_ALTERNATE_TIME_OFFSET_NAME:
	case TLV_ALTERNATE_TIME_OFFSET_MAX_KEY:
	case TLV_ALTERNATE_TIME_OFFSET_PROPERTIES:
	case TLV_TRANSPARENT_CLOCK_DEFAULT_DATA_SET:
	case TLV_PRIMARY_DOMAIN:
	case TLV_TIME_STATUS_NP:
	case TLV_GRANDMASTER_SETTINGS_NP:
	case TLV_SUBSCRIBE_EVENTS_NP:
	case TLV_SYNCHRONIZATION_UNCERTAIN_NP:
		clock_management_send_error(p, msg, TLV_NOT_SUPPORTED);
		break;
	default:
		answers = 0;
		LIST_FOREACH(piter, &c->ports, list) {
			res = port_manage(piter, p, msg);
			if (res < 0)
				return changed;
			if (res > 0)
				answers++;
		}
		if (!answers) {
			/* IEEE 1588 Interpretation #21 suggests to use
			 * TLV_WRONG_VALUE for ports that do not exist */
			clock_management_send_error(p, msg, TLV_WRONG_VALUE);
		}
		break;
	}
	return changed;
}

void clock_notify_event(struct clock *c, enum notification event)
{
	struct port *uds = c->uds_rw_port;
	struct PortIdentity pid = port_identity(uds);
	struct ptp_message *msg;
	int id;

	switch (event) {
	case NOTIFY_TIME_SYNC:
		id = TLV_TIME_STATUS_NP;
		break;
	default:
		return;
	}
	/* targetPortIdentity and sequenceId will be filled by
	 * clock_send_notification */
	msg = port_management_notify(pid, uds);
	if (!msg)
		return;
	if (!clock_management_fill_response(c, NULL, NULL, msg, id))
		goto err;
	if (msg_pre_send(msg))
		goto err;
	clock_send_notification(c, msg, event);
err:
	msg_put(msg);
}

struct parent_ds *clock_parent_ds(struct clock *c)
{
	return &c->dad;
}

struct PortIdentity clock_parent_identity(struct clock *c)
{
	return c->dad.pds.parentPortIdentity;
}

void clock_set_sde(struct clock *c, int sde)
{
	c->sde = sde;
}

// 等待所有端口的事件 → 分发给 port → 触发状态机 → 必要时做一次全局选主决策
int clock_poll(struct clock *c)
{
	int cnt, i;
	enum fsm_event event;
	struct pollfd *cur;
	struct port *p;

	clock_check_pollfd(c);
	cnt = poll(c->pollfd, (c->nports + 2) * N_CLOCK_PFD, -1);  // 等待所有 fd（包括每个 port 的 socket + UDS）
                                                               // -1 表示阻塞等待,直到有数据到达
                                                               // 这相当于：“我先睡着，谁来消息叫我。”
	if (cnt < 0) {
		if (EINTR == errno) {
			return 0;
		} else {
			pr_emerg("poll failed");
			return -1;
		}
	} else if (!cnt) {
		return 0;
	}

	cur = c->pollfd;

	// 遍历每个 port
	    // *_event：把“外部刺激”（收包/超时）翻译成"事件event"
        // *_dispatch：根据"event"驱动状态机做动作
	LIST_FOREACH(p, &c->ports, list) {
		/* Let the ports handle their events. */  // 遍历0-11是为了判断：这个 port 的每一种“可能事件源”有没有触发。
		for (i = 0; i < N_POLLFD; i++) {
			if (cur[i].revents & (POLLIN|POLLPRI|POLLERR)) {
				if (cur[i].revents & POLLERR) {
					pr_err("%s: unexpected socket error",
					       port_log_name(p));
					event = EV_FAULT_DETECTED;
				} else {
					// 当 clock_poll() 发现某个 port 的 fd 有 POLLIN，它会调用：event = port_event(p, i);
					event = port_event(p, i);  // 报文真正进入 ptp4l 的入口, port_event → (bc_event) -> process_sync → clock_synchronize → servo_sample
				}
				// sde = state decision event,意思是：“可能需要重新选主（BMC）。”
                    // 比如：1.收到新的 Announce 2. announce 超时 3.状态变化
				if (EV_STATE_DECISION_EVENT == event) {
					c->sde = 1;
				}
				if (EV_ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES == event) {
					c->sde = 1;
				}
				port_dispatch(p, event, 0);  // 把这个事件丢给 port 的状态机处理
				/* Clear any fault after a little while. */
				if (PS_FAULTY == port_state(p)) {
					clock_fault_timeout(p, 1);
					break;
				}
			}
		}

		/*
		 * When the fault timer expires we clear the fault,
		 * but only if the link is up.
		 */
		if (cur[N_POLLFD].revents & (POLLIN|POLLPRI)) {
			clock_fault_timeout(p, 0);
			if (port_link_status_get(p)) {
				port_dispatch(p, EV_FAULT_CLEARED, 0);
			}
		}

		cur += N_CLOCK_PFD;  // 表示跳到下一个 port 的 fd 区块
		// [port0_fd0, port0_fd1, ... port0_fdN,
        //  port1_fd0, port1_fd1, ... port1_fdN,
        //  ...]
	}

	/* Check the UDS ports. */
	for (i = 0; i < N_POLLFD; i++) {
		if (cur[i].revents & (POLLIN|POLLPRI)) {
			event = port_event(c->uds_rw_port, i);
			if (EV_STATE_DECISION_EVENT == event) {
				c->sde = 1;
			}
		}
	}
	cur += N_CLOCK_PFD;
	for (i = 0; i < N_POLLFD; i++) {
		if (cur[i].revents & (POLLIN|POLLPRI)) {
			event = port_event(c->uds_ro_port, i);
			/* sde is not expected on the UDS-RO port */
		}
	}

	// 所有 port 的变化统一收集后，再做一次全局选主决策。
	if (c->sde) {
		handle_state_decision_event(c);
		c->sde = 0;
	}
	clock_prune_subscriptions(c);
	return 0;
}

void clock_path_delay(struct clock *c, tmv_t req, tmv_t rx)
{
	tsproc_up_ts(c->tsproc, req, rx);

	if (tsproc_update_delay(c->tsproc, &c->path_delay))
		return;

	c->cur.meanPathDelay = tmv_to_TimeInterval(c->path_delay);

	if (c->stats.delay)
		stats_add_value(c->stats.delay, tmv_dbl(c->path_delay));
}

void clock_peer_delay(struct clock *c, tmv_t ppd, tmv_t req, tmv_t rx,
		      double nrr)
{
	c->path_delay = ppd;
	c->nrr = nrr;

	tsproc_set_delay(c->tsproc, ppd);
	tsproc_up_ts(c->tsproc, req, rx);

	if (c->stats.delay)
		stats_add_value(c->stats.delay, tmv_dbl(ppd));
}

struct monitor *clock_slave_monitor(struct clock *c)
{
	return c->slave_event_monitor;
}

int clock_slave_only(struct clock *c)
{
	return c->dds.flags & DDS_SLAVE_ONLY;
}

UInteger8 clock_max_steps_removed(struct clock *c)
{
	return c->max_steps_removed;
}

UInteger8 clock_get_clock_class_threshold(struct clock *c)
{
	return c->clock_class_threshold;
}

UInteger16 clock_steps_removed(struct clock *c)
{
	return c->cur.stepsRemoved;
}

int clock_switch_phc(struct clock *c, int phc_index)
{
	struct servo *servo;
	int fadj, max_adj;
	clockid_t clkid;
	char phc[32];

	snprintf(phc, sizeof(phc), "/dev/ptp%d", phc_index);
	clkid = phc_open(phc);
	if (clkid == CLOCK_INVALID) {
		pr_err("Switching PHC, failed to open %s: %m", phc);
		return -1;
	}
	max_adj = phc_max_adj(clkid);
	if (!max_adj) {
		pr_err("Switching PHC, clock is not adjustable");
		phc_close(clkid);
		return -1;
	}
	fadj = (int) clockadj_get_freq(clkid);
	clockadj_set_freq(clkid, fadj);
	servo = servo_create(c->config, c->servo_type, -fadj, max_adj, 0);
	if (!servo) {
		pr_err("Switching PHC, failed to create clock servo");
		phc_close(clkid);
		return -1;
	}
	phc_close(c->clkid);
	servo_destroy(c->servo);
	c->clkid = clkid;
	c->servo = servo;
	c->servo_state = SERVO_UNLOCKED;
	return 0;
}

static void clock_step_window(struct clock *c)
{
	if (!c->step_window) {
		return;
	}
	c->step_window_counter = c->step_window;
}

static void clock_synchronize_locked(struct clock *c, double adj)
{
	clockadj_set_freq(c->clkid, -adj);
	if (c->clkid == CLOCK_REALTIME) {
		sysclk_set_sync();
	}
	if (c->sanity_check) {
		clockcheck_set_freq(c->sanity_check, -adj);
	}
}

// 把 PHC 往 GM 主时钟靠
enum servo_state clock_synchronize(struct clock *c, tmv_t ingress, tmv_t origin)
{
	enum servo_state state = SERVO_UNLOCKED;
	double adj, weight;
	int64_t offset;

	// step1: 一旦发生过 step/jump，就会在接下来的 N 个 Sync 样本里直接跳过调整，保持上一次的 servo_state。
	// 这是为了避免刚 step 完就立刻被噪声/延迟样本拉着乱调。
	//所以如果你看到频繁 JUMP，说明它要么一直在触发 step/jump，要么 step_window 很小/被打断。
	if (c->step_window_counter) {
		c->step_window_counter--;
		pr_debug("skip sync after jump %d/%d",
			 c->step_window - c->step_window_counter,
			 c->step_window);
		return c->servo_state;
	}

	c->ingress_ts = ingress;

	// step2: 由 tsproc 计算 offset（并可能失败）
	tsproc_down_ts(c->tsproc, origin, ingress);
    // tsproc_update_offset() 返回非 0 表示：这次样本没法形成有效 offset（例如缺 delay、滤波器还没准备好、数据不完整/不可信等）
	if (tsproc_update_offset(c->tsproc, &c->master_offset, &weight)) {
		if (c->free_running) {
			return clock_no_adjust(c, ingress, origin);
		} else {
			//if the condition isnot satisfy, just keep the state. will not adjust.
			if (c->servo_state == SERVO_LOCKED_STABLE)//Z
			{
				c->servo_state = SERVO_LOCKED;
			}

			return c->servo_state;
		}
	}

	if (clock_utc_correct(c, ingress)) {
		return c->servo_state;
	}

	c->cur.offsetFromMaster = tmv_to_TimeInterval(c->master_offset);

	if (c->free_running) {
		state = clock_no_adjust(c, ingress, origin);
		clock_notify_event(c, NOTIFY_TIME_SYNC);
		return state;
	}

	// step3: 调用 servo_sample() 得到 servo_state 与 adj
	offset = tmv_to_nanoseconds(c->master_offset);
	adj = servo_sample(c->servo, offset, tmv_to_nanoseconds(ingress),
			   weight, &state);
	c->servo_state = state;

	tsproc_set_clock_rate_ratio(c->tsproc, clock_rate_ratio(c));

	// step4: 根据 state 对 PHC 执行动作：
	// SERVO_JUMP → clockadj_step() / set_freq() / tsproc_reset() / step_window
	// SERVO_LOCKED → clock_synchronize_locked()（调频）
	// SERVO_LOCKED_STABLE →（可选）set_phase() 或继续调频
	switch (state) {
	case SERVO_UNLOCKED:
		break;
	case SERVO_JUMP:
		clockadj_set_freq(c->clkid, -adj);
		clockadj_step(c->clkid, -tmv_to_nanoseconds(c->master_offset));
		c->ingress_ts = tmv_zero();
		if (c->sanity_check) {
			clockcheck_set_freq(c->sanity_check, -adj);
			clockcheck_step(c->sanity_check,
					-tmv_to_nanoseconds(c->master_offset));
		}
		tsproc_reset(c->tsproc, 0);
		clock_step_window(c);
		break;
	case SERVO_LOCKED:
		clock_synchronize_locked(c, adj);
		break;
	case SERVO_LOCKED_STABLE:
		if (c->write_phase_mode) {
			clockadj_set_phase(c->clkid, -offset);
			adj = 0;
		} else {
			clock_synchronize_locked(c, adj);
		}
		break;
	}

	if (c->stats.max_count > 1) {
		clock_stats_update(&c->stats, tmv_dbl(c->master_offset), adj);
	} else {
		pr_info("master offset %10" PRId64 " s%d freq %+7.0f "
			"path delay %9" PRId64,
			tmv_to_nanoseconds(c->master_offset), state, adj,
			tmv_to_nanoseconds(c->path_delay));
	}

	clock_notify_event(c, NOTIFY_TIME_SYNC);

	return state;
}

void clock_sync_interval(struct clock *c, int n)
{
	int shift;

	shift = c->freq_est_interval - n;
	if (shift < 0)
		shift = 0;
	else if (shift >= sizeof(int) * 8) {
		shift = sizeof(int) * 8 - 1;
		pr_warning("freq_est_interval is too long");
	}
	c->fest.max_count = (1 << shift);

	shift = c->stats_interval - n;
	if (shift < 0)
		shift = 0;
	else if (shift >= sizeof(int) * 8) {
		shift = sizeof(int) * 8 - 1;
		pr_warning("summary_interval is too long");
	}
	c->stats.max_count = (1 << shift);

	servo_sync_interval(c->servo, n < 0 ? 1.0 / (1 << -n) : 1 << n);
}

struct timePropertiesDS clock_time_properties(struct clock *c)
{
	struct timePropertiesDS tds = c->tds;

	switch (c->local_sync_uncertain) {
	case SYNC_UNCERTAIN_DONTCARE:
		tds.flags &= ~SYNC_UNCERTAIN;
		break;
	case SYNC_UNCERTAIN_FALSE:
		/* Pass the upstream value, if any. */
		break;
	case SYNC_UNCERTAIN_TRUE:
		tds.flags |= SYNC_UNCERTAIN;
		break;
	}
	return tds;
}

void clock_update_time_properties(struct clock *c, struct timePropertiesDS tds)
{
	c->tds = tds;
}

static void handle_state_decision_event(struct clock *c)
{
	struct foreign_clock *best = NULL, *fc;
	struct ClockIdentity best_id;
	struct port *piter;
	int fresh_best = 0;

// step1: 从每个端口的 foreign masters 里找“全局最优 best master”
	LIST_FOREACH(piter, &c->ports, list) {
		fc = port_compute_best(piter);  // 在这个端口看到的所有外部主时钟（foreign master）里，挑一个该端口认为最好的 fc.
		if (!fc)
			continue;
		// 然后跨端口再挑一个“全局最好”的 best, 比较用 c->dscmp()：你之前问过的 dataset comparison（普通1588 vs G.8275）
		if (!best || c->dscmp(&fc->dataset, &best->dataset) > 0)
			best = fc;
	}

// step2: 如果找不到 best_master，那么就把自己当做 best_master.
// Feb 11 05:44:01 Apricot-APP user.notice ptp4l: [3409.852] selected local clock 34fe9e.fffe.3c00a2 as best master
// Feb 11 05:44:01 Apricot-APP user.notice ptp4l: [3410.208] selected local clock 34fe9e.fffe.3c00a2 as best master
// Feb 11 05:44:02 Apricot-APP user.notice ptp4l: [3410.648] selected local clock 34fe9e.fffe.3c00a2 as best master
// Feb 11 05:44:02 Apricot-APP user.notice ptp4l: [3410.663] selected best master clock fcaf6a.fffe.02e902
// Feb 11 05:44:02 Apricot-APP user.info ptp4l: [3410.663] updating UTC offset to 37
	if (best) {
		best_id = best->dataset.identity;
	} else {
		best_id = c->dds.clockIdentity;
	}

	if (cid_eq(&best_id, &c->dds.clockIdentity)) {
		pr_notice("selected local clock %s as best master",
			  cid2str(&best_id));
	} else {
		pr_notice("selected best master clock %s, sxn c->dds.clockIdentity = %s",
			  cid2str(&best_id), cid2str(&c->dds.clockIdentity));
	}

	pr_err("sxn best_id %s, c->best_id = %s",
			  cid2str(&best_id), cid2str(&c->best_id));

// step3: 如果 best master 变了，就“重置同步相关状态”.
    // 主时钟一变，之前所有延迟/滤波/频偏估计都不可信了，必须清空重来。
	if (!cid_eq(&best_id, &c->best_id)) {
		clock_freq_est_reset(c);
		tsproc_reset(c->tsproc, 1);  // 清空 delay/offset 滤波器内部状态
		pr_err("sxn leave tsproc_reset(c->tsproc, 1)");
		if (!tmv_is_zero(c->initial_delay))
			tsproc_set_delay(c->tsproc, c->initial_delay);
		c->ingress_ts = tmv_zero();
		c->path_delay = c->initial_delay;  // path_delay 回到初值
		c->master_local_rr = 1.0;
		c->nrr = 1.0;
		fresh_best = 1;  // 告诉端口“这是一个新选出来的 master”，后面 port_dispatch(..., fresh_best) 会用到.
	}

	c->best = best;
	c->best_id = best_id;

// step4: 对每个 port 做 BMC state decision，并派发状态机事件
	LIST_FOREACH(piter, &c->ports, list) {
		enum port_state ps;
		enum fsm_event event;
		// 决定该端口应该扮演什么角色
			// 可能结果：
			// SLAVE：跟随 best master
			// MASTER / GRAND_MASTER：自己当主
			// LISTENING：还没决定/等待 announce
		ps = bmc_state_decision(c, piter, c->dscmp);

		// 把 ps 转换成 event
		switch (ps) {
		case PS_LISTENING:
			event = EV_NONE;
			break;
		case PS_GRAND_MASTER:
			pr_notice("%s: assuming the grand master role",
				  port_log_name(piter));
			clock_update_grandmaster(c);
			event = EV_RS_GRAND_MASTER;
			break;
		case PS_MASTER:
			event = EV_RS_MASTER;
			break;
		case PS_PASSIVE:
			event = EV_RS_PASSIVE;
			break;
		case PS_SLAVE:
			clock_update_slave(c);
			event = EV_RS_SLAVE;
			break;
		default:
			event = EV_FAULT_DETECTED;
			break;
		}

		// 执行 event
		port_dispatch(piter, event, fresh_best);
	}
}

struct clock_description *clock_description(struct clock *c)
{
	return &c->desc;
}

enum clock_type clock_type(struct clock *c)
{
	return c->type;
}

// 如果 PHC突然走得太快、太慢，或者直接跳了一大步，servo 继续工作是危险的，需要重置servo。
void clock_check_ts(struct clock *c, uint64_t ts)
{
	if (c->sanity_check && clockcheck_sample(c->sanity_check, ts)) {
		servo_reset(c->servo);
	}
}

double clock_rate_ratio(struct clock *c)
{
	if (c->free_running) {
		return c->master_local_rr;
	}
	return servo_rate_ratio(c->servo);
}

struct servo *clock_servo(struct clock *c)
{
	return c->servo;
}

enum servo_state clock_servo_state(struct clock *c)
{
	return c->servo_state;
}

void clock_set_ptp_source_in_announce_msg(struct clock *c, struct announce_msg msg)
{
	c->aml.pri1 = msg.grandmasterPriority1;
	c->aml.annMsgFlagField[0] = msg.hdr.flagField[0];
	c->aml.annMsgFlagField[1] = msg.hdr.flagField[1];
	c->aml.stepsRemoved = msg.stepsRemoved;
	c->aml.pri2 = msg.grandmasterPriority2;
	c->aml.currentUtcOffset = msg.currentUtcOffset;
	//pr_info("BR-DEBUG: get GET message from clock_set_ptp_source_in_announce_msg");
	//pr_info("BR-DEBUG: pri1=%d, flag0=%d, flag1=%d, stepsRemoved=%d", c->aml.pri1, c->aml.annMsgFlagField[0], c->aml.annMsgFlagField[1], c->aml.stepsRemoved);
	//pr_info("BR-DEBUG: pri2=%d, currentUtcOffset=%d", c->aml.pri2, c->aml.currentUtcOffset);
}

void clock_set_ptp_source_in_sync_msg(struct clock *c, struct sync_msg msg)
{
	c->aml.syncMsgFlagField[0] = msg.hdr.flagField[0];
	c->aml.syncMsgFlagField[1] = msg.hdr.flagField[1];
	//pr_info("BR-DEBUG: get GET message from clock_set_ptp_source_in_sync_msg");
	//pr_info("BR-DEBUG: flag0=%d, flag1=%d", c->aml.syncMsgFlagField[0], c->aml.syncMsgFlagField[1]);
}
