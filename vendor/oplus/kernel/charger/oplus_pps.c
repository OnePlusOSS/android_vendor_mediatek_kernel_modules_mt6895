/**********************************************************************************
 * Copyright (c)  2008-2020  Guangdong OPLUS Mobile Comm Corp., Ltd
 * 
 * Description: Charger IC management module for charger system framework.
 *              Manage all charger IC and define abstarct function flow.
 * Version    : 1.0
 * Date       : 2020-06-29
 * Author     : zhouhaikang@oplus.com
 * ------------------------------ Revision History: --------------------------------
 * <version>           <date>                <author>                       <desc>
 * Revision 1.0       2020-06-29        zhouhaikang@oplus.com          Created for new architecture
 * Revision 2.0       2021-08-01         Kong Fanhong                  PPS 150w
 ***********************************************************************************/
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/list.h>
#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/usb/typec.h>
#include <linux/usb/usbpd.h>
#endif
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/rtc.h>

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/gpio.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
#include <uapi/linux/sched/types.h>
#endif
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/spmi.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/qpnp/qpnp-adc.h>
#else
#include <uapi/linux/sched/types.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
#include <linux/batterydata-lib.h>
#include <linux/of_batterydata.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <linux/msm_bcl.h>
#endif
#endif

#include "oplus_charger.h"
#include "oplus_pps.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "charger_ic/oplus_short_ic.h"
#include "chargepump_ic/oplus_pps_cp.h"
#include "oplus_pps_ops_manager.h"

#ifdef CONFIG_OPLUS_EMMC_LOG
#include <soc/oplus/oplus_emmclog.h>
#endif /*CONFIG_OPLUS_EMMC_LOG*/

/*bidirect int flag*/
#define BIDIRECT_IRQ_EVNET_NUM	13

#define VBAT_OVP_FLAG_MASK				BIT(7)
#define VOUT_OVP_FLAG_MASK				BIT(5)
#define VBUS_OVP_FLAG_MASK				BIT(1)

#define IBUS_OCP_FLAG_MASK				BIT(7)
#define IBUS_UCP_FLAG_MASK				BIT(5)
#define PIN_DIAG_FAIL_FLAG_MASK			BIT(2)

#define VAC1_OVP_FLAG_MASK				BIT(7)
#define VAC2_OVP_FLAG_MASK				BIT(6)

#define SS_TIMEOUT_FLAG_MASK			BIT(6)
#define TSHUT_FLAG_MASK					BIT(2)
#define I2C_WDT_FLAG_MASK				BIT(0)

#define VBUS2OUT_ERRORHI_FLAG_MASK		BIT(4)
#define VBUS2OUT_ERRORLO_FLAG_MASK		BIT(3)

struct irqinfo {
	int mask;
	char except_info[30];
	int mark_except;
};

struct irqinfo sc8571_int_flag[BIDIRECT_IRQ_EVNET_NUM] = {
	{VBAT_OVP_FLAG_MASK,			"VBAT_OVP", 1},/*18*/
	{VOUT_OVP_FLAG_MASK, 			"VOUT_OVP", 1},
	{VBUS_OVP_FLAG_MASK, 			"VBUS_OVP ", 1},
	{IBUS_OCP_FLAG_MASK, 			"IBUS_OCP", 1},/*19*/
	{IBUS_UCP_FLAG_MASK, 			"IBUS_UCP", 1},
	{PIN_DIAG_FAIL_FLAG_MASK, 		"PIN_DIAG_FAIL", 1},
	{VAC1_OVP_FLAG_MASK,			"VAC1_OVP", 1},/*1A*/
	{VAC2_OVP_FLAG_MASK, 			"VAC2_OVP", 1},
	{SS_TIMEOUT_FLAG_MASK,			"SS_TIMEOUT", 1},/*1B*/
	{TSHUT_FLAG_MASK, 				"TSHUT", 1},
	{I2C_WDT_FLAG_MASK,				"I2C_WDT", 1},
	{VBUS2OUT_ERRORHI_FLAG_MASK,	"VBUS2OUT_ERRORHI", 1},/*1C*/
	{VBUS2OUT_ERRORLO_FLAG_MASK,	"VBUS2OUT_ERRORLO", 1},
};
extern void oplus_chg_sc8571_error(int report_flag, int *buf, int ret);

int oplus_pps_print_dbg_info(struct oplus_pps_chip *chip)
{
	int i = 0, j = 0;
	u8 value = 0;
	bool fg_send_info = false;
	int report_flag = 0;

	for (i = 0; i < 3; i++) {
		if ((sc8571_int_flag[i].mask & chip->int_column[0]) && sc8571_int_flag[i].mark_except) {
			fg_send_info = true;
			memcpy(chip->reg_dump, chip->int_column, sizeof(chip->int_column));
			printk("cp int happened %s\n", sc8571_int_flag[i].except_info);
			goto chg_exception;
		}
	}
	for (i = 3; i < 6; i++) {
		if ((sc8571_int_flag[i].mask & chip->int_column[1]) && sc8571_int_flag[i].mark_except) {
			fg_send_info = true;
			memcpy(chip->reg_dump, chip->int_column, sizeof(chip->int_column));
			printk("cp int happened %s\n", sc8571_int_flag[i].except_info);
			goto chg_exception;
		}
	}

	for (i = 6; i < 8; i++) {
		if ((sc8571_int_flag[i].mask & chip->int_column[2]) && sc8571_int_flag[i].mark_except) {
			fg_send_info = true;
			memcpy(chip->reg_dump, chip->int_column, sizeof(chip->int_column));
			printk("cp int happened %s\n", sc8571_int_flag[i].except_info);
			goto chg_exception;
		}
	}

	for (i = 8; i < 11; i++) {
		if ((sc8571_int_flag[i].mask & chip->int_column[3]) && sc8571_int_flag[i].mark_except) {
			fg_send_info = true;
			memcpy(chip->reg_dump, chip->int_column, sizeof(chip->int_column));
			printk("cp int happened %s\n", sc8571_int_flag[i].except_info);
			goto chg_exception;
		}
	}

	for (i = 11; i < 13; i++) {
		if ((sc8571_int_flag[i].mask & chip->int_column[4]) && sc8571_int_flag[i].mark_except) {
			fg_send_info = true;
			memcpy(chip->reg_dump, chip->int_column, sizeof(chip->int_column));
			printk("cp int happened %s\n", sc8571_int_flag[i].except_info);
			goto chg_exception;
		}
	}

	return 0;

chg_exception:
	if (fg_send_info) {
		report_flag |= (1 << 4);
		oplus_chg_sc8571_error(report_flag, NULL, 0);
	}

	pps_err("pps data[18~1C][%d %d %d %d %d %d], iic[%d, %d]\n",
	       chip->reg_dump[0], chip->reg_dump[1], chip->reg_dump[2], chip->reg_dump[3], chip->reg_dump[4], chip->reg_dump[5],
	       chip->pps_iic_err, chip->pps_iic_err_num);
	return 0;
}

int  __attribute__((weak)) oplus_pps_pd_exit(void)
{
	return 0;
}

extern int oplus_pps_get_src_cap(void);
static void oplus_pps_stop_work(struct work_struct *work);
static void oplus_pps_update_work(struct work_struct *work);
static int oplus_pps_psy_changed(struct oplus_pps_chip *chip);
static int oplus_pps_get_vbat0(struct oplus_pps_chip *chip);
void oplus_pps_r_init(struct oplus_pps_chip *chip);
void oplus_pps_count_init(struct oplus_pps_chip *chip);
void oplus_pps_cancel_update_work_sync(void);

static struct oplus_pps_chip g_pps_chip;

static const char * const pps_strategy_soc[] = {
	[BATT_CURVE_SOC_RANGE_MIN]		= "strategy_soc_range_min",
	[BATT_CURVE_SOC_RANGE_LOW]		= "strategy_soc_range_low",
	[BATT_CURVE_SOC_RANGE_MID_LOW]	= "strategy_soc_range_mid_low",
	[BATT_CURVE_SOC_RANGE_MID]		= "strategy_soc_range_mid",
	[BATT_CURVE_SOC_RANGE_MID_HIGH]	= "strategy_soc_range_mid_high",
	[BATT_CURVE_SOC_RANGE_HIGH]		= "strategy_soc_range_high",
};

static const char * const pps_strategy_temp[] = {
	[BATT_CURVE_TEMP_RANGE_LITTLE_COLD]	= "strategy_temp_little_cold",
	[BATT_CURVE_TEMP_RANGE_COOL]		= "strategy_temp_cool",
	[BATT_CURVE_TEMP_RANGE_LITTLE_COOL]	= "strategy_temp_little_cool",
	[BATT_CURVE_TEMP_RANGE_NORMAL_LOW]	= "strategy_temp_normal_low",
	[BATT_CURVE_TEMP_RANGE_NORMAL_HIGH]	= "strategy_temp_normal_high",
	[BATT_CURVE_TEMP_RANGE_WARM]		= "strategy_temp_warm",
};

static const char * const strategy_low_curr_full[] = {
	[LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL]	= "strategy_temp_little_cool",
	[LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW]	= "strategy_temp_normal_low",
	[LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH]	= "strategy_temp_normal_high",
};

