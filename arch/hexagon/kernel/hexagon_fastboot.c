/*
 * Support for communicating with LK
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/io.h>

#ifdef  CONFIG_ARCH_MSM8960
#define MSM_SHARED_IMEM_BASE 0x2A03F000
#define RESTART_REASON_ADDR  (MSM_SHARED_IMEM_BASE + 0x65C)
#elif   CONFIG_ARCH_MSM8974
// #define MSM_SHARED_IMEM_BASE 0xFE805000
// #define RESTART_REASON_ADDR  (MSM_SHARED_IMEM_BASE + 0x65C)
#define  RESTART_REASON_ADDR (0x0FF00000 + 0x0) // should be start of unused memory
#else
#error  "MSM Platform must be defined!"
#endif

#define RECOVERY_MODE        0x77665502
#define FASTBOOT_MODE        0x77665500
#define REFLASH_MODE         0x11101986
#define REBOOT_MODE          0x05282013
#define KRAIT_PING           0x50494E47
#define KRAIT_GNOP           0x474E4F50

#define BUFFER_LEN           8 // we shouldn't be passing more than 8 characters + NULL

struct dentry *dirret,*fileret;
int filevalue;

struct fb_command {
	int val;
	char* str;
};

struct fb_command commands[] = {
	{RECOVERY_MODE, "RECOVERY_MODE"},
	{FASTBOOT_MODE, "FASTBOOT_MODE"},
	{REFLASH_MODE, "REFLASH_MODE"},
	{REBOOT_MODE, "REBOOT_MODE"},
	{KRAIT_PING, "KRAIT_PING"},
	{KRAIT_GNOP, "KRAIT_GNOP"}
};
static int cmdLength = sizeof(commands) / sizeof(struct fb_command);

void do_fastboot(int mode)
{
	uint32_t* addr = (uint32_t*)ioremap_nocache(RESTART_REASON_ADDR, 0x10000);
	uint32_t countdown = 3;
	writel(mode, (volatile void *)(addr));

	if (mode == KRAIT_PING) {
		printk("%s - INFO: waiting on KRAIT...", __func__);
		while (countdown-- > 0) {
			if (readl(addr) == KRAIT_GNOP) {
				printk("%s ALIVE!\n", __func__);
				return;
			}
			mdelay(100);
		}
		printk("%s DEAD!\n", __func__);
	}
}

/* list all arguments */
void print_arguments(void)
{
	int idx;

	printk(KERN_ALERT "Available commands (%d):\n", cmdLength);
	for (idx = 0; idx < cmdLength; ++idx)
		printk(KERN_ALERT "[%d] %s = %#08x\n", idx, commands[idx].str, commands[idx].val);
}

/* write file operation */
static ssize_t parse_argument(struct file *fp, const char __user *user_buffer,
                                size_t count, loff_t *position)
{
	int idx;
	long value;
	char buf[2];

	if (user_buffer == NULL)
		goto parser_error;

	buf[0] = user_buffer[0];
	buf[1] = '\0';
	// passing 0 as base should enable auto detect
	if (kstrtol(buf, 10, &value)) {
		goto parser_error;
	} 
	for (idx = 0; idx < cmdLength; ++idx)
	{
		if (value == idx) {
			do_fastboot(commands[idx].val);
			return count;
		}
	}

parser_error:
	printk("%s - ERROR: unrecognized command!\n", __func__);
	print_arguments();
	return count;
}

/* read file operation */
static ssize_t list_arguments(struct file *fp, char __user *user_buffer,
                                size_t count, loff_t *position)
{
	print_arguments();
	return 0;
}

static const struct file_operations fops_debug = {
	.read = list_arguments,
	.write = parse_argument,
};

static int __init init_debug(void)
{
	printk("Installing hexagon_fastboot\n");

	/* create a directory by the name hexagon in /sys/kernel/debugfs */
	dirret = debugfs_create_dir("hexagon", NULL);

	/* create a file in the above directory
	This requires read and write file operations */
	fileret = debugfs_create_file("fastboot", 0644, dirret, &filevalue, &fops_debug);

	return (0);
}
module_init(init_debug);

static void __exit exit_debug(void)
{
	/* removing the directory recursively which
	in turn cleans all the file */
	debugfs_remove_recursive(dirret);
}
module_exit(exit_debug);
