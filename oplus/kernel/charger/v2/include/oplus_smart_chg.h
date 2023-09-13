#ifndef __OPLUS_SMART_CHG_H__
#define __OPLUS_SMART_CHG_H__

#if IS_ENABLED(CONFIG_OPLUS_SMART_CHARGE)
int oplus_smart_chg_set_normal_current(int curr);
int oplus_smart_chg_set_normal_cool_down(int cool_down);
long oplus_smart_chg_get_quick_mode_time_gain(void);
int oplus_smart_chg_get_quick_mode_percent_gain(void);
int oplus_smart_chg_get_soh_support(void);
#else /* CONFIG_OPLUS_SMART_CHARGE */
static inline int oplus_smart_chg_set_normal_current(int curr)
{
	return -ENOTSUPP;
}

static inline int oplus_smart_chg_set_normal_cool_down(int cool_down)
{
	return -ENOTSUPP;
}

static inline long oplus_smart_chg_get_quick_mode_time_gain(void)
{
	return -ENOTSUPP;
}

static inline int oplus_smart_chg_get_quick_mode_percent_gain(void)
{
	return -ENOTSUPP;
}

static inline int oplus_smart_chg_get_soh_support(void)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_OPLUS_SMART_CHARGE */

#endif /* __OPLUS_SMART_CHG_H__ */