static int oplus_pps_parse_charge_strategy(struct oplus_pps_chip *chip)
{
	int rc;
	int length = 0;
	int soc_tmp[7] = {0, 15, 30, 50, 75, 85, 95};
	int rang_temp_tmp[7] = {0, 50, 120, 200, 350, 440, 510};
	int high_temp_tmp[6] = {425, 430, 435, 400, 415, 420};
	int high_curr_tmp[6] = {4000, 3000, 2000, 3000, 4000, 4000};
	int low_curr_temp_tmp[4] = {120, 200, 300, 440};

	struct device_node *node;

	node = chip->dev->of_node;
	rc = of_property_read_u32(node, "oplus,pps_support_type",
	                          &chip->pps_support_type);
	if (rc || chip->pps_support_type == 0) {
		chip->pps_support_type = 0;
		return -ENODEV;
	} else {
		pps_err("oplus,pps_support_type is %d\n", chip->pps_support_type);
	}

	rc = of_property_read_u32(node, "oplus,pps_support_third", &chip->pps_support_third);
	if (rc) {
		chip->pps_support_third = false;
	} else {
		pps_err("oplus,pps_support_third is %d\n", chip->pps_support_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_warm_allow_vol", &chip->limits.pps_warm_allow_vol);
	if (rc) {
		chip->limits.pps_warm_allow_vol = 4000;
	} else {
		pps_err("oplus,pps_warm_allow_vol is %d\n", chip->limits.pps_warm_allow_vol);
	}
	rc = of_property_read_u32(node, "oplus,pps_warm_allow_soc", &chip->limits.pps_warm_allow_soc);
	if (rc) {
		chip->limits.pps_warm_allow_soc = 50;
	} else {
		pps_err("oplus,pps_warm_allow_soc is %d\n", chip->limits.pps_warm_allow_soc);
	}

	rc = of_property_read_u32(node, "oplus,pps_strategy_normal_current",
	                          &chip->limits.pps_strategy_normal_current);
	if (rc) {
		chip->limits.pps_strategy_normal_current = 6000;
	} else {
		pps_err("oplus,pps_strategy_normal_current is %d\n",
		          chip->limits.pps_strategy_normal_current);
	}

	rc = of_property_read_u32(node, "oplus,pps_over_high_or_low_current",
	                          &chip->limits.pps_over_high_or_low_current);
	if (rc) {
		chip->limits.pps_over_high_or_low_current = -EINVAL;
	} else {
		pps_err("oplus,pps_over_high_or_low_current is %d\n",
		          chip->limits.pps_over_high_or_low_current);
	}
	rc = of_property_read_u32(node, "oplus,pps_full_cool_sw_vbat", &chip->limits.pps_full_cool_sw_vbat);
	if (rc) {
		chip->limits.pps_full_cool_sw_vbat = 4430;
	} else {
		pps_err("oplus,pps_full_cool_sw_vbat is %d\n", chip->limits.pps_full_cool_sw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_normal_sw_vbat", &chip->limits.pps_full_normal_sw_vbat);
	if (rc) {
		chip->limits.pps_full_normal_sw_vbat = 4495;
	} else {
		pps_err("oplus,pps_full_normal_sw_vbat %d\n", chip->limits.pps_full_normal_sw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_normal_hw_vbat", &chip->limits.pps_full_normal_hw_vbat);
	if (rc) {
		chip->limits.pps_full_normal_hw_vbat = 4500;
	} else {
		pps_err("oplus,pps_full_normal_hw_vbat is %d\n", chip->limits.pps_full_normal_hw_vbat);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_cool_sw_vbat_third", &chip->limits.pps_full_cool_sw_vbat_third);
	if (rc) {
		chip->limits.pps_full_cool_sw_vbat_third = 4430;
	} else {
		pps_err("oplus,pps_full_cool_sw_vbat_third is %d\n", chip->limits.pps_full_cool_sw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_normal_sw_vbat_third", &chip->limits.pps_full_normal_sw_vbat_third);
	if (rc) {
		chip->limits.pps_full_normal_sw_vbat_third = 4430;
	} else {
		pps_err("oplus,pps_full_normal_sw_vbat_third is %d\n", chip->limits.pps_full_normal_sw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_normal_hw_vbat_third", &chip->limits.pps_full_normal_hw_vbat_third);
	if (rc) {
		chip->limits.pps_full_normal_hw_vbat_third = 4440;
	} else {
		pps_err("oplus,pps_full_normal_hw_vbat_third is %d\n", chip->limits.pps_full_normal_hw_vbat_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_ffc_vbat", &chip->limits.pps_full_ffc_vbat);
	if (rc) {
		chip->limits.pps_full_ffc_vbat = 4420;
	} else {
		pps_err("oplus,pps_full_ffc_vbat is %d\n", chip->limits.pps_full_ffc_vbat);
	}

	rc = of_property_read_u32(node, "oplus,pps_full_warm_vbat", &chip->limits.pps_full_warm_vbat);
	if (rc) {
		chip->limits.pps_full_warm_vbat = 4150;
	} else {
		pps_err("oplus,pps_full_warm_vbat is %d\n", chip->limits.pps_full_warm_vbat);
	}

	rc = of_property_read_u32(node, "oplus,pps_timeout_third", &chip->limits.pps_timeout_third);
	if (rc) {
		chip->limits.pps_timeout_third = 9000;
	} else {
		pps_err("oplus,pps_timeout_third is %d\n", chip->limits.pps_timeout_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_timeout_oplus", &chip->limits.pps_timeout_oplus);
	if (rc) {
		chip->limits.pps_timeout_oplus = 7200;
	} else {
		pps_err("oplus,pps_timeout_oplus is %d\n", chip->limits.pps_timeout_oplus);
	}

	rc = of_property_read_u32(node, "oplus,pps_ibat_over_third", &chip->limits.pps_ibat_over_third);
	if (rc) {
		chip->limits.pps_ibat_over_third = 4000;
	} else {
		pps_err("oplus,pps_ibat_over_third is %d\n", chip->limits.pps_ibat_over_third);
	}

	rc = of_property_read_u32(node, "oplus,pps_ibat_over_oplus", &chip->limits.pps_ibat_over_oplus);
	if (rc) {
		chip->limits.pps_ibat_over_oplus = 17000;
	} else {
		pps_err("oplus,pps_ibat_over_oplus is %d\n", chip->limits.pps_ibat_over_oplus);
	}
	rc = of_property_read_u32(node, "oplus,pps_bcc_max_curr", &chip->bcc_max_curr);
	if (rc) {
		chip->bcc_max_curr = 15000;
	} else {
		pps_err("oplus,pps_bcc_max_curr is %d\n", chip->bcc_max_curr);
	}

	rc = of_property_read_u32(node, "oplus,pps_bcc_min_curr", &chip->bcc_min_curr);
	if (rc) {
		chip->bcc_min_curr = 1000;
	} else {
		pps_err("oplus,pps_bcc_min_curr is %d\n", chip->bcc_min_curr);
	}

	rc = of_property_read_u32(node, "oplus,pps_bcc_exit_curr", &chip->bcc_exit_curr);
	if (rc) {
		chip->bcc_exit_curr = 1000;
	} else {
		pps_err("oplus,pps_bcc_exit_curr is %d\n", chip->bcc_exit_curr);
	}

	rc = of_property_count_elems_of_size(node, "oplus,pps_strategy_batt_high_temp", sizeof(u32));
	if (rc >= 0) {
		length = rc;
		if (length > 6)
			length = 6;
		rc = of_property_read_u32_array(node, "oplus,pps_strategy_batt_high_temp", (u32 *)high_temp_tmp, length);
		if (rc < 0) {
			pps_err("parse pps_strategy_batt_high_temp failed, rc=%d\n", rc);
		}
	} else {
		length = 6;
		pps_err("parse pps_strategy_batt_high_temp,of_property_count_elems_of_size rc=%d\n", rc);
	}

	chip->limits.pps_strategy_batt_high_temp0 = high_temp_tmp[0];
	chip->limits.pps_strategy_batt_high_temp1 = high_temp_tmp[1];
	chip->limits.pps_strategy_batt_high_temp2 = high_temp_tmp[2];
	chip->limits.pps_strategy_batt_low_temp0 = high_temp_tmp[3];
	chip->limits.pps_strategy_batt_low_temp1 = high_temp_tmp[4];
	chip->limits.pps_strategy_batt_low_temp2 = high_temp_tmp[5];
	pps_err(", length = %d, high_temp[%d, %d, %d, %d, %d, %d]\n",
		length, high_temp_tmp[0], high_temp_tmp[1], high_temp_tmp[2], high_temp_tmp[3], high_temp_tmp[4], high_temp_tmp[5]);

	rc = of_property_count_elems_of_size(node, "oplus,pps_strategy_high_current", sizeof(u32));
	if (rc >= 0) {
		length = rc;
		if (length > 6)
			length = 6;
		rc = of_property_read_u32_array(node, "oplus,pps_strategy_high_current", (u32 *)high_curr_tmp, length);
		if (rc < 0) {
			pps_err("parse pps_strategy_high_current failed, rc=%d\n", rc);
		}
	} else {
		length = 6;
		pps_err("parse pps_strategy_high_current,of_property_count_elems_of_size rc=%d\n", rc);
	}
	chip->limits.pps_strategy_high_current0 = high_curr_tmp[0];
	chip->limits.pps_strategy_high_current1 = high_curr_tmp[1];
	chip->limits.pps_strategy_high_current2 = high_curr_tmp[2];
	chip->limits.pps_strategy_low_current0 = high_curr_tmp[3];
	chip->limits.pps_strategy_low_current1 = high_curr_tmp[4];
	chip->limits.pps_strategy_low_current2 = high_curr_tmp[5];
	pps_err(",length = %d, high_current[%d, %d, %d, %d, %d, %d]\n",
		length, high_curr_tmp[0], high_curr_tmp[1], high_curr_tmp[2], high_curr_tmp[3], high_curr_tmp[4], high_curr_tmp[5]);

	rc = of_property_count_elems_of_size(node, "oplus,pps_charge_strategy_temp", sizeof(u32));
	if (rc >= 0) {
		chip->limits.pps_strategy_temp_num = rc;
		if (chip->limits.pps_strategy_temp_num > 7)
			chip->limits.pps_strategy_temp_num = 7;
		rc = of_property_read_u32_array(node, "oplus,pps_charge_strategy_temp", (u32 *)rang_temp_tmp, chip->limits.pps_strategy_temp_num);
		if (rc < 0) {
			pps_err("parse pps_charge_strategy_temp failed, rc=%d\n", rc);
		}
	} else {
		chip->limits.pps_strategy_temp_num = 7;
		pps_err("parse pps_strategy_temp_num,of_property_count_elems_of_size rc=%d\n", rc);
	}
	chip->limits.pps_batt_over_low_temp = rang_temp_tmp[0];
	chip->limits.pps_little_cold_temp = rang_temp_tmp[1];
	chip->limits.pps_cool_temp = rang_temp_tmp[2];
	chip->limits.pps_little_cool_temp = rang_temp_tmp[3];
	chip->limits.pps_normal_low_temp = rang_temp_tmp[4];
	chip->limits.pps_normal_high_temp = rang_temp_tmp[5];
	chip->limits.pps_batt_over_high_temp = rang_temp_tmp[6];
	pps_err(",pps_charge_strategy_temp num = %d, [%d, %d, %d, %d, %d, %d, %d]\n",
		chip->limits.pps_strategy_temp_num, rang_temp_tmp[0], rang_temp_tmp[1], rang_temp_tmp[2], rang_temp_tmp[3],
		rang_temp_tmp[4], rang_temp_tmp[5], rang_temp_tmp[6]);
	chip->limits.default_pps_normal_high_temp = chip->limits.pps_normal_high_temp;
	chip->limits.default_pps_normal_low_temp = chip->limits.pps_normal_low_temp;
	chip->limits.default_pps_little_cool_temp = chip->limits.pps_little_cool_temp;
	chip->limits.default_pps_cool_temp = chip->limits.pps_cool_temp;
	chip->limits.default_pps_little_cold_temp = chip->limits.pps_little_cold_temp;

	rc = of_property_count_elems_of_size(node, "oplus,pps_charge_strategy_soc", sizeof(u32));
	if (rc >= 0) {
		chip->limits.pps_strategy_soc_num = rc;
		if (chip->limits.pps_strategy_soc_num > 7)
			chip->limits.pps_strategy_soc_num = 7;
		rc = of_property_read_u32_array(node, "oplus,pps_charge_strategy_soc", (u32 *)soc_tmp, chip->limits.pps_strategy_soc_num);
		if (rc < 0) {
			pps_err("parse pps_charge_strategy_soc failed, rc=%d\n", rc);
		}
	} else {
		chip->limits.pps_strategy_soc_num = 7;
		pps_err("parse pps_charge_strategy_soc,of_property_count_elems_of_size rc=%d\n", rc);
	}

	chip->limits.pps_strategy_soc_over_low = soc_tmp[0];
	chip->limits.pps_strategy_soc_min = soc_tmp[1];
	chip->limits.pps_strategy_soc_low = soc_tmp[2];
	chip->limits.pps_strategy_soc_mid_low = soc_tmp[3];
	chip->limits.pps_strategy_soc_mid = soc_tmp[4];
	chip->limits.pps_strategy_soc_mid_high = soc_tmp[5];
	chip->limits.pps_strategy_soc_high = soc_tmp[6];
	pps_err(",pps_charge_strategy_soc num = %d, [%d, %d, %d, %d, %d, %d, %d]\n", chip->limits.pps_strategy_soc_num,
		soc_tmp[0], soc_tmp[1], soc_tmp[2], soc_tmp[3], soc_tmp[4], soc_tmp[5], soc_tmp[6]);

	rc = of_property_count_elems_of_size(node, "oplus,pps_low_curr_full_strategy_temp", sizeof(u32));

	if (rc >= 0) {
		length = rc;
		if (length > 4)
			length = 4;
		rc = of_property_read_u32_array(node, "oplus,pps_low_curr_full_strategy_temp", (u32 *)low_curr_temp_tmp, length);
		if (rc < 0) {
			pps_err("parse pps_low_curr_full_strategy_temp failed, rc=%d\n", rc);
		}
	} else {
		length = 4;
		pps_err("parse pps_low_curr_full_strategy_temp,of_property_count_elems_of_size rc=%d\n", rc);
	}

	chip->limits.pps_low_curr_full_cool_temp = low_curr_temp_tmp[0];
	chip->limits.pps_low_curr_full_little_cool_temp = low_curr_temp_tmp[1];
	chip->limits.pps_low_curr_full_normal_low_temp = low_curr_temp_tmp[2];
	chip->limits.pps_low_curr_full_normal_high_temp = low_curr_temp_tmp[3];
	pps_err(",length = %d, low_curr_temp[%d, %d, %d, %d]\n", length, low_curr_temp_tmp[0],
		low_curr_temp_tmp[1], low_curr_temp_tmp[2], low_curr_temp_tmp[3]);

	chip->pps_power = 0;

	return rc;
}

static int oplus_pps_parse_resistense_strategy(struct oplus_pps_chip *chip)
{
	int rc, i = 0;
	int length = 7;
	int r_tmp[PPS_R_ROW_NUM] = {85, 10, 10, 15, 15, 15, 15};
	PPS_R_LIMIT limit_tmp = {280, 200, 140, 90, 50};

	struct device_node *node;
	if (!chip || !chip->pps_support_type) {
		return -ENODEV;
	}
	node = chip->dev->of_node;

	rc = of_property_read_u32(node, "oplus,pps_rmos_mohm", &chip->rmos_mohm);
	if (rc < 0) {
		pps_err("Could't find oplus,pps_rmos_mohm failed, rc=%d\n", rc);
		chip->rmos_mohm = 8;
	}

	rc = of_property_count_elems_of_size(node, "oplus,pps_r_limit", sizeof(u32));
	if (rc < 0) {
		pps_err("Could't find oplus,pps_r_limit failed, rc=%d\n", rc);
		chip->r_limit = limit_tmp;
	} else {
		length = rc;
		if (length > 5)
			length = 5;
		rc = of_property_read_u32_array(node, "oplus,pps_r_limit", (u32 *)&limit_tmp, length);
		if (rc < 0) {
			pps_err("parse pps_r_limit failed, rc=%d\n", rc);
		}
		chip->r_limit = limit_tmp;
	}
	pps_err("parse rmos_mohm = %d, r_limit[%d %d %d %d %d]\n", chip->rmos_mohm, chip->r_limit.limit_exit_mohm,
		chip->r_limit.limit_1a_mohm, chip->r_limit.limit_2a_mohm, chip->r_limit.limit_3a_mohm, chip->r_limit.limit_4a_mohm);
	rc = of_property_count_elems_of_size(node, "oplus,pps_r_default", sizeof(u32));
	if (rc < 0) {
		pps_err("Could't find oplus,pps_r_default failed, rc=%d\n", rc);
	}
	length = rc;
	if (length > 7)
		length = 7;
	pps_err("parse pps_r_default, length=%d rc =%d\n", length, rc);
	rc = of_property_read_u32_array(node, "oplus,pps_r_default", (u32 *)r_tmp, length);
	if (rc < 0) {
		pps_err("parse pps_r_default failed, rc=%d\n", rc);
	}
	chip->r_default.r0 = r_tmp[0];
	chip->r_default.r1 = r_tmp[1];
	chip->r_default.r2 = r_tmp[2];
	chip->r_default.r3 = r_tmp[3];
	chip->r_default.r4 = r_tmp[4];
	chip->r_default.r5 = r_tmp[5];
	chip->r_default.r6 = r_tmp[6];

	memcpy(&chip->r_avg, &chip->r_default, sizeof(PPS_R_INFO));

	pps_err("chip->r_avg[%d %d %d %d %d %d %d]\n",
		chip->r_avg.r0, chip->r_avg.r1, chip->r_avg.r2, chip->r_avg.r3, chip->r_avg.r4, chip->r_avg.r5, chip->r_avg.r6);
	pps_err("chip->r_default[%d %d %d %d %d %d %d]\n",
			chip->r_default.r0, chip->r_default.r1, chip->r_default.r2, chip->r_default.r3,
			chip->r_default.r4, chip->r_default.r5, chip->r_default.r6);

	return rc;
}

static int oplus_pps_parse_batt_curves_third(struct oplus_pps_chip *chip)
{
	struct device_node *node, *pps_node, *soc_node;
	int rc = 0, i, j, k, length;

	if (!chip || !chip->pps_support_type) {
		return -ENODEV;
	}

	node = chip->dev->of_node;

	pps_node = of_get_child_by_name(node, "pps_charge_third_strategy");

	for (i = 0; i < chip->limits.pps_strategy_soc_num - 1; i++) {
		soc_node = of_get_child_by_name(pps_node, pps_strategy_soc[i]);
		if (!soc_node) {
			pr_err("Can not find third %s node\n", pps_strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < chip->limits.pps_strategy_temp_num - 1; j++) {
			rc = of_property_count_elems_of_size(soc_node, pps_strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count third %s failed, rc=%d\n", pps_strategy_temp[j], rc);
				return rc;
			}
			length = rc;

			switch(i) {
			case BATT_CURVE_SOC_RANGE_MIN:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_LOW:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID_LOW:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID_HIGH:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_HIGH:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;

			default:
				break;
			}
		}
	}

	for (i = 0; i < chip->limits.pps_strategy_soc_num - 1; i++) {
		for (j = 0; j < chip->limits.pps_strategy_temp_num - 1; j++) {
			for (k = 0; k < chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curve_num; k++) {
				pps_err("third %s: i=%d,j=%d %d %d %d %d %d\n", __func__, i, j,
					chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves[k].target_vbus,
					chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves[k].target_vbat,
					chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves[k].target_ibus,
					chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves[k].exit,
					chip->batt_curves_third_soc[i].batt_curves_temp[j].batt_curves[k].target_time);
			}
		}
	}

	return rc;
}

static int oplus_pps_parse_low_curr_full_curves(struct oplus_pps_chip *chip)
{
	struct device_node *node, *full_node;
	int rc = 0, i, j, length;

	if (!chip || !chip->pps_support_type) {
		return -ENODEV;
	}

	node = chip->dev->of_node;

	full_node = of_get_child_by_name(node, "pps_charge_low_curr_full");
	if (!full_node) {
		pr_err("Can not find pps_charge_low_curr_full node\n");
		return -EINVAL;
	}


	for (i = 0; i < LOW_CURR_FULL_CURVE_TEMP_MAX; i++) {
		rc = of_property_count_elems_of_size(full_node, strategy_low_curr_full[i], sizeof(u32));
		if (rc < 0) {
			pr_err("Count low curr %s failed, rc=%d\n", strategy_low_curr_full[i], rc);
			return rc;
		}

		length = rc;

		switch(i) {
		case LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL:
			rc = of_property_read_u32_array(full_node, strategy_low_curr_full[i],
					(u32 *)chip->low_curr_full_curves_temp[i].full_curves,
					length);
			chip->low_curr_full_curves_temp[i].full_curve_num = length/3;
			break;
		case LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW:
			rc = of_property_read_u32_array(full_node, strategy_low_curr_full[i],
					(u32 *)chip->low_curr_full_curves_temp[i].full_curves,
					length);
			chip->low_curr_full_curves_temp[i].full_curve_num = length/3;
			break;
		case LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH:
			rc = of_property_read_u32_array(full_node, strategy_low_curr_full[i],
					(u32 *)chip->low_curr_full_curves_temp[i].full_curves,
					length);
			chip->low_curr_full_curves_temp[i].full_curve_num = length/3;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < LOW_CURR_FULL_CURVE_TEMP_MAX; i++) {
		for (j = 0; j < chip->low_curr_full_curves_temp[i].full_curve_num; j++) {
			pps_err(": i = %d,  %d %d %d\n", i,
				chip->low_curr_full_curves_temp[i].full_curves[j].iterm,
				chip->low_curr_full_curves_temp[i].full_curves[j].vterm,
				chip->low_curr_full_curves_temp[i].full_curves[j].exit);
		}
	}
	return rc;
}

static int oplus_pps_parse_batt_curves_oplus(struct oplus_pps_chip *chip)
{
	struct device_node *node, *pps_node, *soc_node;
	int rc = 0, i, j, k, length;

	if (!chip || !chip->pps_support_type) {
		return -ENODEV;
	}
	node = chip->dev->of_node;

	pps_node = of_get_child_by_name(node, "pps_charge_oplus_strategy");
	if (!pps_node) {
		pr_err("Can not find pps_charge_oplus_strategy node\n");
		return -EINVAL;
	}
	for (i = 0; i < chip->limits.pps_strategy_soc_num - 1; i++) {
		soc_node = of_get_child_by_name(pps_node, pps_strategy_soc[i]);
		if (!soc_node) {
			pr_err("Can not find oplus pps %s node\n", pps_strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < chip->limits.pps_strategy_temp_num - 1; j++) {
			rc = of_property_count_elems_of_size(soc_node, pps_strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				pr_err("Count oplus %s failed, rc=%d\n", pps_strategy_temp[j], rc);
				return rc;
			}

			length = rc;

			switch(i) {
			case BATT_CURVE_SOC_RANGE_MIN:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_LOW:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID_LOW:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_MID_HIGH:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			case BATT_CURVE_SOC_RANGE_HIGH:
				rc = of_property_read_u32_array(soc_node, pps_strategy_temp[j],
						(u32 *)chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves,
						length);
				chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num = length/5;
				break;
			default:
				break;
			}
		}
	}

	for (i = 0; i < chip->limits.pps_strategy_soc_num - 1; i++) {
		for (j = 0; j < chip->limits.pps_strategy_temp_num - 1; j++) {
			for (k = 0; k < chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curve_num; k++) {
				pps_err("%s:oplus i = %d,j = %d  %d %d %d %d %d\n", __func__, i, j,
					chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves[k].target_vbus,
					chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves[k].target_vbat,
					chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves[k].target_ibus,
					chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves[k].exit,
					chip->batt_curves_oplus_soc[i].batt_curves_temp[j].batt_curves[k].target_time);
			}
		}
	}

	return rc;
}

int oplus_pps_parse_dt(struct oplus_pps_chip *chip)
{
	int rc;
	struct device_node *node;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	node = chip->dev->of_node;
	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -ENODEV;
	}

	oplus_pps_parse_charge_strategy(chip);
	oplus_pps_parse_resistense_strategy(chip);
	oplus_get_pps_ops_name_from_dt(node);
	oplus_pps_parse_batt_curves_third(chip);
	oplus_pps_parse_batt_curves_oplus(chip);
	oplus_pps_parse_low_curr_full_curves(chip);

	return 0;
}

int oplus_pps_get_curve_vbus(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbus;
}

int oplus_pps_get_curve_ibus(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
}

int oplus_pps_get_curve_time(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_time;
}

int oplus_pps_get_next_curve_time(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index+1].target_time;
}

int oplus_pps_get_last_curve_vbat(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index - 1].target_vbat;
}

int oplus_pps_get_curve_vbat(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbat;
}

int oplus_pps_get_next_curve_vbus(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index+1].target_vbus;
}

int curve_cooldown_volt(struct oplus_pps_chip *chip, int cool_down)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[cool_down].target_ibus;
}

int oplus_pps_get_next_curve_ibus(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	return chip->batt_curves.batt_curves[chip->batt_curve_index+1].target_ibus;
}

static int oplus_pps_get_pps_status(struct oplus_pps_chip *chip)
{
	u32 pps_status;
	int volt, cur;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (!chip->ops->get_pps_status) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	pps_status = chip->ops->get_pps_status();
	if (pps_status < 0) {
		pps_err("pps get pdo status fail\n");
		return -EINVAL;
	}

	if (PD_PPS_STATUS_VOLT(pps_status) == 0xFFFF
			|| PD_PPS_STATUS_CUR(pps_status) == 0xFF) {
		pps_err("get adapter pps status fail\n");
		return -EINVAL;
	}

	volt = PD_PPS_STATUS_VOLT(pps_status) * 20;
	cur = PD_PPS_STATUS_CUR(pps_status) * 50;

	chip->charger_output_volt = volt;
	chip->charger_output_current = cur;

	return 0;
}

static int oplus_pps_get_charging_data(struct oplus_pps_chip *chip)
{
	int ret = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	oplus_chg_disable_charge();
	oplus_chg_suspend_charger();

	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
	chip->ap_batt_current = oplus_gauge_get_batt_current();
	chip->current_adapter_max = chip->ops->get_pps_max_cur(oplus_pps_get_curve_vbus(chip));

	chip->batt_input_current = abs(chip->ap_batt_current / (oplus_pps_get_curve_vbus(chip)/ 10000));
	if(oplus_pps_get_support_type() == PPS_SUPPORT_MCU) {
		chip->charger_output_volt = 0;
		chip->charger_output_current = 0;
		chip->cp_master_ibus = 0;
		chip->cp_slave_ibus = 0;
		chip->ap_input_current = 0;
		chip->cp_master_vac = 0;
		chip->cp_slave_vac = 0;
		chip->cp_master_vout = 0;
		chip->cp_slave_vout = 0;
		chip->cp_master_tdie = 0;
		chip->cp_slave_tdie = 0;
		chip->ap_input_volt = chip->ops->pps_get_cp_master_vbus();
		chip->slave_input_volt = chip->ops->pps_get_cp_slave_vbus();
	} else {
		oplus_pps_get_pps_status(chip);
		chip->cp_master_ibus = chip->ops->pps_get_cp_master_ibus();
		chip->cp_slave_ibus = chip->ops->pps_get_cp_slave_ibus();
		chip->ap_input_current = chip->cp_master_ibus + chip->cp_slave_ibus;
		chip->ap_input_volt = chip->ops->pps_get_cp_master_vbus();
		chip->slave_input_volt = chip->ops->pps_get_cp_slave_vbus();
		chip->cp_master_vac = chip->ops->pps_get_cp_master_vac();
		chip->cp_slave_vac = chip->ops->pps_get_cp_slave_vac();
		chip->cp_master_vout = chip->ops->pps_get_cp_master_vout();
		chip->cp_slave_vout = chip->ops->pps_get_cp_slave_vout();
		chip->cp_master_tdie = chip->ops->pps_get_cp_master_tdie();
		chip->cp_slave_tdie = chip->ops->pps_get_cp_slave_tdie();
	}
	chip->vbat0 = oplus_pps_get_vbat0(chip);
	pps_err(" [%d, %d, %d, %d, %d], [%d, %d, %d, %d, %d, %d]\n",
			chip->ap_batt_volt, chip->ap_batt_current, chip->ap_input_volt, chip->ap_input_current,
			chip->charger_output_volt, chip->charger_output_current, chip->cp_master_ibus,
			chip->cp_slave_ibus, chip->cp_master_tdie, chip->cp_slave_tdie, chip->vbat0);
	return 0;
}

void oplus_pps_read_ibus(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops || !chip->ops->pps_get_cp_master_ibus || !chip->ops->pps_get_cp_master_ibus) {
		chip->cp_master_ibus = 0;
		chip->cp_slave_ibus = 0;
	} else {
		chip->cp_master_ibus = chip->ops->pps_get_cp_master_ibus();
		chip->cp_slave_ibus = chip->ops->pps_get_cp_master_ibus();
	}
}


int oplus_pps_get_master_ibus(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return 0;
	}

	return chip->cp_master_ibus;
}


int oplus_pps_get_slave_ibus(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type) {
		return 0;
	}
	return chip->cp_slave_ibus;
}

static int oplus_pps_check_bcc_curr(struct oplus_pps_chip *chip)
{
	int index_vbat, index_min = 0, index_max = 0, bcc_min = 0, bcc_max = 0;
	int batt_temp_plugin;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}
	index_vbat = chip->batt_curves.batt_curves[chip->batt_curve_index].target_vbat;

	for (index_min = 0; index_min < chip->batt_curves.batt_curve_num; index_min++) {
		if (index_vbat == chip->batt_curves.batt_curves[index_min].target_vbat) {
			pps_err("index_min = %d found!\n", index_min);
			break;
		}
	}

	if (index_min >= chip->batt_curves.batt_curve_num) {
		index_min = chip->batt_curves.batt_curve_num - 1;
	}
	for (index_max = index_min + 1; index_max < chip->batt_curves.batt_curve_num; index_max++) {
		if (index_vbat != chip->batt_curves.batt_curves[index_max].target_vbat) {
			index_max = index_max - 1;
			pps_err("index_max = %d found!\n", index_max);
			break;
		}
	}
	if (index_max < index_min) {
		index_max = index_min;
	}
	if (index_max >= chip->batt_curves.batt_curve_num) {
		index_max = chip->batt_curves.batt_curve_num - 1;
	}

	bcc_max = chip->batt_curves.batt_curves[index_min].target_ibus * oplus_pps_get_curve_vbus(chip) / 10000 / 100;
	bcc_min = chip->batt_curves.batt_curves[index_max].target_ibus * oplus_pps_get_curve_vbus(chip) / 10000 / 100;

	if ((bcc_min - BCC_CURRENT_MIN) < BCC_CURRENT_MIN)
		bcc_min = BCC_CURRENT_MIN;
	else
		bcc_min = bcc_min - BCC_CURRENT_MIN;

	chip->bcc_max_curr = bcc_max;
	chip->bcc_min_curr = bcc_min;
	pps_err("result index_vbat = %d,  [%d, %d, %d, %d]\n", index_vbat, chip->batt_curve_index,
				chip->bcc_max_curr, chip->bcc_min_curr, chip->bcc_exit_curr);
	return 0;
}

static int oplus_pps_choose_curves(struct oplus_pps_chip *chip)
{
	int i;
	int batt_soc_plugin, batt_temp_plugin;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();

	if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_min) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_MIN;
	} else if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_low) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_LOW;
	} else if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_mid_low) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_MID_LOW;
	} else if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_mid) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_MID;
	} else if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_mid_high) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_MID_HIGH;
	} else if (chip->ap_batt_soc <= chip->limits.pps_strategy_soc_high) {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_HIGH;

	} else {
		batt_soc_plugin = BATT_CURVE_SOC_RANGE_MAX;
		pps_err("batt soc high, stop pps\n");
		chip->pps_stop_status = PPS_STOP_VOTER_OTHER_ABORMAL;
		return -EINVAL;
	}

	if (chip->ap_batt_temperature < chip->limits.pps_batt_over_low_temp) {
		pps_err("batt temp low, stop pps\n");
        return -EINVAL;
	} else if (chip->ap_batt_temperature < chip->limits.pps_little_cold_temp) { /*0-5C*/
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_LITTLE_COLD;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COLD;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COLD;
	} else if (chip->ap_batt_temperature < chip->limits.pps_cool_temp) { /*5-12C*/
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_COOL;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_COOL;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
	} else if (chip->ap_batt_temperature < chip->limits.pps_little_cool_temp) { /*12-16C*/
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_LITTLE_COOL;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COOL;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
	} else if (chip->ap_batt_temperature < chip->limits.pps_normal_low_temp) {/*16-35C*/
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_NORMAL_LOW;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_LOW;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_LOW;
	} else if (chip->ap_batt_temperature < chip->limits.pps_normal_high_temp) {/*25C-43C*/
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_NORMAL_HIGH;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_HIGH;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_HIGH;
	} else if (chip->ap_batt_temperature < chip->limits.pps_batt_over_high_temp) {
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_WARM;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_WARM;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_WARM;
	} else {
		pps_err("batt temp high, stop pps\n");
		chip->pps_stop_status = PPS_STOP_VOTER_TBATT_OVER;
		batt_temp_plugin = BATT_CURVE_TEMP_RANGE_MAX;
		return -EINVAL;
	}

	if(chip->pps_adapter_type == PPS_ADAPTER_THIRD) {
		memcpy(&chip->batt_curves,
				   chip->batt_curves_third_soc[batt_soc_plugin].batt_curves_temp[batt_temp_plugin].batt_curves, sizeof(struct batt_curves));
	} else {
		memcpy(&chip->batt_curves,
				   chip->batt_curves_oplus_soc[batt_soc_plugin].batt_curves_temp[batt_temp_plugin].batt_curves, sizeof(struct batt_curves));
	}

	pps_err("[%d, %d, %d, %d, %d, %d, %d]", chip->pps_adapter_type, chip->ap_batt_temperature, chip->ap_batt_soc, batt_soc_plugin,
		batt_temp_plugin, chip->pps_temp_cur_range, chip->pps_fastchg_batt_temp_status);

	for (i=0; i < chip->batt_curves.batt_curve_num; i++) {
		pr_err("i= %d, %s: %d %d %d %d %d\n", i, __func__,
		       chip->batt_curves.batt_curves[i].target_vbus,
		       chip->batt_curves.batt_curves[i].target_vbat,
		       chip->batt_curves.batt_curves[i].target_ibus,
		       chip->batt_curves.batt_curves[i].exit,
		       chip->batt_curves.batt_curves[i].target_time);
	}

	chip->batt_curve_index = 0;
	while (chip->ops->get_pps_max_cur(oplus_pps_get_curve_vbus(chip)) < oplus_pps_get_curve_ibus(chip)
		&& chip->batt_curve_index < chip->batt_curves.batt_curve_num) {
		pps_err("oplus_pps_get_curve_vbus = %d, get_pps_max_cur =  %d, oplus_pps_get_curve_ibus = %d, batt_curve_index = %d\r\n",
	        oplus_pps_get_curve_vbus(chip), chip->ops->get_pps_max_cur(oplus_pps_get_curve_vbus(chip)), oplus_pps_get_curve_ibus(chip), chip->batt_curve_index);

		chip->batt_curve_index++;
		chip->timer.batt_curve_time = 0;
	}
	if (chip->batt_curve_index >= chip->batt_curves.batt_curve_num) {
		chip->batt_curve_index = chip->batt_curves.batt_curve_num - 1;
	}
	chip->current_batt_curve = chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
	chip->bcc_exit_curr = chip->batt_curves.batt_curves[chip->batt_curves.batt_curve_num - 1].target_ibus * (oplus_pps_get_curve_vbus(chip)/ 10000);
	oplus_pps_check_bcc_curr(chip);

	return 0;
}

