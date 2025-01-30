// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
#include <asm/setup.h>
#endif

#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
#define INITRAMFS_STR_FIND "skip_initramf"
#define INITRAMFS_STR_REPLACE "want_initramf"
#define INITRAMFS_STR_LEN (sizeof(INITRAMFS_STR_FIND) - 1)

static char proc_command_line[COMMAND_LINE_SIZE];

static void proc_command_line_init(void) {
	char *offset_addr;

	strcpy(proc_command_line, saved_command_line);

	offset_addr = strstr(proc_command_line, INITRAMFS_STR_FIND);
	if (!offset_addr)
		return;

	memcpy(offset_addr, INITRAMFS_STR_REPLACE, INITRAMFS_STR_LEN);
}
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
extern int susfs_spoof_cmdline_or_bootconfig(struct seq_file *m);
#endif

static int cmdline_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	if (!susfs_spoof_cmdline_or_bootconfig(m)) {
		seq_putc(m, '\n');
		return 0;
	}
#endif
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
	seq_puts(m, proc_command_line);
#else
	seq_puts(m, saved_command_line);
#endif
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_cmdline_init(void)
{
#ifdef CONFIG_INITRAMFS_IGNORE_SKIP_FLAG
	proc_command_line_init();
#endif

	proc_create_single("cmdline", 0, NULL, cmdline_proc_show);
	return 0;
}
fs_initcall(proc_cmdline_init);
