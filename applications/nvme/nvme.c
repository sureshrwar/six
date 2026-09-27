/*
 * nvme.c - NVM-Express user space tooling (nvme-cli) for SIX (/bin/nvme)
 *
 * Supported subcommands:
 *   nvme list
 *   nvme id-ctrl   [/dev/nvme0]
 *   nvme id-ns     [/dev/nvme0n1] [-n <nsid>]
 *   nvme smart-log [/dev/nvme0]
 *   nvme flush     [/dev/nvme0n1]
 *   nvme dsm       [/dev/nvme0n1] [-s <slba>] [-b <blocks>] [-d]
 *   nvme write-zeroes [/dev/nvme0n1] [-s <slba>] [-c <blocks>]
 *   nvme reset     [/dev/nvme0]
 *   nvme show-regs [/dev/nvme0]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../../include/linux/nvme.h"

static void trim_trailing_spaces(const char *src, int max_len, char *dst)
{
	int i;

	memcpy(dst, src, max_len);
	dst[max_len] = '\0';
	for (i = max_len - 1; i >= 0; i--) {
		if (dst[i] == ' ' || dst[i] == '\0')
			dst[i] = '\0';
		else
			break;
	}
}

static int open_nvme_dev(const char *path)
{
	int fd;

	if (path && path[0]) {
		fd = open(path, O_RDWR);
		if (fd >= 0)
			return fd;
		fd = open(path, O_RDONLY);
		if (fd >= 0)
			return fd;
	}
	fd = open("/dev/nvme0", O_RDWR);
	if (fd >= 0)
		return fd;
	fd = open("/dev/nvme0n1", O_RDWR);
	if (fd >= 0)
		return fd;
	return open("/dev/nvme0n1", O_RDONLY);
}

static int fetch_id_ctrl(int fd, struct nvme_id_ctrl *ctrl)
{
	struct nvme_passthru_cmd cmd;

	memset(ctrl, 0, sizeof(*ctrl));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_identify;
	cmd.nsid = 0;
	cmd.addr = (unsigned long)ctrl;
	cmd.data_len = sizeof(*ctrl);
	cmd.cdw10 = NVME_ID_CNS_CTRL;

	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

static int fetch_id_ns(int fd, unsigned int nsid, struct nvme_id_ns *ns)
{
	struct nvme_passthru_cmd cmd;

	memset(ns, 0, sizeof(*ns));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_identify;
	cmd.nsid = nsid ? nsid : 1;
	cmd.addr = (unsigned long)ns;
	cmd.data_len = sizeof(*ns);
	cmd.cdw10 = NVME_ID_CNS_NS;

	return ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

static int cmd_list(void)
{
	struct nvme_id_ctrl ctrl;
	struct nvme_id_ns ns;
	char sn[24], mn[44], fr[12], usage_str[32], fmt_str[16];
	int fd = open_nvme_dev("/dev/nvme0n1");
	unsigned long cap_mb, use_mb;
	unsigned int lba_bytes;

	if (fd < 0) {
		fprintf(stderr, "nvme: no NVMe devices found (/dev/nvme0n1)\n");
		return 1;
	}
	if (fetch_id_ctrl(fd, &ctrl) < 0 || fetch_id_ns(fd, 1, &ns) < 0) {
		fprintf(stderr, "nvme: failed to identify controller/namespace\n");
		close(fd);
		return 1;
	}
	close(fd);

	trim_trailing_spaces(ctrl.sn, 20, sn);
	trim_trailing_spaces(ctrl.mn, 40, mn);
	trim_trailing_spaces(ctrl.fr, 8, fr);

	cap_mb = ((unsigned long)ns.nsze_lo * 512UL) / (1024UL * 1024UL);
	use_mb = ((unsigned long)ns.nuse_lo * 512UL) / (1024UL * 1024UL);
	lba_bytes = 1U << (ns.lbaf[ns.flbas & 0x0f].ds ? ns.lbaf[ns.flbas & 0x0f].ds : 9);

	sprintf(usage_str, "%lu.00 MB / %lu.00 MB", use_mb, cap_mb);
	sprintf(fmt_str, "%u B +  0 B", lba_bytes);

	printf("%-16s %-20s %-32s %-9s %-22s %-14s %-8s\n",
	       "Node", "SN", "Model", "Namespace", "Usage", "Format", "FW Rev");
	printf("%-16s %-20s %-32s %-9s %-22s %-14s %-8s\n",
	       "----------------", "--------------------",
	       "--------------------------------", "---------",
	       "----------------------", "--------------", "--------");
	printf("%-16s %-20s %-32s %-9d %-22s %-14s %-8s\n",
	       "/dev/nvme0n1", sn, mn, 1, usage_str, fmt_str, fr);
	return 0;
}

static int cmd_id_ctrl(const char *dev)
{
	struct nvme_id_ctrl ctrl;
	char sn[24], mn[44], fr[12];
	int fd = open_nvme_dev(dev);

	if (fd < 0) {
		fprintf(stderr, "nvme id-ctrl: cannot open %s\n", dev ? dev : "/dev/nvme0");
		return 1;
	}
	if (fetch_id_ctrl(fd, &ctrl) < 0) {
		fprintf(stderr, "nvme id-ctrl: NVME_IOCTL_ADMIN_CMD failed\n");
		close(fd);
		return 1;
	}
	close(fd);

	trim_trailing_spaces(ctrl.sn, 20, sn);
	trim_trailing_spaces(ctrl.mn, 40, mn);
	trim_trailing_spaces(ctrl.fr, 8, fr);

	printf("NVME Identify Controller:\n");
	printf("vid       : 0x%04x\n", ctrl.vid);
	printf("ssvid     : 0x%04x\n", ctrl.ssvid);
	printf("sn        : %s\n", sn);
	printf("mn        : %s\n", mn);
	printf("fr        : %s\n", fr);
	printf("rab       : %u\n", ctrl.rab);
	printf("ieee      : %02x%02x%02x\n", ctrl.ieee[0], ctrl.ieee[1], ctrl.ieee[2]);
	printf("cmic      : 0x%02x\n", ctrl.cmic);
	printf("mdts      : %u (128 KB)\n", ctrl.mdts);
	printf("cntlid    : 0x%04x\n", ctrl.cntlid);
	printf("ver       : 0x%08x (NVMe %u.%u.%u)\n",
	       ctrl.ver, (ctrl.ver >> 16) & 0xffff, (ctrl.ver >> 8) & 0xff, ctrl.ver & 0xff);
	printf("oacs      : 0x%04x (Format NVM)\n", ctrl.oacs);
	printf("wctemp    : %u K (%d C)\n", ctrl.wctemp, (int)ctrl.wctemp - 273);
	printf("cctemp    : %u K (%d C)\n", ctrl.cctemp, (int)ctrl.cctemp - 273);
	printf("sqes      : 0x%02x (64 bytes)\n", ctrl.sqes);
	printf("cqes      : 0x%02x (16 bytes)\n", ctrl.cqes);
	printf("maxcmd    : %u\n", ctrl.maxcmd);
	printf("nn        : %u\n", ctrl.nn);
	printf("oncs      : 0x%04x (Dataset Management / TRIM, Write Zeroes)\n", ctrl.oncs);
	printf("vwc       : 0x%02x (Volatile Write Cache Present)\n", ctrl.vwc);
	printf("subnqn    : %s\n", ctrl.subnqn);
	return 0;
}

static int cmd_id_ns(const char *dev, unsigned int nsid)
{
	struct nvme_id_ns ns;
	int fd = open_nvme_dev(dev);
	unsigned int ds;

	if (fd < 0) {
		fprintf(stderr, "nvme id-ns: cannot open %s\n", dev ? dev : "/dev/nvme0n1");
		return 1;
	}
	if (fetch_id_ns(fd, nsid, &ns) < 0) {
		fprintf(stderr, "nvme id-ns: NVME_IOCTL_ADMIN_CMD failed for nsid %u\n", nsid);
		close(fd);
		return 1;
	}
	close(fd);

	ds = ns.lbaf[0].ds ? ns.lbaf[0].ds : 9;
	printf("NVME Identify Namespace %u:\n", nsid);
	printf("nsze      : 0x%x (%u sectors / %u KB)\n",
	       ns.nsze_lo, ns.nsze_lo, ns.nsze_lo >> 1);
	printf("ncap      : 0x%x (%u sectors / %u KB)\n",
	       ns.ncap_lo, ns.ncap_lo, ns.ncap_lo >> 1);
	printf("nuse      : 0x%x (%u sectors / %u KB)\n",
	       ns.nuse_lo, ns.nuse_lo, ns.nuse_lo >> 1);
	printf("nsfeat    : 0x%02x (Deallocated/Unwritten Logical Block support)\n", ns.nsfeat);
	printf("nlbaf     : %u\n", ns.nlbaf);
	printf("flbas     : 0x%02x\n", ns.flbas);
	printf("dlfeat    : 0x%02x (Read Zeroes on Deallocated Blocks)\n", ns.dlfeat);
	printf("nguid     : %.16s\n", (char *)ns.nguid);
	printf("eui64     : %.8s\n", (char *)ns.eui64);
	printf("lbaf  0   : ms:%u   lbads:%u (%u B)  rp:0x%x (in use)\n",
	       ns.lbaf[0].ms, ds, 1U << ds, ns.lbaf[0].rp);
	return 0;
}

static int cmd_smart_log(const char *dev)
{
	struct nvme_smart_log log;
	struct nvme_passthru_cmd cmd;
	unsigned int temp_k;
	int fd = open_nvme_dev(dev);

	if (fd < 0) {
		fprintf(stderr, "nvme smart-log: cannot open %s\n", dev ? dev : "/dev/nvme0");
		return 1;
	}

	memset(&log, 0, sizeof(log));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_get_log_page;
	cmd.nsid = 0xffffffffU;
	cmd.addr = (unsigned long)&log;
	cmd.data_len = sizeof(log);
	cmd.cdw10 = NVME_LOG_SMART | (((sizeof(log) / 4) - 1) << 16);

	if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
		fprintf(stderr, "nvme smart-log: NVME_IOCTL_ADMIN_CMD failed\n");
		close(fd);
		return 1;
	}
	close(fd);

	temp_k = (unsigned int)log.temperature[0] | ((unsigned int)log.temperature[1] << 8);
	printf("Smart Log for NVME device:%s namespace-id:ffffffff\n",
	       dev ? dev : "nvme0");
	printf("critical_warning                    : 0x%02x\n", log.critical_warning);
	printf("temperature                         : %d C (%u K)\n",
	       (int)temp_k - 273, temp_k);
	printf("available_spare                     : %u%%\n", log.avail_spare);
	printf("available_spare_threshold           : %u%%\n", log.spare_thresh);
	printf("percentage_used                     : %u%%\n", log.percent_used);
	printf("data_units_read                     : %u (%u sectors / %u KB)\n",
	       log.data_units_read[0], log.sectors_read_total, log.sectors_read_total >> 1);
	printf("data_units_written                  : %u (%u sectors / %u KB)\n",
	       log.data_units_written[0], log.sectors_written_total, log.sectors_written_total >> 1);
	printf("host_read_commands                  : %u\n", log.host_reads[0]);
	printf("host_write_commands                 : %u\n", log.host_writes[0]);
	printf("controller_busy_time                : %u\n", log.ctrl_busy_time[0]);
	printf("power_cycles                        : %u\n", log.power_cycles[0]);
	printf("power_on_hours                      : %u\n", log.power_on_hours[0]);
	printf("unsafe_shutdowns                    : %u\n", log.unsafe_shutdowns[0]);
	printf("media_errors                        : %u\n", log.media_errors[0]);
	printf("num_err_log_entries                 : %u\n", log.num_err_log_entries[0]);
	printf("flush_commands                      : %u\n", log.flush_cmds);
	printf("dsm_trim_commands                   : %u (%u sectors trimmed)\n",
	       log.dsm_cmds, log.dsm_sectors_trimmed);
	return 0;
}

static int cmd_flush(const char *dev)
{
	struct nvme_passthru_cmd cmd;
	int fd = open_nvme_dev(dev ? dev : "/dev/nvme0n1");

	if (fd < 0) {
		fprintf(stderr, "nvme flush: cannot open device\n");
		return 1;
	}
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_cmd_flush;
	cmd.nsid = 1;
	if (ioctl(fd, NVME_IOCTL_IO_CMD, &cmd) < 0) {
		fprintf(stderr, "nvme flush: NVME_IOCTL_IO_CMD failed\n");
		close(fd);
		return 1;
	}
	close(fd);
	printf("NVMe Flush: success (NSID 1)\n");
	return 0;
}

static int cmd_dsm(int argc, char **argv)
{
	const char *dev = "/dev/nvme0n1";
	unsigned int slba = 0;
	unsigned int blocks = 8;
	int i, fd;
	struct nvme_dsm_range range;
	struct nvme_passthru_cmd cmd;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			slba = (unsigned int)atol(argv[++i]);
		} else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "-c") == 0) &&
			   i + 1 < argc) {
			blocks = (unsigned int)atol(argv[++i]);
		} else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--ad") == 0) {
			/* Deallocate attribute enabled by default */
		} else if (argv[i][0] != '-') {
			dev = argv[i];
		}
	}

	fd = open_nvme_dev(dev);
	if (fd < 0) {
		fprintf(stderr, "nvme dsm: cannot open %s\n", dev);
		return 1;
	}

	memset(&range, 0, sizeof(range));
	range.cattr = 0;
	range.nlb = blocks;
	range.slba_lo = slba;
	range.slba_hi = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_cmd_dsm;
	cmd.nsid = 1;
	cmd.addr = (unsigned long)&range;
	cmd.data_len = sizeof(range);
	cmd.cdw10 = 0; /* NR = 0 (1 range, 0-based) */
	cmd.cdw11 = NVME_DSMGMT_AD;

	if (ioctl(fd, NVME_IOCTL_IO_CMD, &cmd) < 0) {
		fprintf(stderr, "nvme dsm: Dataset Management (TRIM) failed on %s\n", dev);
		close(fd);
		return 1;
	}
	close(fd);
	printf("NVMe DSM (Deallocate/TRIM): success (slba=%u, blocks=%u on %s)\n",
	       slba, blocks, dev);
	return 0;
}

