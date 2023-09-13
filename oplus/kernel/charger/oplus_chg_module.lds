/*
 * Additional link script required by oplus_chg.ko
 */

SECTIONS {
	.oplus_chg_module.data 0 : ALIGN(8) {
		__oplus_chg_module_start = .;
		*(.oplus_chg_module.data)
		__oplus_chg_module_end = .;
	}

	.oplus_chg_wls_fw.data 0 : ALIGN(8) {
		__oplus_chg_wls_fw_start = .;
		*(.oplus_chg_wls_fw.data)
		__oplus_chg_wls_fw_end = .;
	}
}