static int oplus_pps_variables_init(struct oplus_pps_chip *chip, int status)
{
	int ret = 0;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	pps_err("status:%d\n", status);
	oplus_pps_set_chg_status(PPS_CHARGERING);
	oplus_pps_r_init(chip);
	oplus_pps_count_init(chip);
	chip->pps_adapter_type = status;
	chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NATURAL;
	chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
	ret = oplus_pps_choose_curves(chip);
	if (ret < 0) {
		return ret;
	}

	oplus_pps_get_charging_data(chip);

	if(status == PPS_ADAPTER_THIRD) {
		oplus_chg_pps_cp_mode_init(PPS_BYPASS_MODE);
		chip->cp_pmid2vout_enable = true;
		chip->timer.pps_timeout_time = chip->limits.pps_timeout_third;
	} else {
		oplus_chg_pps_cp_mode_init(PPS_SC_MODE);
		chip->cp_pmid2vout_enable = false;
		chip->timer.pps_timeout_time = chip->limits.pps_timeout_oplus;
	}
	chip->pps_dummy_started = false;
	chip->pps_status = OPLUS_PPS_STATUS_START;
	chip->pps_low_curr_full_temp_status = LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW;
	chip->pps_stop_status = PPS_STOP_VOTER_NONE;
	chip->timer.batt_curve_time = 0;
	chip->timer.set_pdo_flag = 0;
	chip->timer.set_vcp_flag = 0;
	chip->timer.set_temp_flag = 0;
	chip->timer.check_ibat_flag = 0;
	chip->target_charger_volt = 0;
	chip->target_charger_current = 0;
	chip->target_charger_volt_pre = 0;
	chip->target_charger_current_pre = 0;
	chip->ask_charger_volt_last = 0;
	chip->ask_charger_current_last = 0;
	chip->ask_charger_volt = 5500;
	chip->ask_charger_current = 0;
	chip->cp_master_ibus = 0;
	chip->cp_slave_ibus = 0;
	chip->cp_slave_enable = false;
	chip->cp_ibus_down = PPS_CURRENT_MAX;
	chip->cp_r_down = PPS_CURRENT_MAX;
	chip->cp_tdie_down = PPS_CURRENT_MAX;
	chip->current_bcc = PPS_CURRENT_MAX;

	chip->cp_master_abnormal = false;
	chip->cp_slave_abnormal = false;

	chip->timer.fastchg_timer = current_kernel_time();
	chip->timer.temp_timer = current_kernel_time();
	chip->timer.vcp_timer = current_kernel_time();
	chip->timer.pdo_timer = current_kernel_time();
	chip->timer.ibat_timer = current_kernel_time();
	chip->current_batt_curve
		= chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;

	return 0;
}