static int cmd_write_zeroes(int argc, char **argv)
{
	const char *dev = "/dev/nvme0n1";
	unsigned int slba = 0;
	unsigned int blocks = 1;
	int i, fd;
	struct nvme_passthru_cmd cmd;

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			slba = (unsigned int)atol(argv[++i]);
		} else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-b") == 0) &&
			   i + 1 < argc) {
			blocks = (unsigned int)atol(argv[++i]);
		} else if (argv[i][0] != '-') {
			dev = argv[i];
		}
	}
	if (blocks == 0)
		blocks = 1;

	fd = open_nvme_dev(dev);
	if (fd < 0) {
		fprintf(stderr, "nvme write-zeroes: cannot open %s\n", dev);
		return 1;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_cmd_write_zeroes;
	cmd.nsid = 1;
	cmd.cdw10 = slba;
	cmd.cdw12 = (blocks - 1) & 0xffff;

	if (ioctl(fd, NVME_IOCTL_IO_CMD, &cmd) < 0) {
		fprintf(stderr, "nvme write-zeroes: failed on %s\n", dev);
		close(fd);
		return 1;
	}
	close(fd);
	printf("NVMe Write Zeroes: success (slba=%u, blocks=%u on %s)\n",
	       slba, blocks, dev);
	return 0;
}

