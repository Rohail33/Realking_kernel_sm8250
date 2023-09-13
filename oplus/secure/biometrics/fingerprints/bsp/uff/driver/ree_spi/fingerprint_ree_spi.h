#ifndef FINGERPRINT_REE_SPI_H
#define FINGERPRINT_REE_SPI_H
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#define FP_MAX_PWR_LIST_LEN (4)

#define SPI_IOC_POWER_ON _IO(SPI_IOC_MAGIC, 21)
#define SPI_IOC_POWER_OFF _IO(SPI_IOC_MAGIC, 22)
#define SPI_IOC_RESET _IO(SPI_IOC_MAGIC, 23)
#define SET_DEFALUT_CONFIG _IO(SPI_IOC_MAGIC, 24)


#define FP_IOC_MAGIC 'O'  // define magic number   // need modify to 'o'
#define FP_IOC_EXIT _IO(FP_IOC_MAGIC, 1)
#define FP_IOC_RESET _IO(FP_IOC_MAGIC, 2)
#define FP_IOC_ENABLE_IRQ _IO(FP_IOC_MAGIC, 3)
#define FP_IOC_DISABLE_IRQ _IO(FP_IOC_MAGIC, 4)
#define FP_IOC_ENABLE_SPI_CLK _IOW(FP_IOC_MAGIC, 5, uint32_t)
#define FP_IOC_DISABLE_SPI_CLK _IO(FP_IOC_MAGIC, 6)
#define FP_IOC_ENABLE_POWER _IO(FP_IOC_MAGIC, 7)
#define FP_IOC_DISABLE_POWER _IO(FP_IOC_MAGIC, 8)
#define FP_IOC_ENTER_SLEEP_MODE _IO(FP_IOC_MAGIC, 10)
#define FP_IOC_RD_IRQ_VALUE _IOW(FP_IOC_MAGIC, 13, int32_t)


#define FP_IOC_MAXNR \
    21 /* THIS MACRO IS NOT USED NOW..

/* Read / Write of SPI mode (SPI_MODE_0..SPI_MODE_3) */
#define FP_SPI_IOC_RD_MODE _IOR(FP_IOC_MAGIC, FP_IOC_MAXNR + 1, __u8)  // 22
#define FP_SPI_IOC_WR_MODE _IOW(FP_IOC_MAGIC, FP_IOC_MAXNR + 1, __u8)  // 22

/* Read / Write SPI bit justification */
#define FP_SPI_IOC_RD_LSB_FIRST _IOR(FP_IOC_MAGIC, FP_IOC_MAXNR + 2, __u8)  // 23
#define FP_SPI_IOC_WR_LSB_FIRST _IOW(FP_IOC_MAGIC, FP_IOC_MAXNR + 2, __u8)  // 23

/* Read / Write SPI device word length (1..N) */
#define FP_SPI_IOC_RD_BITS_PER_WORD _IOR(FP_IOC_MAGIC, FP_IOC_MAXNR + 3, __u8)  // 24
#define FP_SPI_IOC_WR_BITS_PER_WORD _IOW(FP_IOC_MAGIC, FP_IOC_MAXNR + 3, __u8)  // 24

/* Read / Write SPI device default max speed hz */
#define FP_SPI_IOC_RD_MAX_SPEED_HZ _IOR(FP_IOC_MAGIC, FP_IOC_MAXNR + 4, __u32)  // 25
#define FP_SPI_IOC_WR_MAX_SPEED_HZ _IOW(FP_IOC_MAGIC, FP_IOC_MAXNR + 4, __u32)  // 25
#define FP_SPI_IOC_SET_DEFALUT_CONFIG _IO(FP_IOC_MAGIC, FP_IOC_MAXNR + 5)  // 25

struct fp_vreg_config {
    const char *name;
    int vmin;
    int vmax;
    int ua_load;
};
typedef enum {
    FP_POWER_MODE_NOT_SET,  //
    FP_POWER_MODE_LDO,
    FP_POWER_MODE_GPIO,
    FP_POWER_MODE_AUTO,
} fp_power_mode_t;

enum {
    FP_OK,
    FP_ERROR_GPIO,
    FP_ERROR_GENERAL,
};

struct spi_private_header {
    unsigned int type;
    unsigned int tx_len;
    unsigned int rx_len;
};

struct spi_private_data {
    struct spi_private_header header;
    // malloc_data
};

typedef struct {
    uint32_t pwr_type;
    signed pwr_gpio;
    struct fp_vreg_config vreg_config;
    struct regulator *vreg;
    unsigned delay;
    unsigned poweron_level;
} fp_power_info_t;

struct spidev_data {
    dev_t devt;
    spinlock_t spi_lock;
    struct spi_device *spi;
    struct list_head device_entry;
    struct mutex buf_lock;
    unsigned users; //user count
    u8 *buffer;
    u8 *spi_buffer;
    u8 buf_status;
    signed irq_gpio;
    signed reset_gpio;
    signed cs_gpio;
    signed pwr_gpio;
    int irq;
    int irq_enabled;
    int clk_enabled;
    struct device *dev;
    struct pinctrl *pinctrl;
    struct pinctrl_state *pstate_spi_6mA;
    struct pinctrl_state *pstate_default;
    struct pinctrl_state *pstate_cs_func;
    struct pinctrl_state *pstate_irq_no_pull;
    bool cs_gpio_set;
    unsigned power_num;
    fp_power_info_t pwr_list[FP_MAX_PWR_LIST_LEN];
    bool is_optical;
};

typedef struct fp_underscreen_info {
    uint8_t touch_state;
    uint8_t area_rate;
    uint16_t x;
    uint16_t y;
} fp_underscreen_info_t;
#endif //FINGERPRINT_REE_SPI_H