static int oplus_pps_charging_enable_master(struct oplus_pps_chip *chip, bool on)
{
	int status = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->cp_master_abnormal) {
			pps_err("cp_master_abnormal = %d, cp_slave_abnormal = %d, cp_master_ibus = %d, cp_slave_ibus = %d\n",
			chip->cp_master_abnormal,
			chip->cp_slave_abnormal,
			chip->cp_master_ibus,
			chip->cp_slave_ibus);
		return status;
	}
	if (on) {
		status = chip->ops->pps_mos_ctrl(1);
	} else {
		status = chip->ops->pps_mos_ctrl(0);
	}
	return status;
}

static int oplus_pps_charging_enable_slave(struct oplus_pps_chip *chip, bool on)
{
	int status = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (!chip->ops->pps_mos_slave_ctrl) {
		pr_err("%s, pps_mos_slave_ctrl null!\n", __func__);
		return -ENODEV;
	}

	if (on) {
		status = chip->ops->pps_mos_slave_ctrl(1);
	} else {
		status = chip->ops->pps_mos_slave_ctrl(0);
	}
	return status;
}

void oplus_pps_set_pps_mos_enable(bool enable)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	oplus_pps_charging_enable_master(chip, enable);
	oplus_pps_charging_enable_slave(chip, enable);
}

#define PPS_TEMP_OVER_COUNTS				2
#define PPS_TEMP_RANGE_THD					10
void oplus_pps_reset_temp_range(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		return;
	}

	chip->limits.pps_normal_high_temp = chip->limits.default_pps_normal_high_temp;
	chip->limits.pps_little_cold_temp = chip->limits.default_pps_little_cold_temp;
	chip->limits.pps_cool_temp = chip->limits.default_pps_cool_temp;
	chip->limits.pps_little_cool_temp = chip->limits.default_pps_little_cool_temp;
	chip->limits.pps_normal_low_temp = chip->limits.default_pps_normal_low_temp;
}

static int oplus_pps_set_current_warm_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = chip->limits.pps_strategy_normal_current;

	if (chip->limits.pps_batt_over_high_temp != -EINVAL
	    && vbat_temp_cur > chip->limits.pps_batt_over_high_temp) {
		chip->limits.pps_strategy_change_count++;
		if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
			chip->limits.pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_EXIT;
			ret = chip->limits.pps_over_high_or_low_current;
			pps_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur < chip->limits.pps_normal_high_temp) {
		chip->limits.pps_strategy_change_count++;
		if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
			chip->limits.pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_HIGH;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
			ret = chip->limits.pps_strategy_normal_current;
			oplus_pps_reset_temp_range(chip);
			oplus_pps_choose_curves(chip);
			chip->limits.pps_normal_high_temp += PPS_TEMP_RANGE_THD;
		}

	} else {
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_WARM;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_WARM;
		ret = chip->limits.pps_strategy_normal_current;
	}

	return ret;
}

static int oplus_pps_set_current_temp_normal_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = chip->limits.pps_strategy_normal_current;

	switch (chip->pps_fastchg_batt_temp_status) {
	case PPS_BAT_TEMP_NORMAL_HIGH:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp0) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
			ret = chip->limits.pps_strategy_high_current0;
		} else if (vbat_temp_cur >= chip->limits.pps_normal_low_temp) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.pps_strategy_normal_current;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_LOW;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_LOW;
			ret = chip->limits.pps_strategy_normal_current;
			oplus_pps_reset_temp_range(chip);
			chip->limits.pps_normal_low_temp += PPS_TEMP_RANGE_THD;
		}
		break;
	case PPS_BAT_TEMP_HIGH0:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp1) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
			ret = chip->limits.pps_strategy_high_current1;
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp0) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
			ret = chip->limits.pps_strategy_low_current0;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
			ret = chip->limits.pps_strategy_high_current0;
		}
		break;
	case PPS_BAT_TEMP_HIGH1:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp2) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
			ret = chip->limits.pps_strategy_high_current2;
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp1) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
			ret = chip->limits.pps_strategy_low_current1;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
			ret = chip->limits.pps_strategy_high_current1;
		}
		break;
	case PPS_BAT_TEMP_HIGH2:
		if (chip->limits.pps_normal_high_temp != -EINVAL
			&& vbat_temp_cur > chip->limits.pps_normal_high_temp) {
			chip->limits.pps_strategy_change_count++;
			if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
				chip->limits.pps_strategy_change_count = 0;
				if (chip->ap_batt_soc < chip->limits.pps_warm_allow_soc
					&& chip->ap_batt_volt < chip->limits.pps_warm_allow_vol) {
					chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_WARM;
					chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
					ret = chip->limits.pps_strategy_high_current2;
					oplus_pps_reset_temp_range(chip);
					oplus_pps_choose_curves(chip);
					chip->limits.pps_normal_high_temp -= PPS_TEMP_RANGE_THD;
				} else {
					pps_err("high temp_over:%d", vbat_temp_cur);
					chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_EXIT;
					ret = chip->limits.pps_over_high_or_low_current;
				}
			}
		} else if (chip->limits.pps_batt_over_high_temp != -EINVAL
		    && vbat_temp_cur > chip->limits.pps_batt_over_high_temp) {
			chip->limits.pps_strategy_change_count++;
			if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
				chip->limits.pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_EXIT;
				ret = chip->limits.pps_over_high_or_low_current;
				pps_err(" over_high temp_over:%d", vbat_temp_cur);
			}
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp2) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW2;
			ret = chip->limits.pps_strategy_low_current2;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
			ret = chip->limits.pps_strategy_high_current2;
		}
		break;
	case PPS_BAT_TEMP_LOW0:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp0) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH0;
			ret = chip->limits.pps_strategy_high_current0;
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp0) {/*T<39C*/
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.pps_strategy_normal_current;
			oplus_pps_reset_temp_range(chip);
			chip->limits.pps_strategy_batt_low_temp0 += PPS_TEMP_RANGE_THD;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
			ret = chip->limits.pps_strategy_low_current0;
		}
		break;
	case PPS_BAT_TEMP_LOW1:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp1) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH1;
			ret = chip->limits.pps_strategy_high_current2;
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp0) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW0;
			ret = chip->limits.pps_strategy_low_current0;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
			ret = chip->limits.pps_strategy_low_current1;
		}
		break;
	case PPS_BAT_TEMP_LOW2:
		if (vbat_temp_cur > chip->limits.pps_strategy_batt_high_temp2) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_HIGH2;
			ret = chip->limits.pps_strategy_high_current2;
		} else if (vbat_temp_cur < chip->limits.pps_strategy_batt_low_temp1) {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW1;
			ret = chip->limits.pps_strategy_low_current1;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LOW2;
			ret = chip->limits.pps_strategy_low_current2;
		}
		break;
	default:
		break;
	}
	pps_err("the ret: %d, the temp =%d, status = %d\r\n", ret, vbat_temp_cur, chip->pps_fastchg_batt_temp_status);
	return ret;
}

static int oplus_pps_set_current_temp_low_normal_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = chip->limits.pps_strategy_normal_current;

	if (vbat_temp_cur < chip->limits.pps_normal_low_temp
	    && vbat_temp_cur >= chip->limits.pps_little_cool_temp) { /*20C<=T<35C*/
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_LOW;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_LOW;
		ret = chip->limits.pps_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->limits.pps_normal_low_temp) {
			chip->limits.pps_normal_low_temp -= PPS_TEMP_RANGE_THD;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_HIGH;
			ret = chip->limits.pps_strategy_normal_current;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_HIGH;
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COOL;
			ret = chip->limits.pps_strategy_normal_current;
			oplus_pps_reset_temp_range(chip);
			chip->limits.pps_little_cool_temp += PPS_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_pps_set_current_temp_little_cool_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->limits.pps_little_cool_temp
	    && vbat_temp_cur >= chip->limits.pps_cool_temp) {/*12C<=T<20C*/
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
		ret = chip->limits.pps_strategy_normal_current;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COOL;
	} else if (vbat_temp_cur >= chip->limits.pps_little_cool_temp) {
			chip->limits.pps_little_cool_temp -= PPS_TEMP_RANGE_THD;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NORMAL_LOW;
			ret = chip->limits.pps_strategy_normal_current;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_NORMAL_LOW;
	} else {
		if (oplus_chg_get_ui_soc() <= 90) {
			chip->limits.pps_strategy_change_count++;
			if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
				chip->limits.pps_strategy_change_count = 0;
				chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
				chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
				oplus_pps_choose_curves(chip);
				pps_err(" switch temp range:%d", vbat_temp_cur);
			}
		} else {
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
			chip->pps_temp_cur_range = PPS_TEMP_RANGE_COOL;
		}
		ret = chip->limits.pps_strategy_normal_current;
		oplus_pps_reset_temp_range(chip);
		chip->limits.pps_cool_temp += PPS_TEMP_RANGE_THD;
	}

	return ret;
}