static int cmd_show_regs(void)
{
	FILE *fp = fopen("/proc/nvme", "r");
	char line[256];

	if (!fp) {
		fprintf(stderr, "nvme: cannot open /proc/nvme\n");
		return 1;
	}
	while (fgets(line, sizeof(line), fp))
		fputs(line, stdout);
	fclose(fp);
	return 0;
}

static int cmd_reset(const char *dev)
{
	int fd = open_nvme_dev(dev);
	if (fd < 0) {
		fprintf(stderr, "nvme reset: cannot open controller\n");
		return 1;
	}
	if (ioctl(fd, NVME_IOCTL_RESET, 0) < 0) {
		fprintf(stderr, "nvme reset: NVME_IOCTL_RESET failed\n");
		close(fd);
		return 1;
	}
	close(fd);
	printf("NVMe Controller Reset: success (AQ & IOQ reinitialized)\n");
	return 0;
}

static int cmd_fw_log(const char *dev)
{
	struct nvme_fw_slot_info_log log;
	struct nvme_passthru_cmd cmd;
	char s1[12], s2[12];
	int fd = open_nvme_dev(dev);

	if (fd < 0) {
		fprintf(stderr, "nvme fw-log: cannot open %s\n", dev ? dev : "/dev/nvme0");
		return 1;
	}

	memset(&log, 0, sizeof(log));
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nvme_admin_get_log_page;
	cmd.nsid = 0xffffffffU;
	cmd.addr = (unsigned long)&log;
	cmd.data_len = sizeof(log);
	cmd.cdw10 = NVME_LOG_FW_SLOT | (((sizeof(log) / 4) - 1) << 16);

	if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
		fprintf(stderr, "nvme fw-log: NVME_IOCTL_ADMIN_CMD failed\n");
		close(fd);
		return 1;
	}
	close(fd);

	trim_trailing_spaces(log.frs[0], 8, s1);
	trim_trailing_spaces(log.frs[1], 8, s2);

	printf("Firmware Log for device:%s\n", dev ? dev : "nvme0");
	printf("afi  : 0x%02x (Active Slot: %u, Next Reset Slot: %u)\n",
	       log.afi, log.afi & 0x07, (log.afi >> 4) & 0x07);
	printf("frs1 : %s [RO Factory]\n", s1[0] ? s1 : "-");
	printf("frs2 : %s [RW Updatable]\n", s2[0] ? s2 : "-");
	return 0;
}

