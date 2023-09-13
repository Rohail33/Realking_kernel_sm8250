/*
 * Additional link script required by oplus_chg.ko
 */

SECTIONS {
	.oplus_chg_module.data 0 : ALIGN(8) {
		__oplus_chg_module_start = .;
		*(.oplus_chg_module.core.data)
		*(.oplus_chg_module.early.data)
		*(.oplus_chg_module.normal.data)
		*(.oplus_chg_module.late.data)
		__oplus_chg_module_end = .;
	}
}