static int oplus_pps_set_current_temp_cool_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->limits.pps_batt_over_low_temp != -EINVAL
	    && vbat_temp_cur < chip->limits.pps_batt_over_low_temp) {
		chip->limits.pps_strategy_change_count++;
		if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
			chip->limits.pps_strategy_change_count = 0;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_EXIT;
			ret = chip->limits.pps_over_high_or_low_current;
			pps_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur < chip->limits.pps_cool_temp
	           && vbat_temp_cur >= chip->limits.pps_little_cold_temp) {/*5C <=T<12C*/
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_COOL;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
		ret = chip->limits.pps_strategy_normal_current;
	} else if (vbat_temp_cur >= chip->limits.pps_cool_temp) {
			chip->limits.pps_strategy_change_count++;
			if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
				chip->limits.pps_strategy_change_count = 0;
				if (oplus_chg_get_ui_soc() <= 90) {
					chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
					chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
					oplus_pps_choose_curves(chip);
					pps_err(" switch temp range:%d", vbat_temp_cur);
				} else {
					chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COOL;
					chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COOL;
				}
				oplus_pps_reset_temp_range(chip);
				chip->limits.pps_cool_temp -= PPS_TEMP_RANGE_THD;
			}

			ret = chip->limits.pps_strategy_normal_current;
	} else {
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COLD;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COLD;
		ret = chip->limits.pps_strategy_normal_current;
		oplus_pps_reset_temp_range(chip);
		chip->limits.pps_little_cold_temp += PPS_TEMP_RANGE_THD;
	}
	return ret;
}

static int oplus_pps_set_current_temp_little_cold_range(struct oplus_pps_chip *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->limits.pps_batt_over_low_temp != -EINVAL
	    && vbat_temp_cur < chip->limits.pps_batt_over_low_temp) {
		chip->limits.pps_strategy_change_count++;
		if (chip->limits.pps_strategy_change_count >= PPS_TEMP_OVER_COUNTS) {
			chip->limits.pps_strategy_change_count = 0;
			ret = chip->limits.pps_over_high_or_low_current;
			chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_EXIT;
			pps_err(" temp_over:%d", vbat_temp_cur);
		}
	} else if (vbat_temp_cur < chip->limits.pps_little_cold_temp) { /*0C<=T<5C*/
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_LITTLE_COLD;
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_LITTLE_COLD;
		ret = chip->limits.pps_strategy_normal_current;
	} else {
		chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_COOL;
		ret = chip->limits.pps_strategy_normal_current;
		chip->pps_temp_cur_range = PPS_TEMP_RANGE_COOL;
		oplus_pps_reset_temp_range(chip);
		chip->limits.pps_little_cold_temp -= PPS_TEMP_RANGE_THD;
	}

	return ret;
}

static int oplus_pps_get_batt_temp_curr(struct oplus_pps_chip *chip)

{
	int ret = 0;
	int vbat_temp_cur = 350;

	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}
	vbat_temp_cur = chip->ap_batt_temperature;
	ret = chip->limits.pps_strategy_normal_current;

	switch (chip->pps_temp_cur_range) {
	case PPS_TEMP_RANGE_WARM:
		ret = oplus_pps_set_current_warm_range(chip, vbat_temp_cur);
		break;
	case PPS_TEMP_RANGE_NORMAL_HIGH:
		ret = oplus_pps_set_current_temp_normal_range(chip, vbat_temp_cur);
		break;
	case PPS_TEMP_RANGE_NORMAL_LOW:
		ret = oplus_pps_set_current_temp_low_normal_range(chip, vbat_temp_cur);
		break;
	case PPS_TEMP_RANGE_LITTLE_COOL:
		ret = oplus_pps_set_current_temp_little_cool_range(chip, vbat_temp_cur);
		break;
	case PPS_TEMP_RANGE_COOL:
		ret = oplus_pps_set_current_temp_cool_range(chip, vbat_temp_cur);
		break;
	case PPS_TEMP_RANGE_LITTLE_COLD:
		ret = oplus_pps_set_current_temp_little_cold_range(chip, vbat_temp_cur);
		break;
	default:
		break;
	}

	pps_err("the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n",
	             ret, vbat_temp_cur, chip->pps_fastchg_batt_temp_status, chip->pps_temp_cur_range);

	chip->current_batt_temp = ret;
	return ret;
}

static void oplus_pps_check_low_curr_temp_status(struct oplus_pps_chip *chip)
{
	static int t_cnts = 0;
	int vbat_temp_cur = 350;

	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	vbat_temp_cur = chip->ap_batt_temperature;

	if (((vbat_temp_cur >= chip->limits.pps_low_curr_full_normal_high_temp)
		|| (vbat_temp_cur < chip->limits.pps_low_curr_full_cool_temp))
		&& (chip->pps_low_curr_full_temp_status != LOW_CURR_FULL_CURVE_TEMP_MAX)) {
		t_cnts++;
		if(t_cnts >= PPS_TEMP_OVER_COUNTS) {
			chip->pps_low_curr_full_temp_status = LOW_CURR_FULL_CURVE_TEMP_MAX;
			t_cnts = 0;
		}
	} else if ((vbat_temp_cur >= chip->limits.pps_low_curr_full_normal_low_temp)
	&& (chip->pps_low_curr_full_temp_status != LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH)) {
		t_cnts++;
		if(t_cnts >= PPS_TEMP_OVER_COUNTS) {
			chip->pps_low_curr_full_temp_status = LOW_CURR_FULL_CURVE_TEMP_NORMAL_HIGH;
			t_cnts = 0;
		}
	} else if ((vbat_temp_cur >= chip->limits.pps_low_curr_full_little_cool_temp)
	&& (chip->pps_low_curr_full_temp_status != LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW)) {
		t_cnts++;
		if(t_cnts >= PPS_TEMP_OVER_COUNTS) {
			chip->pps_low_curr_full_temp_status = LOW_CURR_FULL_CURVE_TEMP_NORMAL_LOW;
			t_cnts = 0;
		}
	} else if (chip->pps_low_curr_full_temp_status != LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL) {
		t_cnts++;
		if(t_cnts >= PPS_TEMP_OVER_COUNTS) {
			chip->pps_low_curr_full_temp_status = LOW_CURR_FULL_CURVE_TEMP_LITTLE_COOL;
			t_cnts = 0;
		}
	} else {
		t_cnts = 0;
	}
	pps_err("[%d, %d, %d, %d, %d]\n", chip->ap_batt_current, chip->ap_batt_volt, vbat_temp_cur,
		chip->pps_low_curr_full_temp_status, t_cnts);
}

static int oplus_pps_get_batt_curve_curr(struct oplus_pps_chip *chip)
{
	int curve_volt_change = 0, curve_time_change = 0;
	static int curve_ov_count = 0, curve_uc_count = 0;
	static unsigned int toal_curve_time = 0;
	if (chip->ap_batt_volt > oplus_pps_get_curve_vbat(chip)) {
		if (curve_ov_count > 3) {
			curve_volt_change = 1;
			curve_ov_count = 0;
		} else {
			curve_ov_count++;
		}
	} else {
		curve_ov_count = 0;
	}

	if (oplus_pps_get_curve_time(chip) > 0) {
		if ((curve_volt_change) && (chip->timer.batt_curve_time < (oplus_pps_get_curve_time(chip) + toal_curve_time))) {
			toal_curve_time = oplus_pps_get_curve_time(chip) + toal_curve_time - chip->timer.batt_curve_time;
		}

		if (chip->timer.batt_curve_time > (oplus_pps_get_curve_time(chip) + toal_curve_time)) {
			curve_time_change = 1;
			toal_curve_time = 0;
		}
	} else {
		toal_curve_time = 0;
	}

	pps_err(" [%d, %d, %d, %d, %d] [%d, %d, %d, %d, %d]\n",
		curve_volt_change, curve_time_change, chip->batt_curve_index, chip->ap_batt_volt, oplus_pps_get_curve_vbat(chip),
		chip->ap_input_current, chip->ask_charger_current, chip->timer.batt_curve_time, toal_curve_time, oplus_pps_get_curve_time(chip));
	if ((curve_volt_change) || (curve_time_change)) {
		chip->batt_curve_index++;
		chip->timer.batt_curve_time = 0;
		curve_volt_change = 0;
		curve_time_change = 0;
		if (chip->batt_curve_index >= chip->batt_curves.batt_curve_num) {
			chip->batt_curve_index = chip->batt_curves.batt_curve_num - 1;
			chip->pps_stop_status = PPS_STOP_VOTER_FULL;
		}
		chip->current_batt_curve = chip->batt_curves.batt_curves[chip->batt_curve_index].target_ibus;
		if ((chip->batt_curve_index > 0) && (oplus_pps_get_last_curve_vbat(chip) != oplus_pps_get_curve_vbat(chip)))
			oplus_pps_check_bcc_curr(chip);
		pps_err("pps_stop_status = %d, batt_curve_index = %d, batt_curve_num = %d\n",
			chip->pps_stop_status, chip->batt_curve_index, chip->batt_curves.batt_curve_num);
	}

	return 0;
}

static const int Cool_Down_Third_Curve[] = {3000, 1000, 1000, 1200, 1500, 1700, 2000, 2200, 2500, 2700, 3000};
static const int Cool_Down_Oplus_Curve[] = {8000, 1000, 1000, 1200, 1500, 1700, 2000, 2200, 2500, 2700, 3000, 3200, 3500,
                                           3700, 4000, 4500, 5000, 5500, 6000, 6300, 6500, 7000, 7500, 8000, 8500};

static int oplus_pps_get_cool_down_curr(struct oplus_pps_chip *chip)
{
	int cool_down = 0;
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}
	cool_down = oplus_chg_get_cool_down_status();
	if(chip->pps_adapter_type == PPS_ADAPTER_THIRD) {
		if (cool_down >= (sizeof(Cool_Down_Third_Curve)/sizeof(int))) {
			cool_down = sizeof(Cool_Down_Third_Curve)/sizeof(int) - 1;
		}
		chip->current_cool_down = Cool_Down_Third_Curve[cool_down];
	} else {
	if (cool_down >= (sizeof(Cool_Down_Oplus_Curve)/sizeof(int))) {
			cool_down = sizeof(Cool_Down_Oplus_Curve)/sizeof(int) - 1;
		}
		chip->current_cool_down = Cool_Down_Oplus_Curve[cool_down];
	}
	return 0;
}


static int oplus_pps_get_target_current(struct oplus_pps_chip *chip)
{
	int target_current_temp = 0;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	pps_err("[%d, %d, %d, %d, %d, %d, %d]\n", chip->current_batt_curve, chip->current_batt_temp,
		chip->current_cool_down, chip->current_bcc, chip->cp_ibus_down, chip->cp_tdie_down, chip->cp_r_down);

	target_current_temp = chip->current_batt_curve < chip->current_batt_temp ? chip->current_batt_curve : chip->current_batt_temp;
	target_current_temp = target_current_temp < chip->current_cool_down ? target_current_temp : chip->current_cool_down;
	target_current_temp = target_current_temp < chip->current_bcc ? target_current_temp : chip->current_bcc;
	target_current_temp = target_current_temp < chip->cp_ibus_down ? target_current_temp : chip->cp_ibus_down;
	target_current_temp = target_current_temp < chip->cp_tdie_down ? target_current_temp : chip->cp_tdie_down;
	target_current_temp = target_current_temp < chip->cp_r_down ? target_current_temp  : chip->cp_r_down;

	return target_current_temp;
}

static void oplus_pps_tick_timer(struct oplus_pps_chip *chip)
{
	struct timespec ts_current;
	/*char buf[1] = {0};*/

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	ts_current = current_kernel_time();
	if (oplus_chg_get_bcc_support() && (ts_current.tv_sec - chip->timer.fastchg_timer.tv_sec) >= UPDATE_FASTCHG_TIME) {
		/*oplus_gauge_fastchg_update_bcc_parameters(buf);*/
	}

	if ((ts_current.tv_sec - chip->timer.fastchg_timer.tv_sec) >= UPDATE_FASTCHG_TIME) {
		chip->timer.fastchg_timer = ts_current;
		if (oplus_pps_get_curve_time(chip) > 0) {
			chip->timer.batt_curve_time++;
		}
		if (chip->timer.pps_timeout_time > 0) {
			chip->timer.pps_timeout_time--;
		}
	}

	if ((ts_current.tv_sec - chip->timer.pdo_timer.tv_sec) >= UPDATE_PDO_TIME) {
		chip->timer.pdo_timer = ts_current;
		chip->timer.set_pdo_flag = 1;
	}

	if ((ts_current.tv_sec - chip->timer.vcp_timer.tv_sec) >= UPDATE_VCP_TIME) {
		chip->timer.vcp_timer = ts_current;
		chip->timer.set_vcp_flag = 1;
	}

	if ((ts_current.tv_sec - chip->timer.temp_timer.tv_sec) >= UPDATE_TEMP_TIME) {
		chip->timer.temp_timer = ts_current;
		chip->timer.set_temp_flag = 1;
	}

	if ((ts_current.tv_sec - chip->timer.ibat_timer.tv_sec) >= UPDATE_IBAT_TIME) {
		chip->timer.ibat_timer = ts_current;
		chip->timer.check_ibat_flag = 1;
	}
}


#define FULL_COUNTS_HW				3
#define FULL_COUNTS_COOL			6
#define FULL_COUNTS_SW				6
#define FULL_COUNTS_LOW_CURR		6

