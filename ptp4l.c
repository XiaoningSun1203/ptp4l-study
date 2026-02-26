/**
 * @file ptp4l.c
 * @brief PTP Boundary Clock or Transparent Clock main program
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "config.h"
#include "ntpshm.h"
#include "pi.h"
#include "print.h"
#include "raw.h"
#include "sk.h"
#include "transport.h"
#include "udp6.h"
#include "uds.h"
#include "util.h"
#include "version.h"

#include "timestamp_get.h"


// /usr/bin/ptp4l -A -i fheth0 -p /dev/ptp0 -f /var/log/psislog/app/standalone_G.8275.1.cfg -s
// 这条命令的意思就是：
// 	在网卡 fheth0 上启动 ptp4l，使用 /dev/ptp0 硬件时钟设备，按照 standalone_G.8275.1.cfg 配置文件中的规则，以 Slave 模式 运行，并根据网络自动选择时钟主从（但这里加了 -s 强制做 Slave）。

static void usage(char *progname)
{
	fprintf(stderr,
		"\nusage: %s [options]\n\n"
		" Delay Mechanism\n\n"
		" -A        Auto, starting with E2E\n"
		" -E        E2E, delay request-response (default)\n"
		" -P        P2P, peer delay mechanism\n\n"
		" Network Transport\n\n"
		" -2        IEEE 802.3\n"
		" -4        UDP IPV4 (default)\n"
		" -6        UDP IPV6\n\n"
		" Time Stamping\n\n"
		" -H        HARDWARE (default)\n"
		" -S        SOFTWARE\n"
		" -L        LEGACY HW\n\n"
		" Other Options\n\n"
		" -f [file] read configuration from 'file'\n"
		" -i [dev]  interface device to use, for example 'eth0'\n"
		"           (may be specified multiple times)\n"
		" -p [dev]  Clock device to use, default auto\n"
		"           (ignored for SOFTWARE/LEGACY HW time stamping)\n"
		" -s        client only synchronization mode (overrides configuration file)\n"
		" -l [num]  set the logging level to 'num'\n"
		" -m        print messages to stdout\n"
		" -q        do not print messages to the syslog\n"
		" -v        prints the software version and exits\n"
		" -h        prints this message and exits\n"
		"\n",
		progname);
}

int main(int argc, char *argv[])
{
	// 	config：-f 指定的配置文件路径（默认不用文件）。
	// req_phc：-p 指定的 PHC 设备（比如 /dev/ptp0），用于强制选择哪个硬件时钟。
	// progname：程序名（用于打印 usage/log tag）。
	// type：时钟类型默认 Ordinary Clock（单口 slave/master）。
	// c：getopt_long() 每次解析到的选项字符。
	// err=-1：默认返回失败；只有跑到后面成功才置 0。
	// index：getopt_long() 返回 long option 时用的索引。
	// print_level：日志级别参数暂存。
	// clock：后面 clock_create() 创建出来的核心对象（整个 ptp4l 实例）。
	// opts：从配置系统里导出的 long options 列表。
	// cfg：配置对象，存所有配置项和接口列表。
	char *config = NULL, *req_phc = NULL, *progname;
	enum clock_type type = CLOCK_TYPE_ORDINARY;
	int c, err = -1, index, print_level;
	struct clock *clock = NULL;
	struct option *opts;
	struct config *cfg;

#ifdef PROT_STACK_FROM_FPGA
	if (0 != timestamp_mmap())
        return -1;
#endif

	// 注册 SIGINT/SIGTERM 等处理函数。
	// 目的：让 is_running() 能感知退出请求，while loop 能停下来。
	if (handle_term_signals())
		return -1;

	// 创建配置对象 & 获取 long options
	cfg = config_create();
	if (!cfg) {
		return -1;
	}
	opts = config_long_options(cfg);

	// 解析命令行
	/* Process the command line arguments. */
	progname = strrchr(argv[0], '/');
	progname = progname ? 1+progname : argv[0];
	while (EOF != (c = getopt_long(argc, argv, "AEP246HSLf:i:p:sl:mqvh",
				       opts, &index))) {
		switch (c) {
		case 0:
			if (config_parse_option(cfg, opts[index].name, optarg))
				goto out;
			break;
		case 'A':
			if (config_set_int(cfg, "delay_mechanism", DM_AUTO))
				goto out;
			break;
		case 'E':
			if (config_set_int(cfg, "delay_mechanism", DM_E2E))
				goto out;
			break;
		case 'P':
			if (config_set_int(cfg, "delay_mechanism", DM_P2P))
				goto out;
			break;
		case '2':
			if (config_set_int(cfg, "network_transport",
					    TRANS_IEEE_802_3))
				goto out;
			break;
		case '4':
			if (config_set_int(cfg, "network_transport",
					    TRANS_UDP_IPV4))
				goto out;
			break;
		case '6':
			if (config_set_int(cfg, "network_transport",
					    TRANS_UDP_IPV6))
				goto out;
			break;
		case 'H':
			if (config_set_int(cfg, "time_stamping", TS_HARDWARE))
				goto out;
			break;
		case 'S':
			if (config_set_int(cfg, "time_stamping", TS_SOFTWARE))
				goto out;
			break;
		case 'L':
			if (config_set_int(cfg, "time_stamping", TS_LEGACY_HW))
				goto out;
			break;
		case 'f':
			config = optarg;
			break;
		case 'i':
			if (!config_create_interface(optarg, cfg))
				goto out;
			break;
		case 'p':
			req_phc = optarg;
			break;
		case 's':
			if (config_set_int(cfg, "clientOnly", 1)) {
				goto out;
			}
			break;
		case 'l':
			if (get_arg_val_i(c, optarg, &print_level,
					  PRINT_LEVEL_MIN, PRINT_LEVEL_MAX))
				goto out;
			config_set_int(cfg, "logging_level", print_level);
			break;
		case 'm':
			config_set_int(cfg, "verbose", 1);
			break;
		case 'q':
			config_set_int(cfg, "use_syslog", 0);
			break;
		case 'v':
			version_show(stdout);
			return 0;
		case 'h':
			usage(progname);
			return 0;
		case '?':
			usage(progname);
			goto out;
		default:
			usage(progname);
			goto out;
		}
	}

	// 如果 config 非空，调用 config_read() 解析配置文件 standalone_G.8275.1.cfg 并填充 cfg。
	// 注意这里直接 return c;：配置文件解析失败不会走 out: 清理（因为此时还没创建 clock，但 cfg 已创建；
	// 这段代码风格就是直接返回错误码）
	if (config && (c = config_read(config, cfg))) {
		return c;
	}

	// 设置日志模块的：程序名、tag、是否 stdout、是否 syslog、日志级别。
	print_set_progname(progname);
	print_set_tag(config_get_string(cfg, NULL, "message_tag"));
	print_set_verbose(config_get_int(cfg, NULL, "verbose"));
	print_set_syslog(config_get_int(cfg, NULL, "use_syslog"));
	print_set_level(config_get_int(cfg, NULL, "logging_level"));

	//设置 socket/timestamp 相关的全局参数
	assume_two_step = config_get_int(cfg, NULL, "assume_two_step");  // 是否假设 master 是 two-step
	sk_check_fupsync = config_get_int(cfg, NULL, "check_fup_sync");  // 是否校验 follow_up 的 sync
	sk_tx_timeout = config_get_int(cfg, NULL, "tx_timestamp_timeout");  // TX 时间戳等待超时
	sk_hwts_filter_mode = config_get_int(cfg, NULL, "hwts_filter");  // 硬件时间戳过滤模式

	// 如果 servo 用的是 NTPSHM（把 ptp4l 结果喂给 ntpd/chrony），那：
    // kernel_leap 关掉（不让内核 leap second 机制干预？）
	// sanity_freq_limit 关掉（不做频率调整幅度的 sanity 限制）
	// 这是“配合外部 NTP 系统接管”的一种模式。
	if (config_get_int(cfg, NULL, "clock_servo") == CLOCK_SERVO_NTPSHM) {
		config_set_int(cfg, "kernel_leap", 0);
		config_set_int(cfg, "sanity_freq_limit", 0);
	}

	// 检查如果没有任何 -i 或配置文件设置的 port 时，直接报错退出。
	// 必须至少有一个port，否则根本不会创建任何 port. 因为 ptp4l 必须绑定 port 才能收发 PTP
	if (STAILQ_EMPTY(&cfg->interfaces)) {
		fprintf(stderr, "no interface specified\n");
		usage(progname);
		goto out;
	}

	// 从 standalone_G.8275.1.cfg 获取 clock_type [clock_type	OC]
	type = config_get_int(cfg, NULL, "clock_type");
	switch (type) {
	case CLOCK_TYPE_ORDINARY:
	    // 配置写 OC，但你给了多个port，实际变成 BC（更合理）
		if (cfg->n_interfaces > 1) {
			type = CLOCK_TYPE_BOUNDARY;
		}
		break;
	case CLOCK_TYPE_BOUNDARY:
	    // BOUNDARY 需要至少 2 port
		if (cfg->n_interfaces < 2) {
			fprintf(stderr, "BC needs at least two interfaces\n");
			goto out;
		}
		break;
	case CLOCK_TYPE_P2P:
	    // P2P_TC 需要至少 2 port + delay_mechanism 必须 P2P
		if (cfg->n_interfaces < 2) {
			fprintf(stderr, "TC needs at least two interfaces\n");
			goto out;
		}
		if (DM_P2P != config_get_int(cfg, NULL, "delay_mechanism")) {
			fprintf(stderr, "P2P_TC needs P2P delay mechanism\n");
			goto out;
		}
		break;
	case CLOCK_TYPE_E2E:
	    // E2E_TC 需要至少 2 port + delay_mechanism 必须 E2E
		if (cfg->n_interfaces < 2) {
			fprintf(stderr, "TC needs at least two interfaces\n");
			goto out;
		}
		if (DM_E2E != config_get_int(cfg, NULL, "delay_mechanism")) {
			fprintf(stderr, "E2E_TC needs E2E delay mechanism\n");
			goto out;
		}
		break;
	case CLOCK_TYPE_MANAGEMENT:
	    // MANAGEMENT：直接退出（这里不支持）
		goto out;
	}

	// 创建核心对象 clock.
	// 这是主入口里最关键的一步：
	// clock_create() 会根据 type + cfg->interfaces：
	// 打开 PHC（或自动选择）
	// 创建每个 interface 对应的 port
	// 初始化数据集（default/current/parent…）
	// 初始化网络传输（udp/802.3）
	// 初始化 servo / clockadj（为后面 lock 做准备）
	// 启动状态机初始状态（LISTENING 等）
	// 你要追“时钟源寻找和锁定”的真正逻辑，下一步基本都从这里深入到 clock.c / port.c / bmc.c / servo.c。
	clock = clock_create(type, cfg, req_phc);
	if (!clock) {
		fprintf(stderr, "failed to create a clock\n");
		goto out;
	}

	// 主循环：poll驱动整个程序运行
	// 	err=0：到这里说明初始化成功，默认成功退出。
	//  is_running()：信号处理里控制的全局状态（收到 SIGINT/SIGTERM 会变 false）。
	//  clock_poll(clock)：核心“事件循环”
	//  等待 socket/定时器事件
	//  收包处理（Sync/Announce/Delay…）
	//  触发 BMC/状态机切换 master/slave
	//  在 slave 状态下持续喂 offset 给 servo，输出频率调整，调用 clockadj 调整 PHC/系统时钟
	//  如果 clock_poll 返回非 0，表示出错或需要退出。
	err = 0;

	while (is_running()) {
		if (clock_poll(clock))
			break;
	}

// 统一清理退出
// 如果有 FPGA mmap，退出时 munmap。
// 销毁 clock（会依次销毁 ports/sockets/servo 等）。
// 销毁 config。
// 返回 err（初始化成功是 0，否则 -1）。
out:
#ifdef PROT_STACK_FROM_FPGA
	timestamp_munmap();
#endif

	if (clock)
		clock_destroy(clock);
	config_destroy(cfg);
	return err;
}