static void usage(void)
{
	printf("nvme-cli 1.16 (SIX NVMe 1.4 Management Utility)\n"
	       "Usage: nvme <command> [<device>] [<args>]\n\n"
	       "Commands:\n"
	       "  list                  List all NVMe controllers and namespaces\n"
	       "  id-ctrl   [dev]       Send NVMe Identify Controller (CNS 0x01)\n"
	       "  id-ns     [dev]       Send NVMe Identify Namespace  (CNS 0x00)\n"
	       "  smart-log [dev]       Retrieve NVMe SMART / Health Log Page (LID 0x02)\n"
	       "  fw-log    [dev]       Retrieve NVMe Firmware Slot Info Log (LID 0x03)\n"
	       "  flush     [dev]       Submit NVMe Flush command (opcode 0x00)\n"
	       "  dsm       [dev]       Submit NVMe Dataset Management / TRIM (opcode 0x09)\n"
	       "                        Options: -s <slba> -b <blocks> [-d]\n"
	       "  write-zeroes [dev]    Submit NVMe Write Zeroes command (opcode 0x08)\n"
	       "                        Options: -s <slba> -c <blocks>\n"
	       "  show-regs             Display NVMe controller registers & SQ/CQ rings\n"
	       "  reset     [dev]       Reset NVMe controller queues\n");
}