static void oplus_pps_check_sw_full(struct oplus_pps_chip *chip)
{
	int cool_sw_vth = 0, normal_sw_vth = 0, normal_hw_vth = 0;

	if (chip->pps_adapter_type == PPS_ADAPTER_THIRD) {
		cool_sw_vth = chip->limits.pps_full_cool_sw_vbat_third;
		normal_sw_vth = chip->limits.pps_full_normal_sw_vbat_third;
		normal_hw_vth = chip->limits.pps_full_normal_hw_vbat_third;
	} else {
		cool_sw_vth = chip->limits.pps_full_cool_sw_vbat;
		normal_sw_vth = chip->limits.pps_full_normal_sw_vbat;
		normal_hw_vth = chip->limits.pps_full_normal_hw_vbat;
	}

	if (chip->timer.pps_timeout_time == 0) {
		chip->pps_stop_status = PPS_STOP_VOTER_TIME_OVER;
	}

	if((chip->ap_batt_temperature < chip->limits.pps_cool_temp) &&(chip->ap_batt_volt > cool_sw_vth)) {
		chip->count.cool_fw++;
		if (chip->count.cool_fw >= FULL_COUNTS_COOL) {
			chip->count.cool_fw = 0;
			chip->pps_stop_status = PPS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.cool_fw = 0;
	}

	if((chip->ap_batt_temperature >= chip->limits.pps_cool_temp) && (chip->ap_batt_temperature < chip->limits.pps_batt_over_high_temp)) {
		if ((chip->ap_batt_volt > normal_sw_vth) && (chip->batt_curve_index == chip->batt_curves.batt_curve_num - 1)) {
			chip->count.sw_full++;
			if (chip->count.sw_full >= FULL_COUNTS_SW) {
				chip->count.sw_full = 0;
				chip->pps_stop_status = PPS_STOP_VOTER_FULL;
			}
		}

		if ((chip->ap_batt_volt > normal_hw_vth)) {
			chip->count.hw_full++;
			if (chip->count.hw_full >= FULL_COUNTS_HW) {
				chip->count.hw_full = 0;
				chip->pps_stop_status = PPS_STOP_VOTER_FULL;
			}
		}
	} else {
		chip->count.sw_full = 0;
		chip->count.hw_full = 0;
	}
	if((chip->pps_fastchg_batt_temp_status == PPS_BAT_TEMP_WARM) &&(chip->ap_batt_volt > chip->limits.pps_full_warm_vbat)) {
		chip->count.cool_fw++;
		if (chip->count.cool_fw >= FULL_COUNTS_COOL) {
			chip->count.cool_fw = 0;
			chip->pps_stop_status = PPS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.cool_fw = 0;
	}
	pps_err(" [%d, %d, %d, %d, %d, %d], [%d, %d, %d, %d, %d, %d]\n",
		chip->count.cool_fw, chip->count.sw_full, chip->count.hw_full, chip->ap_batt_volt,
		chip->ap_batt_temperature, chip->timer.pps_timeout_time, chip->pps_stop_status, cool_sw_vth,
		normal_sw_vth, normal_hw_vth, chip->batt_curve_index, chip->batt_curves.batt_curve_num);
}

static void oplus_pps_check_low_curr_full(struct oplus_pps_chip *chip)
{
	int i, temp_current, temp_vbatt, temp_status, iterm, vterm;
	bool low_curr = false;

	oplus_pps_check_low_curr_temp_status(chip);

	temp_current = -chip->ap_batt_current;
	temp_vbatt = chip->ap_batt_volt;
	temp_status = chip->pps_low_curr_full_temp_status;

	if(temp_status == LOW_CURR_FULL_CURVE_TEMP_MAX)
		return;

	for (i = 0; i < chip->low_curr_full_curves_temp[temp_status].full_curve_num; i++) {
		iterm = chip->low_curr_full_curves_temp[temp_status].full_curves[i].iterm;
		vterm = chip->low_curr_full_curves_temp[temp_status].full_curves[i].vterm;

		if ((temp_current <= iterm) && (temp_vbatt >= vterm)) {
			low_curr = true;
			break;
		}
	}

	if (low_curr) {
		chip->count.low_curr_full++;

		if (chip->count.low_curr_full > FULL_COUNTS_LOW_CURR) {
			chip->count.low_curr_full = 0;
			chip->pps_stop_status = PPS_STOP_VOTER_FULL;
		}
	} else {
		chip->count.low_curr_full = 0;
	}
}

int oplus_pps_get_ffc_vth(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return 4420;
	}
	return chip->limits.pps_full_ffc_vbat;
}

static void oplus_pps_check_full(struct oplus_pps_chip *chip)
{
	if (chip->pps_status == OPLUS_PPS_STATUS_CHECK) {
		oplus_pps_check_sw_full(chip);
		oplus_pps_check_low_curr_full(chip);
	}
}

void oplus_pps_count_init(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		return;
	}

	chip->count.cool_fw = 0;
	chip->count.sw_full = 0;
	chip->count.hw_full = 0;
	chip->count.low_curr_full = 0;
	chip->count.ibat_low = 0;
	chip->count.ibat_high = 0;
	chip->count.btb_high = 0;
	chip->count.tbatt_over = 0;
	chip->count.output_low = 0;
	chip->count.cable_over = 0;
	chip->count.ibus_over = 0;
	chip->count.tdie_over = 0;
	chip->count.tdie_exit = 0;
	pps_err("!!!!!");
}

void oplus_pps_r_init(struct oplus_pps_chip *chip)
{
	int i;
	if (!chip || !chip->pps_support_type) {
		return;
	}
	for (i = 0 ; i < PPS_R_AVG_NUM ; i++) {
		chip->r_column[i] = chip->r_default;
	}
	chip->r_avg = chip->r_default;
	pps_err("chip->r_avg[%d %d %d %d %d %d %d]\n",
		chip->r_avg.r0, chip->r_avg.r1, chip->r_avg.r2, chip->r_avg.r3, chip->r_avg.r4, chip->r_avg.r5, chip->r_avg.r6);

	pps_err("!!!!!");
}


static void oplus_pps_protection_check(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	oplus_pps_tick_timer(chip);
	oplus_pps_check_full(chip);
}

static int oplus_pps_get_vbat0(struct oplus_pps_chip *chip)
{
	int ret = 0;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if (chip->ops->get_vbat0_volt && chip->pps_status <= OPLUS_PPS_STATUS_OPEN_MOS) {
			ret = chip->ops->get_vbat0_volt();
			if (ret < 0)
				ret = 0;
	} else {
		ret = 0;
	}
	return ret;
}

static int oplus_pps_get_pps_pdo_vmin(struct oplus_pps_chip *chip)
{
	int pdo_vmin = 5500;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return pdo_vmin;
	}
	return pdo_vmin;
}

static void oplus_pps_master_enable_check(struct oplus_pps_chip *chip)
{
	int ret = 0, cnt_fail = 0;

	if (oplus_pps_get_support_type() != PPS_SUPPORT_2CP) {
		return;
	}

	for (cnt_fail = 0; cnt_fail < 10; cnt_fail++) {
		msleep(200);
		if (oplus_chg_get_pps_type() != PD_PPS_ACTIVE || oplus_pps_get_chg_status() != PPS_CHARGERING) {
			break;
		}
		if (chip->ops->pps_get_cp_master_ibus() < 250) {
			ret = oplus_pps_charging_enable_master(chip, true);
		} else {
			pps_err("OPLUS_PPS_STATUS_OPEN_MOS okay, cnt_fail = %d\n", cnt_fail);
			break;
		}
	}

	if (cnt_fail >= 10 && chip->ops->pps_get_cp_master_ibus() < 250) {
		chip->pps_stop_status = PPS_STOP_VOTER_PDO_ERROR;
		pps_err("OPLUS_PPS_STATUS_OPEN_MOS fail, cnt_fail = %d\n", cnt_fail);
	}
	chip->master_enable_err_num = cnt_fail;

	if(cnt_fail >= 1) {
		oplus_chg_sc8571_error((1 << PPS_REPORT_ERROR_CP_ENABLE), NULL, cnt_fail);
	}
}

static void oplus_pps_slave_enable_check(struct oplus_pps_chip *chip)
{
	int ret = 0, cnt_fail = 0;
	if (oplus_pps_get_support_type() != PPS_SUPPORT_2CP) {
		return;
	}

	if ((chip->ask_charger_current >= 1700) && (!chip->cp_slave_enable)) {
		oplus_pps_charging_enable_slave(chip, true);
		msleep(200);
		for (cnt_fail = 0; cnt_fail < 2; cnt_fail++) {
			if (oplus_chg_get_pps_type() != PD_PPS_ACTIVE || oplus_pps_get_chg_status() != PPS_CHARGERING) {
				break;
			}
			if (chip->ops->pps_get_cp_slave_ibus() < 250) {
				ret = oplus_pps_charging_enable_slave(chip, true);
				msleep(200);
				pps_err("pps_get_cp_slave_ibus < 250,, cnt_fail = %d\n", cnt_fail);
			} else {
				pps_err("slave enable okay, cnt_fail = %d\n", cnt_fail);
				break;
			}
		}
		if (cnt_fail >= 2 && chip->ops->pps_get_cp_slave_ibus() < 250) {
			chip->cp_ibus_down = PPS_CURRENT_3A;
			chip->cp_slave_enable = false;
			pps_err("slave enable fail, cnt_fail = %d\n", cnt_fail);
		} else {
			chip->cp_slave_enable = true;
		}
		chip->slave_enable_err_num = cnt_fail;

		if(cnt_fail >= 1) {
			oplus_chg_sc8571_error((1 << PPS_REPORT_ERROR_CP_ENABLE), NULL, cnt_fail);
		}
	} else if (chip->ask_charger_current < 1300) {
		oplus_pps_charging_enable_slave(chip, false);
		chip->cp_slave_enable = false;
	}

}

static int oplus_pps_action_status_start(struct oplus_pps_chip *chip)
{
	int update_size = 0, vbat = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	if(oplus_pps_get_support_type() == PPS_SUPPORT_MCU) {
		vbat = chip->vbat0 / 2;
	} else {
		vbat = chip->ap_batt_volt;
	}

	if (oplus_pps_get_curve_vbus(chip) == 20000) {
		chip->target_charger_volt = ((vbat * 4) / 100) * 100 + 600;
		chip->target_charger_current = 1000;
	} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
		chip->target_charger_volt = ((vbat * 2) / 100) * 100 + 300;
		chip->target_charger_current = 1000;
	} else {
		pps_err("Invalid argument!\n");
		chip->pps_stop_status = PPS_STOP_VOTER_PDO_ERROR;
		return -EINVAL;
	}

	if (abs(chip->ap_input_volt - chip->target_charger_volt) >= 5000) {
		update_size = 5000;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >= 2000) {
		update_size = 2000;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >= 1000) {
		update_size = 1000;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >= 500) {
		update_size = 500;
	} else if (abs(chip->ap_input_volt - chip->target_charger_volt) >= 200) {
		update_size = 200;
	} else {
		update_size = 100;
	}

	chip->ask_charger_current = chip->target_charger_current;
	if (chip->ap_input_volt < chip->target_charger_volt) {
		chip->ask_charger_volt += update_size;
	} else if (chip->ap_input_volt > (chip->target_charger_volt + 300)) {
		chip->ask_charger_volt -= update_size;
	} else {
		pps_err("pps chargering volt okay\n");
		chip->pps_status = OPLUS_PPS_STATUS_OPEN_MOS;
		if(chip->pps_adapter_type == PPS_ADAPTER_THIRD) {
			chip->pps_fastchg_started = false;
		} else {
			chip->pps_fastchg_started = true;
		}
		oplus_pps_psy_changed(chip);
	}

	chip->timer.work_delay = 200;
	return 0;
}

static int oplus_pps_action_open_mos(struct oplus_pps_chip *chip)
{
	int update_size = 0, ret = 0;
	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	ret = oplus_pps_charging_enable_master(chip, true);

	oplus_pps_master_enable_check(chip);

	chip->pps_status = OPLUS_PPS_STATUS_VOLT_CHANGE;
	chip->timer.batt_curve_time = 0;
	chip->timer.work_delay = 50;

	return 0;
}

static int oplus_pps_action_volt_change(struct oplus_pps_chip *chip)
{
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}
	oplus_pps_charging_enable_master(chip, true);

	chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
	if (oplus_pps_get_curve_vbus(chip) == 20000) {
		chip->target_charger_current = 1000;
	} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
		chip->target_charger_current = 1000;
	} else {
		pps_err("Invalid argument!\n");
		chip->pps_stop_status = PPS_STOP_VOTER_PDO_ERROR;
		return -EINVAL;
	}
/*
	if (chip->ask_charger_volt < chip->target_charger_volt) {
		chip->ask_charger_volt += 2000;
	}
	if (chip->ask_charger_volt >= chip->target_charger_volt) {
		chip->ask_charger_volt = chip->target_charger_volt;
		chip->pps_status = OPLUS_PPS_STATUS_CUR_CHANGE;
	}
*/
	chip->ask_charger_volt = chip->target_charger_volt;
	chip->pps_status = OPLUS_PPS_STATUS_CUR_CHANGE;

	chip->timer.work_delay = 200;

	return 0;
}

static int oplus_pps_action_curr_change(struct oplus_pps_chip *chip)
{
	int update_size = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}
	oplus_pps_charging_enable_master(chip, true);

	oplus_pps_get_batt_curve_curr(chip);
	oplus_pps_get_cool_down_curr(chip);
	oplus_pps_get_batt_temp_curr(chip);

	chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
	chip->target_charger_current = oplus_pps_get_target_current(chip);

	if (abs(chip->ask_charger_current - chip->target_charger_current) >= 300) {
		update_size = 300;
	} else {
		update_size = 50;
	}

	chip->ask_charger_volt = chip->target_charger_volt;

	if ((chip->ask_charger_current > chip->target_charger_current) &&
		((chip->ask_charger_current - chip->target_charger_current) >= update_size)) {
			chip->ask_charger_current -= update_size;
	} else if ((chip->target_charger_current > chip->ask_charger_current) &&
		((chip->target_charger_current - chip->ask_charger_current) >= update_size)) {
		if (chip->batt_input_current < chip->target_charger_current) {
				chip->ask_charger_current += update_size;
		} else {
			pps_err("pps chargering current 1 okay\n");
			chip->pps_status = OPLUS_PPS_STATUS_CHECK;
		}
	} else {
			chip->ask_charger_current = chip->target_charger_current;
			pps_err("pps chargering current 2 okay\n");
			chip->pps_status = OPLUS_PPS_STATUS_CHECK;
	}

	chip->timer.work_delay = 200;

	return 0;
}

static int oplus_pps_action_status_check(struct oplus_pps_chip *chip)
{
	static int curr_over_target_cnt = 0;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	oplus_pps_charging_enable_master(chip, true);

	oplus_pps_get_batt_curve_curr(chip);
	oplus_pps_get_cool_down_curr(chip);
	oplus_pps_get_batt_temp_curr(chip);

	chip->target_charger_volt = oplus_pps_get_curve_vbus(chip);
	chip->target_charger_current = oplus_pps_get_target_current(chip);

	if (chip->ask_charger_volt != chip->target_charger_volt) {
		chip->ask_charger_volt = chip->target_charger_volt;
		if (oplus_pps_get_curve_vbus(chip) == 20000) {
			chip->ask_charger_current = 500;
		} else if (oplus_pps_get_curve_vbus(chip) == 10000) {
			chip->ask_charger_current = 1000;
		} else {
			chip->pps_stop_status = PPS_STOP_VOTER_PDO_ERROR;
			pps_err("check curve Invalid argument!\n");
			return -EINVAL;
		}
		chip->pps_status = OPLUS_PPS_STATUS_VOLT_CHANGE;
	} else {
		if (chip->batt_input_current > chip->target_charger_current) {
			curr_over_target_cnt++;
		} else {
			curr_over_target_cnt = 0;
		}

		if (chip->target_charger_current < chip->target_charger_current_pre
				|| curr_over_target_cnt > 3) {
			chip->pps_status = OPLUS_PPS_STATUS_CUR_CHANGE;
			curr_over_target_cnt = 0;
		} else if (chip->target_charger_current > chip->target_charger_current_pre) {
			chip->pps_status = OPLUS_PPS_STATUS_CUR_CHANGE;
			curr_over_target_cnt = 0;
		} else {
			/*do nothing*/
		}
	}
	chip->timer.work_delay = 500;

	return 0;
}

static int oplus_pps_action_check(struct oplus_pps_chip *chip)
{
	int ret, update_size = 0, cnt_fail = 0;

	pps_err(" %d, %d, %d\n", chip->pps_status, chip->target_charger_volt, chip->target_charger_current);
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -ENODEV;
	}

	chip->target_charger_volt_pre = chip->target_charger_volt;
	chip->target_charger_current_pre = chip->target_charger_current;

	switch (chip->pps_status) {
	case OPLUS_PPS_STATUS_START:
		oplus_pps_action_status_start(chip);
		break;
	case OPLUS_PPS_STATUS_OPEN_MOS:
		oplus_pps_action_open_mos(chip);
		break;
	case OPLUS_PPS_STATUS_VOLT_CHANGE:
		oplus_pps_action_volt_change(chip);
		break;
	case OPLUS_PPS_STATUS_CUR_CHANGE:
		oplus_pps_action_curr_change(chip);
		break;
	case OPLUS_PPS_STATUS_CHECK:
		oplus_pps_action_status_check(chip);
		break;
	default:
		chip->pps_stop_status = PPS_STOP_VOTER_OTHER_ABORMAL;
		pps_err("wrong status!\n");
		return -EINVAL;
	}

	if (chip->ask_charger_volt < oplus_pps_get_pps_pdo_vmin(chip)) {
		chip->ask_charger_volt = oplus_pps_get_pps_pdo_vmin(chip);
	}
	pps_err("[%d, %d, %d, %d, %d, %d, %d]\n", chip->pps_status, chip->target_charger_volt, chip->target_charger_current,
	chip->ap_input_volt, chip->ask_charger_volt, chip->ask_charger_current, chip->ap_batt_current);

	return 0;
}

static int oplus_pps_set_pdo(struct oplus_pps_chip *chip)
{
	int ret = 0, max_cur = 0;

	if ((chip->ask_charger_volt != chip->ask_charger_volt_last)
	    || (chip->ask_charger_current != chip->ask_charger_current_last)) {
		chip->ask_charger_volt_last = chip->ask_charger_volt;
		chip->ask_charger_current_last = chip->ask_charger_current;
		chip->timer.set_pdo_flag = 1;
	}

	max_cur = chip->ops->get_pps_max_cur(chip->ask_charger_volt);
	if (chip->ask_charger_current > max_cur) {
		pps_err("pdo cannot support %dmA,max current:%d\n", chip->ask_charger_current, max_cur);
		chip->ask_charger_current = max_cur;
	}

	if (chip->timer.set_pdo_flag == 1) {
		oplus_pps_slave_enable_check(chip);
		if (oplus_chg_get_bcc_curr_done_status() == BCC_CURR_DONE_REQUEST) {
			oplus_chg_check_bcc_curr_done();
		}

		ret = chip->ops->pps_pdo_select(chip->ask_charger_volt, chip->ask_charger_current);
		if (ret) {
			pps_err("pps set pdo fail\n");
			chip->pps_stop_status = PPS_STOP_VOTER_PDO_ERROR;
			return -EINVAL;
		}
		chip->timer.set_pdo_flag = 0;
	}

	if (chip->ask_charger_current < 5000 && chip->cp_pmid2vout_enable == false && chip->ops->pps_cp_pmid2vout_enable) {
		/*chip->ops->pps_cp_pmid2vout_enable(true);*/
		chip->cp_pmid2vout_enable = true;
	}

	pps_err(" ask_volt, ask_cur: %d, %d\n", chip->ask_charger_volt, chip->ask_charger_current);

	return ret;
}

static void oplus_pps_voter_charging_stop(struct oplus_pps_chip *chip)
{
	int stop_voter = chip->pps_stop_status;
	pps_err(",pps_stop_status = %d\n", stop_voter);

	switch (stop_voter) {
	case PPS_STOP_VOTER_FULL:
		schedule_delayed_work(&chip->pps_stop_work, 0);
		break;
	case PPS_STOP_VOTER_TIME_OVER:
	case PPS_STOP_VOTER_PDO_ERROR:
	case PPS_STOP_VOTER_TBATT_OVER:
	case PPS_STOP_VOTER_IBAT_OVER:
	case PPS_STOP_VOTER_RESISTENSE_OVER:
	case PPS_STOP_VOTER_CABLE_OVER:
	case PPS_STOP_VOTER_OTHER_ABORMAL:
		schedule_delayed_work(&chip->pps_stop_work, 0);
		break;
	case PPS_STOP_VOTER_BTB_OVER:
	case PPS_STOP_VOTER_DISCONNECT_OVER:
	case PPS_STOP_VOTER_MMI_TEST:
	case PPS_STOP_VOTER_USB_TEMP:
	case PPS_STOP_VOTER_TDIE_OVER:
		schedule_delayed_work(&chip->pps_stop_work, 0);
		break;
	default:
		break;
	}
}

void oplus_pps_stop_disconnect(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}
	chip->pps_stop_status = PPS_STOP_VOTER_DISCONNECT_OVER;
		oplus_pps_voter_charging_stop(chip);
}
EXPORT_SYMBOL(oplus_pps_stop_disconnect);

void oplus_pps_stop_usb_temp(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}
	chip->pps_stop_status = PPS_STOP_VOTER_USB_TEMP;
	oplus_chg_sc8571_error((1 << PPS_REPORT_ERROR_USBTEMP_OVER), NULL, 0);
	oplus_pps_voter_charging_stop(chip);
}

void oplus_pps_set_mmi_status(bool mmi)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	if (mmi)
		chip->pps_stop_status &= ~PPS_STOP_VOTER_MMI_TEST;
	else
		chip->pps_stop_status = PPS_STOP_VOTER_MMI_TEST;
}

bool oplus_pps_voter_charging_start(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int stop_voter = chip->pps_stop_status;
	bool restart_voter = false;
	if (!chip || !chip->pps_support_type) {
		return restart_voter;
	}

	switch (stop_voter) {
	case PPS_STOP_VOTER_TBATT_OVER:
		if (oplus_pps_is_allow_real()) {
			restart_voter = true;
			chip->pps_stop_status &= ~PPS_STOP_VOTER_TBATT_OVER;
		}
		break;
	case PPS_STOP_VOTER_MMI_TEST:
		if (oplus_chg_get_mmi_value()) {
			restart_voter = true;
			chip->pps_stop_status &= ~PPS_STOP_VOTER_MMI_TEST;
		}
		break;
	default:
		break;
	}
	pps_err(" stop_voter = %d, restart_voter = %d\n", stop_voter, restart_voter);
	return restart_voter;
}

static int oplus_pps_psy_changed(struct oplus_pps_chip *chip)
{
	if(chip->pps_batt_psy) {
		power_supply_changed(chip->pps_batt_psy);
	}
	return 0;
}

int oplus_pps_get_ffc_started(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return false;
	}
	if (chip->pps_status == OPLUS_PPS_STATUS_FFC)
		return true;
	else
		return false;
}

int oplus_pps_get_pps_mos_started(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (chip->pps_status >= OPLUS_PPS_STATUS_OPEN_MOS)
		return true;
	else
		return false;
}

int oplus_pps_get_adapter_type(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return PPS_ADAPTER_UNKNOWN;
	}
	pps_err("pps_adapter_type = %d\n", chip->pps_adapter_type);

	return chip->pps_adapter_type;
}

void oplus_pps_set_pps_dummy_started(bool enable)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}

	if (enable) {
		chip->pps_dummy_started = true;
		chip->pps_adapter_type = PPS_ADAPTER_OPLUS_V2;
	} else {
		chip->pps_dummy_started = false;
		chip->pps_adapter_type = PPS_ADAPTER_UNKNOWN;
	}
	pps_err(" enable = %d\n", enable);
}

bool oplus_pps_get_pps_dummy_started(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		return false;
	} else {
		return chip->pps_dummy_started;
	}
}

bool oplus_pps_get_pps_fastchg_started(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type) {
		return false;
	} else {
		return chip->pps_fastchg_started;
	}
}

int oplus_pps_get_pps_disconnect(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return false;
	}

	pps_err(" pps_stop_status = %d\n", chip->pps_stop_status);
	if (chip->pps_stop_status == PPS_STOP_VOTER_DISCONNECT_OVER)
		return true;
	else
		return false;
}

void oplus_pps_reset_stop_status(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return;
	}
	pps_err(" pps_stop_status = %d\n", chip->pps_stop_status);
	chip->pps_stop_status = PPS_STOP_VOTER_NONE;
}

int oplus_pps_get_stop_status(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return PPS_STOP_VOTER_NONE;
	}

	pps_err(" pps_stop_status = %d\n", chip->pps_stop_status);
	return chip->pps_stop_status;
}

int oplus_pps_set_ffc_started(bool status) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return 0;
	}
	pps_err("status = %d\n", status);
	if (!status)
		chip->pps_status = OPLUS_PPS_STATUS_START;
	else
		chip->pps_status = OPLUS_PPS_STATUS_FFC;
	return 0;
}

static void oplus_pps_update_work(struct work_struct *work)
{
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip, update_pps_work.work);

	if (oplus_chg_get_pps_type() == PD_PPS_ACTIVE && (oplus_pps_get_chg_status() == PPS_CHARGERING)) {
		oplus_pps_get_charging_data(chip);
		oplus_pps_action_check(chip);
		oplus_pps_protection_check(chip);
		oplus_pps_set_pdo(chip);

		oplus_pps_voter_charging_stop(chip);

		if (chip->pps_stop_status == PPS_STOP_VOTER_NONE)
			schedule_delayed_work(&chip->update_pps_work, msecs_to_jiffies(chip->timer.work_delay));
	} else {
		chip->pps_stop_status = PPS_STOP_VOTER_TYPE_ERROR;
		schedule_delayed_work(&chip->pps_stop_work, 0);
	}
}

static void oplus_pps_ffc_work(struct work_struct *work)
{
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip,
						pps_ffc_work.work);

	if (oplus_pps_get_chg_status() == PPS_CHARGE_END
	    || oplus_pps_get_chg_status() == PPS_NOT_SUPPORT) {
		return;
	} else {
		oplus_pps_set_chg_status(PPS_CHARGE_END);
	}

	pps_err("oplus_pps_ffc_work\n");
	oplus_pps_charging_enable_master(chip, false);
	if(chip->pps_adapter_type != PPS_ADAPTER_THIRD) {
		oplus_pps_charging_enable_slave(chip, false);
	}
	msleep(100);
	chip->ops->pps_pdo_select(5000, 2000);
	oplus_chg_unsuspend_charger();
	oplus_chg_disable_charge();
	chip->pps_status = OPLUS_PPS_STATUS_FFC;

	oplus_chg_wake_update_work();
}

void oplus_pps_cancel_update_work_sync(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return;
	}
	cancel_delayed_work_sync(&chip->update_pps_work);
}
EXPORT_SYMBOL(oplus_pps_cancel_update_work_sync);

void oplus_pps_cancel_stop_work(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return;
	}
	cancel_delayed_work(&chip->pps_stop_work);
}

EXPORT_SYMBOL(oplus_pps_cancel_stop_work);