int main(int argc, char **argv)
{
	const char *sub;
	const char *dev = NULL;
	unsigned int nsid = 1;
	int i;

	if (argc < 2) {
		usage();
		return 1;
	}

	sub = argv[1];
	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			nsid = (unsigned int)atol(argv[++i]);
		} else if (argv[i][0] != '-') {
			dev = argv[i];
		}
	}

	if (strcmp(sub, "list") == 0)
		return cmd_list();
	if (strcmp(sub, "id-ctrl") == 0)
		return cmd_id_ctrl(dev);
	if (strcmp(sub, "id-ns") == 0)
		return cmd_id_ns(dev, nsid);
	if (strcmp(sub, "smart-log") == 0 || strcmp(sub, "smart") == 0)
		return cmd_smart_log(dev);
	if (strcmp(sub, "fw-log") == 0)
		return cmd_fw_log(dev);
	if (strcmp(sub, "flush") == 0)
		return cmd_flush(dev);
	if (strcmp(sub, "dsm") == 0 || strcmp(sub, "trim") == 0)
		return cmd_dsm(argc, argv);
	if (strcmp(sub, "write-zeroes") == 0)
		return cmd_write_zeroes(argc, argv);
	if (strcmp(sub, "show-regs") == 0 || strcmp(sub, "status") == 0)
		return cmd_show_regs();
	if (strcmp(sub, "reset") == 0)
		return cmd_reset(dev);
	if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0 || strcmp(sub, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "nvme: unknown subcommand '%s'\n", sub);
	usage();
	return 1;
}