static void oplus_pps_stop_work(struct work_struct *work)
{
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip,
						pps_stop_work.work);

	if (oplus_pps_get_chg_status() == PPS_CHARGE_END
	    || oplus_pps_get_chg_status() == PPS_NOT_SUPPORT) {
		return;
	} else {
		oplus_pps_set_chg_status(PPS_CHARGE_END);
	}
	pps_err(" %d, %d\n", chip->pps_stop_status, chip->pps_adapter_type);
	oplus_pps_cancel_update_work_sync();

	oplus_pps_charging_enable_master(chip, false);
	if(chip->pps_adapter_type != PPS_ADAPTER_THIRD) {
		oplus_pps_charging_enable_slave(chip, false);
	}

	oplus_pps_pd_exit();
	oplus_pps_cp_reset();
	oplus_pps_hardware_init();

	if (chip->pps_stop_status == PPS_STOP_VOTER_MMI_TEST) {
		chip->pps_status = OPLUS_PPS_STATUS_START;
	} else if ((chip->pps_stop_status == PPS_STOP_VOTER_FULL) && (chip->pps_adapter_type == PPS_ADAPTER_OPLUS_V2)) {
		schedule_delayed_work(&chip->check_vbat_diff_work, PPS_VBAT_DIFF_TIME);
		oplus_chg_unsuspend_charger();
		oplus_chg_disable_charge();
		chip->pps_status = OPLUS_PPS_STATUS_FFC;
	} else {
		oplus_chg_unsuspend_charger();
		oplus_chg_enable_charge();
		chip->pps_status = OPLUS_PPS_STATUS_START;
	}
	if (chip->pps_adapter_type == PPS_ADAPTER_OPLUS_V2)
		chip->pps_dummy_started = true;
	chip->pps_fastchg_started = false;
	oplus_pps_set_mcu_vooc_mode();
	oplus_chg_wake_update_work();
	oplus_pps_cancel_stop_work();
}

static void oplus_pps_vbat_diff_work(struct work_struct *work)
{
	/*struct oplus_pps_chip *chip = &g_pps_chip;*/
	struct oplus_pps_chip *chip = container_of(work, struct oplus_pps_chip, check_vbat_diff_work.work);
	int volt_max = 4000, volt_min = 4000, diff = 0;

	if (!chip || !chip->pps_support_type) {
		return;
	}

	if (oplus_pps_get_pps_dummy_started()) {
		volt_max = oplus_gauge_get_batt_mvolts_2cell_max();
		volt_min = oplus_gauge_get_batt_mvolts_2cell_min();
		diff = abs(volt_max - volt_min);
		oplus_chg_sc8571_error((1 << PPS_REPORT_ERROR_VBAT_DIFF), NULL, diff);
	}
	pps_err("volt_max = %d volt_min = %d diff = %d dummy = %d\n", volt_max, volt_min, diff, oplus_pps_get_pps_dummy_started());
}

int oplus_pps_get_adsp_authenticate(void) {
	struct oplus_pps_chip *chip = &g_pps_chip;
	int auth_result = false;
	auth_result = chip->ops->pps_get_authentiate();
	if (auth_result < 0) {
		pps_err("oplus_svooc_pps_get_authenticate  fail\n");
		return -EINVAL;
	}
	return auth_result;
}

int oplus_pps_start(int authen)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int ret = 0, max_cur = 0;

	oplus_pps_set_mcu_pps_mode();
	oplus_vooc_mcu_reset();

	if(authen == PPS_ADAPTER_THIRD) {
		pps_err("PPS_ADAPTER_THIRD\n");
		ret = oplus_pps_variables_init(chip, PPS_ADAPTER_THIRD);
		if (ret)
			goto fail;
		schedule_delayed_work(&chip->update_pps_work, 0);
	} else {
		 ret = oplus_pps_variables_init(chip, PPS_ADAPTER_OPLUS_V2);
		 if (ret)
		 goto fail;
		 schedule_delayed_work(&chip->update_pps_work, 0);
	}

	return 0;

fail:
	oplus_pps_set_chg_status(PPS_CHARGE_END);
	oplus_pps_cp_reset();
	oplus_pps_hardware_init();
	oplus_pps_set_mcu_vooc_mode();
	chip->pps_dummy_started = false;
	chip->pps_fastchg_started = false;
	return -1;
}

int oplus_pps_get_chg_status(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int ret;

	if (!chip) {
		pps_err(", oplus_pps_chip null!\n");
		return PPS_NOT_SUPPORT;
	}

	if (chip->pps_support_type == 0) {
		return PPS_NOT_SUPPORT;
	}
	ret = chip->pps_chging;
	return ret;
}

int oplus_pps_check_third_pps_support(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || chip->pps_support_type == 0) {
		pps_err(", oplus_pps_chip null!\n");
		return false;
	}
	return chip->pps_support_third;
}

int oplus_pps_get_support_type(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int ret;

	if (!chip) {
		pps_err(", oplus_pps_chip null!\n");
		return PPS_SUPPORT_NOT;
	}

	if (chip->pps_support_type == 1) {
		return PPS_SUPPORT_MCU;
	} else if (chip->pps_support_type == 2) {
		return PPS_SUPPORT_2CP;
	} else if (chip->pps_support_type == 3) {
		return PPS_SUPPORT_VOOCPHY;
	} else {
		return PPS_SUPPORT_NOT;
	}
}

int oplus_pps_set_chg_status(int status)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return 0;
	}
	pps_err(":%d\n", status);

	chip->pps_chging = status;
	return 0;
}

void oplus_pps_hardware_init(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type || !chip->ops) {
		return;
	}
	if (!chip->ops->pps_cp_hardware_init) {
		pps_err(", pps_cp_hardware_init null!\n");
		return;
	}
	chip->ops->pps_cp_hardware_init();
}

void oplus_pps_cp_reset(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type || !chip->ops) {
		return;
	}
	if (!chip->ops->pps_cp_reset) {
		pps_err(", oplus_pps_cp_reset null!\n");
		return;
	}
	chip->ops->pps_cp_reset();
}

int oplus_pps_get_mcu_pps_mode(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops || !chip->ops->set_mcu_pps_mode) {
		return -EINVAL;
	} else {
		return chip->ops->get_mcu_pps_mode();
	}
}

void oplus_pps_set_mcu_pps_mode(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops || !chip->ops->set_mcu_pps_mode) {
		return;
	} else {
		chip->ops->set_mcu_pps_mode(true);
	}
}

void oplus_pps_set_mcu_vooc_mode(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops || !chip->ops->set_mcu_pps_mode) {
		return;
	} else {
		chip->ops->set_mcu_pps_mode(false);
	}
}

void oplus_pps_clear_dbg_info(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type) {
		return;
	}

	chip->pps_iic_err = 0;
	chip->pps_iic_err_num = 0;
	memset(chip->int_column, 0, DUMP_REG_CNT);
}

void oplus_pps_variables_reset(bool in)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return;
	}
	oplus_pps_set_chg_status(PPS_CHECKING);
	oplus_pps_set_ffc_started(false);
	oplus_pps_reset_stop_status();
	oplus_pps_r_init(chip);
	oplus_pps_count_init(chip);
	oplus_pps_reset_temp_range(chip);
	oplus_pps_set_power(OPLUS_PPS_POWER_CLR, 0, 0);
	chip->pps_adapter_type = PPS_ADAPTER_UNKNOWN;
	chip->pps_dummy_started = false;
	chip->pps_fastchg_started = false;
	chip->pps_fastchg_batt_temp_status = PPS_BAT_TEMP_NATURAL;
	chip->pps_temp_cur_range = PPS_TEMP_RANGE_INIT;
	chip->cp_master_ibus = 0;
	chip->cp_slave_ibus = 0;
	chip->master_enable_err_num = 0;
	chip->slave_enable_err_num = 0;
	chip->pps_imax = 0;
	chip->pps_vmax = 0;
}

int oplus_pps_register_ops(struct oplus_pps_operations *ops)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return -EINVAL;
	}

	chip->ops = ops;
	return 0;
}

struct oplus_pps_chip *oplus_pps_get_pps_chip(void)
{
	return &g_pps_chip;
}

int oplus_pps_init(struct oplus_chg_chip *g_chg_chip)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	struct power_supply *batt_psy;
	int ret = 0;

	chip->dev = g_chg_chip->dev;

	oplus_pps_parse_dt(chip);

	batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		dev_err(chip->dev, "battery psy not found\n");
		chip->pps_batt_psy = batt_psy;
	}

	chip->ops = oplus_pps_ops_get();

	INIT_DELAYED_WORK(&chip->pps_stop_work, oplus_pps_stop_work);
	INIT_DELAYED_WORK(&chip->pps_ffc_work, oplus_pps_ffc_work);
	INIT_DELAYED_WORK(&chip->update_pps_work, oplus_pps_update_work);
	INIT_DELAYED_WORK(&chip->check_vbat_diff_work, oplus_pps_vbat_diff_work);
	INIT_LIST_HEAD(&chip->temp_list);
	oplus_pps_variables_reset(true);
	oplus_pps_clear_dbg_info();
	return 0;
}

bool oplus_pps_check_adapter_ability(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type || !chip->ops || !chip->ops->get_pps_max_cur) {
		pps_err(", oplus_pps_chip null!\n");
		return false;
	}

	if (oplus_pps_get_power() == OPLUS_PPS_POWER_V1 || oplus_pps_get_power() == OPLUS_PPS_POWER_V2) {
		pps_err(", pps_power = %d \n", chip->pps_power);
		return true;
	}


	return false;
}

bool oplus_pps_is_allow_real(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return false;
	}

	chip->ap_batt_temperature = oplus_chg_match_temp_for_chging();
	chip->ap_batt_soc = oplus_chg_get_ui_soc();
	chip->ap_batt_volt = oplus_gauge_get_batt_mvolts();
		pps_err("chip->fg[%d %d %d] temp[%d %d %d %d] warm[%d %d %d]\n",
		chip->ap_batt_temperature, chip->ap_batt_soc, chip->ap_batt_volt,
		chip->limits.pps_batt_over_high_temp, chip->limits.pps_batt_over_low_temp, chip->limits.pps_strategy_soc_high, chip->limits.pps_strategy_soc_over_low,
		chip->limits.pps_normal_high_temp, chip->limits.pps_warm_allow_soc, chip->limits.pps_warm_allow_vol);
	if (!oplus_chg_get_mmi_value()) {
		return false;
	}
	if (chip->ap_batt_temperature >= chip->limits.pps_batt_over_high_temp) {
		return false;
	}

	if (chip->ap_batt_temperature < chip->limits.pps_batt_over_low_temp) {
		return false;
	}
/*
	if (chip->ops->check_btb_temp() < 0) {
		return false;
	}
*/
	if (chip->ap_batt_soc > chip->limits.pps_strategy_soc_high) {
		return false;
	}
	if (chip->ap_batt_soc < chip->limits.pps_strategy_soc_over_low) {
		return false;
	}

	if (chip->limits.pps_normal_high_temp != -EINVAL
		&& (chip->ap_batt_temperature > chip->limits.pps_normal_high_temp)
		&& (!(chip->ap_batt_soc < chip->limits.pps_warm_allow_soc && (chip->ap_batt_volt < chip->limits.pps_warm_allow_vol)))) {
		return false;
	}
/*
	if (chip->fastchg_to_warm_full == true
		&& batt_temp > chip->vooc_normal_high_temp - VOOC_WARM_TEMP_RANGE_THD) {
		pps_err(" oplus_vooc_get_fastchg_to_warm_full is true\n");
		return false;
	}
*/
	return true;
}

bool oplus_is_pps_charging(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return false;
	}
	return (oplus_pps_get_pps_dummy_started() || oplus_pps_get_pps_fastchg_started());
}

void oplus_pps_set_power(int pps_ability, int imax, int vmax)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return;
	}

	chip->pps_power = pps_ability;
	chip->pps_imax = imax;
	chip->pps_vmax = vmax;
	pps_err(" %d, %d, %d, \n", chip->pps_power, chip->pps_imax, chip->pps_vmax);
}

int oplus_pps_get_power(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type) {
		return 0;
	}
	return chip->pps_power;
}

void oplus_pps_print_log(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;

	if (!chip || !chip->pps_support_type) {
		return;
	}

	pps_err("PPS[ %d / %d / %d / %d / %d / %d / %d / %d]\n",
		chip->pps_support_type, chip->pps_status, chip->pps_chging, chip->pps_power,
		chip->pps_adapter_type, chip->pps_dummy_started, chip->pps_fastchg_started, chip->pps_stop_status);
}

int oplus_chg_pps_cp_mode_init(int mode)
{
	int status = 0;

	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops) {
		pps_err(", g_pps_chip null!\n");
		return -EINVAL;
	}

	if (!chip->ops->pps_cp_mode_init) {
		pps_err(", pps_cp_mode_init null!\n");
		return -EINVAL;
	}

	if (mode == PPS_SC_MODE) {
		status = chip->ops->pps_cp_mode_init(PPS_SC_MODE);
	} else if (mode == PPS_BYPASS_MODE) {
		status = chip->ops->pps_cp_mode_init(PPS_BYPASS_MODE);
	}
	return 0;
}

void oplus_pps_set_bcc_current(int val)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	int target_current_temp = 0;

	if (!chip || !chip->ops || !chip->pps_support_type) {
		pr_err("%s, g_pps_chip null!\n", __func__);
		return;
	}
	chip->current_bcc = val / 2;
}


int oplus_pps_get_bcc_max_curr(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops) {
		pps_err(", g_pps_chip null!\n");
		return 0;
	} else {
		return chip->bcc_max_curr;
	}
}

int oplus_pps_get_bcc_min_curr(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops) {
		pps_err(", g_pps_chip null!\n");
		return 0;
	} else {
		return chip->bcc_min_curr;
	}
}

int oplus_pps_get_bcc_exit_curr(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops) {
		pps_err(", g_pps_chip null!\n");
		return 0;
	} else {
		/*chip->bcc_exit_curr = oplus_chg_bcc_get_stop_curr(g_vooc_chip);*/
		pps_err("bcc exit curr = %d\n", chip->bcc_exit_curr);
		return chip->bcc_exit_curr;
	}
}

bool oplus_pps_bcc_get_temp_range(void)
{
	struct oplus_pps_chip *chip = &g_pps_chip;
	if (!chip || !chip->pps_support_type || !chip->ops) {
		pps_err(", g_pps_chip null!\n");
		return 0;
	} else {
		if (chip->pps_temp_cur_range == PPS_TEMP_RANGE_NORMAL_LOW ||
			chip->pps_temp_cur_range == PPS_TEMP_RANGE_NORMAL_HIGH) {
			return true;
		} else {
			return false;
		}
	}
}

