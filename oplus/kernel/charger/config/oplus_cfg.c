#define pr_fmt(fmt) "[CFG]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/errno.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <crypto/public_key.h>
#include <linux/crypto.h>

#include "oplus_cfg.h"

static DEFINE_MUTEX(cfg_list_lock);
static LIST_HEAD(cfg_list);
static struct class oplus_cfg_class;

static char public_auth_code[] = {
	0x30, 0x82, 0x02, 0x0A, 0x02, 0x82, 0x02, 0x01, 0x00, 0xE3, 0xBF, 0x47,
	0xB0, 0x0D, 0xEA, 0xC0, 0x45, 0xC0, 0xBF, 0x24, 0x31, 0x9E, 0xEC, 0x64,
	0xF8, 0x47, 0xF8, 0x58, 0x96, 0xCF, 0x5E, 0xDE, 0xC6, 0xF1, 0x90, 0x26,
	0xC9, 0x42, 0xF6, 0x21, 0x40, 0x70, 0x3D, 0x58, 0x26, 0x41, 0x6B, 0x7F,
	0x1E, 0x32, 0x46, 0x82, 0x1C, 0xCD, 0x77, 0x4B, 0xF1, 0xFB, 0xD7, 0x8F,
	0x70, 0x5D, 0x99, 0x1E, 0x0D, 0xD2, 0xB1, 0x9A, 0xEF, 0x9C, 0x2A, 0xC3,
	0x08, 0x0B, 0x79, 0x38, 0x49, 0x45, 0xAA, 0x92, 0xF1, 0x13, 0x08, 0x0F,
	0x6E, 0xBF, 0xCE, 0x26, 0x7F, 0x56, 0x75, 0x4F, 0x34, 0x11, 0x80, 0x39,
	0xA7, 0x2A, 0xCC, 0xA5, 0xF0, 0x35, 0x3A, 0x9C, 0x0F, 0x0B, 0xB2, 0xB5,
	0x2C, 0xC4, 0xE6, 0xC9, 0x1C, 0x79, 0x85, 0xFB, 0x33, 0x49, 0x9B, 0x6B,
	0x25, 0xC1, 0xD9, 0xE8, 0xA4, 0x21, 0xBF, 0xF0, 0x2D, 0xC2, 0x17, 0xD1,
	0xE1, 0xDC, 0x89, 0x2C, 0x22, 0x31, 0x32, 0x36, 0x26, 0xCD, 0x8A, 0xDD,
	0x08, 0x66, 0x82, 0x1B, 0xFB, 0x0D, 0x5C, 0x52, 0xEC, 0x6C, 0xE1, 0x14,
	0xC1, 0x66, 0xB9, 0x18, 0x3C, 0x49, 0x5D, 0xA6, 0xEE, 0x67, 0x2C, 0x30,
	0xB1, 0x64, 0xC1, 0x0B, 0x72, 0x0C, 0x2B, 0x8A, 0xBD, 0x59, 0x7B, 0xA5,
	0xE2, 0x3E, 0xB8, 0x89, 0x17, 0x00, 0x14, 0x40, 0x0D, 0x69, 0x79, 0x5A,
	0xF2, 0x44, 0x20, 0x56, 0xC8, 0x38, 0x06, 0xFA, 0xAB, 0x12, 0x2F, 0xEE,
	0x24, 0x83, 0xDA, 0x29, 0x45, 0x19, 0x91, 0x4A, 0x7A, 0x32, 0x22, 0x84,
	0x51, 0x67, 0x2F, 0x4A, 0xC6, 0x14, 0x7C, 0x42, 0x3E, 0x32, 0x16, 0x92,
	0x25, 0x4A, 0x1B, 0x1E, 0x2B, 0x0C, 0x74, 0x39, 0x96, 0x06, 0x9B, 0xB0,
	0x16, 0xFC, 0xCF, 0x40, 0xB4, 0x08, 0xE2, 0x95, 0x32, 0xF0, 0x48, 0x12,
	0x9B, 0xAA, 0x3B, 0xE6, 0xBB, 0x89, 0x0F, 0xA9, 0xAB, 0x95, 0x98, 0x7F,
	0x4D, 0x12, 0xE0, 0xC9, 0xAE, 0x5F, 0x5C, 0x72, 0x28, 0xF7, 0xBF, 0xC3,
	0xC0, 0x66, 0x35, 0xDF, 0x96, 0x54, 0x30, 0x03, 0xA5, 0xBC, 0xAF, 0xF9,
	0xD9, 0x7B, 0xE3, 0xB4, 0x4A, 0x2E, 0xF8, 0x0C, 0x6F, 0x58, 0x17, 0x84,
	0x9C, 0x30, 0x7F, 0xA4, 0x43, 0x3D, 0xA9, 0x2B, 0x55, 0xCA, 0x51, 0x77,
	0x5B, 0x32, 0xC0, 0xBA, 0x29, 0xCC, 0x2D, 0x8C, 0x6A, 0xA5, 0xD9, 0x57,
	0xCE, 0x55, 0x9E, 0x23, 0xE0, 0xC6, 0x8F, 0xF0, 0x3B, 0x85, 0xDF, 0xEC,
	0xE4, 0x27, 0x6C, 0xFB, 0x0F, 0xFF, 0xED, 0xBD, 0x83, 0x3D, 0x05, 0xF7,
	0xA2, 0xAE, 0xD4, 0xFD, 0x06, 0xA0, 0x71, 0x4C, 0x20, 0x0A, 0x64, 0xC9,
	0x40, 0x14, 0x6E, 0x51, 0xAD, 0x01, 0x28, 0x0C, 0x28, 0xC0, 0xF0, 0xE4,
	0x6A, 0xCC, 0x70, 0x66, 0x24, 0xA2, 0xEC, 0xFC, 0xA8, 0xCF, 0x40, 0xE5,
	0xE0, 0xC7, 0x8C, 0x74, 0x3B, 0x9E, 0xC0, 0x4E, 0x66, 0xDD, 0x52, 0x8B,
	0xF0, 0x53, 0x01, 0x3E, 0x60, 0x56, 0xE6, 0xA6, 0x1F, 0x71, 0x41, 0xFF,
	0x47, 0x0A, 0x5F, 0x2F, 0xA3, 0xE4, 0x93, 0xDA, 0x83, 0xB4, 0xF5, 0x98,
	0xE4, 0x56, 0xF7, 0x83, 0xBD, 0xFC, 0xA1, 0x6B, 0xDA, 0x1C, 0xF5, 0xAC,
	0x0B, 0xFA, 0x0A, 0xE6, 0x0B, 0xC1, 0x62, 0x24, 0x7B, 0xAB, 0x73, 0xB9,
	0xC1, 0xB0, 0xFF, 0xDB, 0xE2, 0x3A, 0x12, 0x61, 0x5F, 0xF7, 0xDF, 0x6E,
	0xFA, 0x56, 0x19, 0xC1, 0x9E, 0x42, 0xCC, 0x20, 0xD0, 0xFF, 0xA7, 0x54,
	0x30, 0x9F, 0xA6, 0x74, 0xEC, 0xB4, 0x3F, 0x19, 0x2B, 0xC5, 0x63, 0x8B,
	0xC8, 0x4B, 0xC7, 0x5E, 0xF4, 0x22, 0x88, 0x43, 0x7E, 0xD8, 0xCA, 0x33,
	0x85, 0x12, 0x9A, 0xEE, 0x76, 0x35, 0xF8, 0xB5, 0xF8, 0x19, 0x03, 0xA3,
	0xD4, 0x40, 0xD0, 0xAC, 0xD9, 0x40, 0x2A, 0xE4, 0xEF, 0x20, 0x3A, 0x66,
	0x09, 0x2C, 0xF9, 0x09, 0x99, 0x02, 0x03, 0x01, 0x00, 0x01
};

static char public_auth_code_params[] = {
	0x05, 0x00
};

static struct public_key oplus_pauth_code = {
	.key = public_auth_code,
	.keylen = ARRAY_SIZE(public_auth_code),
	.algo = OID_rsaEncryption,
	.params = public_auth_code_params,
	.paramlen = ARRAY_SIZE(public_auth_code_params),
	.key_is_private = false,
	.pkey_algo = "rsa",
};

static union {
	char c[4];
	unsigned long l;
} endian_test = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_OPROJECT)
extern unsigned int get_project(void);
#else
static unsigned int get_project(void)
{
	return 0;
}
#endif

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
	struct sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return ERR_PTR(-ENOMEM);
	sdesc->shash.tfm = alg;
	return sdesc;
}

static int calc_hash(struct crypto_shash *alg, const unsigned char *data,
		     unsigned int datalen, unsigned char *digest)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = init_sdesc(alg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc sdesc\n");
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
	kfree(sdesc);
	return ret;
}

static u32
oplus_cfg_get_param_data_size(struct oplus_param_head *param_head)
{
	struct oplus_cfg_data_head *data_head;
	u32 index;

	for (index = 0; index < param_head->size;) {
		data_head =
			(struct oplus_cfg_data_head *)(param_head->data +
							   index);
		if (data_head->magic != OPLUS_CFG_MAGIC) {
			pr_err("data magic error\n");
			return 0;
		}
		index += data_head->size +
			 sizeof(struct oplus_cfg_data_head);
	}

	return index;
}

static int oplus_verify_signature(u8 *s, u32 s_size, u8 *digest,
				      u8 digest_size)
{
	struct public_key_signature sig;

	sig.s = s;
	sig.s_size = s_size;
	sig.digest = digest;
	sig.digest_size = digest_size;
	sig.pkey_algo = "rsa";
	sig.hash_algo = "sha256";
	sig.encoding = "pkcs1";
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	sig.data_size = 0;
	sig.data = NULL;
#endif

	return public_key_verify_signature(&oplus_pauth_code, &sig);
}

static int oplus_check_cfg_data(u8 *buf, int buf_len)
{
	struct oplus_cfg_head *cfg_head;
	struct oplus_param_head *param_head;
	struct oplus_cfg_check_info *check_info;
	struct oplus_cfg_param_info *param_info;
	struct crypto_shash *alg;
	char *hash_alg_name = "sha256";
	unsigned char digest[32];
	int i;
	int rc, tmp;

	if (ENDIANNESS == 'b') {
		pr_err("Big-endian mode is temporarily not supported\n");
		return -EINVAL;
	}

	if (buf == NULL) {
		pr_err("data buf is null\n");
		return -EINVAL;
	}
	if (buf_len < sizeof(struct oplus_cfg_head)) {
		pr_err("buf_len(=%d) error\n", buf_len);
		return -EINVAL;
	}

	cfg_head = (struct oplus_cfg_head *)buf;
	tmp = cfg_head->size + sizeof(struct oplus_cfg_head);
	if (buf_len != tmp) {
		pr_err("buf_len(=%d) error, expected to be %d\n", buf_len,
			tmp);
		return -EINVAL;
	}

	if (cfg_head->magic != OPLUS_CFG_MAGIC ||
	    cfg_head->head_size != sizeof(struct oplus_cfg_head)) {
		pr_err("cfg head error, magic=0x%08x, size=%d\n",
			cfg_head->magic, cfg_head->head_size);
		return -EINVAL;
	}

	alg = crypto_alloc_shash(hash_alg_name, 0, 0);
	if (IS_ERR(alg)) {
		pr_err("can't alloc alg %s\n", hash_alg_name);
		return PTR_ERR(alg);
	}
	rc = calc_hash(alg, (unsigned char *)buf + cfg_head->head_size,
		       cfg_head->size, digest);
	if (rc < 0) {
		pr_err("Configuration file digest calculation failed, rc=%d\n",
			rc);
		return rc;
	}
	crypto_free_shash(alg);

	rc = oplus_verify_signature(cfg_head->signature, 512, digest, 32);
	if (rc < 0) {
		pr_err("Configuration file signature verification failed, rc=%d\n",
			rc);
		return rc;
	}

	check_info = (struct oplus_cfg_check_info *)cfg_head->buf;
	pr_info("user=%u, project=%d\n", check_info->uid, check_info->pid);
	if (get_project() != check_info->pid) {
		pr_err("project code does not match, id=%d\n", get_project());
		return -EINVAL;
	}

	param_info = (struct oplus_cfg_param_info
			      *)(cfg_head->buf +
				 sizeof(struct oplus_cfg_check_info));
	if (param_info->magic != OPLUS_CFG_MAGIC) {
		pr_err("param_info magic error\n");
		return -EINVAL;
	}
	if (param_info->param_num > OPLUS_CFG_PARAM_MAX) {
		pr_err("param_num too big, param_num=%u, max=%d\n",
			param_info->param_num, OPLUS_CFG_PARAM_MAX);
		return -EINVAL;
	}
	for (i = 0; i < param_info->param_num; i++) {
		param_head = (struct oplus_param_head
				      *)((unsigned char *)buf +
					 param_info->param_index[i]);
		if (param_head->size !=
		    oplus_cfg_get_param_data_size(param_head)) {
			pr_err("parameter(=%d) length error, len=%d\n", i,
				oplus_cfg_get_param_data_size(param_head));
			return -EINVAL;
		}
	}

	return 0;
}

struct oplus_cfg_data_head *
oplus_cfg_find_param_by_name(struct oplus_param_head *param_head,
				const char *name)
{
	struct oplus_cfg_data_head *data_head;
	int name_len;
	u32 index;

	if (param_head == NULL) {
		pr_err("param_head is NULL\n");
		return NULL;
	}
	if (name == NULL) {
		pr_err("name is NULL\n");
		return NULL;
	}
	name_len = strlen(name);

	for (index = 0; index < param_head->size;) {
		data_head =
			(struct oplus_cfg_data_head *)(param_head->data +
							   index);
		if (data_head->magic != OPLUS_CFG_MAGIC) {
			pr_err("data magic error\n");
			return NULL;
		}
		if (data_head->data_index >= data_head->size) {
			pr_err("data index error\n");
			return NULL;
		}
		if (name_len >= data_head->size) {
			index += data_head->size +
				 sizeof(struct oplus_cfg_data_head);
			continue;
		}
		if (strncmp((char *)data_head->data, name, name_len) == 0) {
			if (data_head->data_index != (name_len + 1)) {
				pr_err("(%s) data index error, data_index=%d, name_len=%d\n",
					name, data_head->data_index, name_len);
				return NULL;
			}
			return data_head;
		}

		index += data_head->size +
			 sizeof(struct oplus_cfg_data_head);
	}

	return NULL;
}
EXPORT_SYMBOL(oplus_cfg_find_param_by_name);

ssize_t oplus_cfg_get_data_size(struct oplus_cfg_data_head *data_head)
{
	if (data_head == NULL) {
		pr_err("data_head is NULL\n");
		return -EINVAL;
	}

	return (ssize_t)(data_head->size - data_head->data_index);
}
EXPORT_SYMBOL(oplus_cfg_get_data_size);

int oplus_cfg_get_data(struct oplus_cfg_data_head *data_head, u8 *buf,
			   size_t len)
{
	ssize_t data_len;

	if (data_head == NULL) {
		pr_err("data_head is NULL\n");
		return -EINVAL;
	}
	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return -EINVAL;
	}
	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len != len) {
		pr_err("buf length and data(=%s) length do not match, buf_len=%u, data_len=%d\n",
			(char *)data_head->data, len, data_len);
		return -EINVAL;
	}

	memcpy(buf, data_head->data + data_head->data_index, len);

	return 0;
}
EXPORT_SYMBOL(oplus_cfg_get_data);

int oplus_cfg_get_data_by_name(struct oplus_param_head *param_head,
				   const char *name, u8 *buf, size_t len)
{
	struct oplus_cfg_data_head *data_head;

	if (param_head == NULL) {
		pr_err("param_head is NULL\n");
		return -EINVAL;
	}
	if (name == NULL) {
		pr_err("name is NULL\n");
		return -EINVAL;
	}
	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return -EINVAL;
	}

	data_head = oplus_cfg_find_param_by_name(param_head, name);
	if (data_head == NULL)
		return -EINVAL;

	return oplus_cfg_get_data(data_head, buf, len);
}
EXPORT_SYMBOL(oplus_cfg_get_data_by_name);

static int oplus_cfg_update_config(u8 *buf, int buf_len)
{
	struct oplus_cfg_head *cfg_head;
	struct oplus_param_head *param_head;
	struct oplus_cfg_param_info *param_info;
	struct oplus_cfg *cfg;
	bool notsupport_param_type;
	int i;
	int rc, tmp;

	if (buf == NULL) {
		pr_err("buf is NULL\n");
		return -EINVAL;
	}
	if (buf_len < sizeof(struct oplus_cfg_head)) {
		pr_err("buf_len(=%d) error\n", buf_len);
		return -EINVAL;
	}

	cfg_head = (struct oplus_cfg_head *)buf;
	tmp = cfg_head->size + sizeof(struct oplus_cfg_head);
	if (buf_len != tmp) {
		pr_err("buf_len(=%d) error, expected to be %d\n", buf_len,
			tmp);
		return -EINVAL;
	}

	param_info = (struct oplus_cfg_param_info
			      *)(cfg_head->buf +
				 sizeof(struct oplus_cfg_check_info));
	for (i = 0; i < param_info->param_num; i++) {
		param_head = (struct oplus_param_head
				      *)((unsigned char *)buf +
					 param_info->param_index[i]);
		notsupport_param_type = true;
		mutex_lock(&cfg_list_lock);
		list_for_each_entry (cfg, &cfg_list, list) {
			if (cfg->type != param_head->type)
				continue;
			if (cfg->update) {
				rc = cfg->update(cfg->priv_data, param_head);
				if (rc != 0) {
					pr_err("param(=%d) update error, rc=%d\n",
						param_head->type, rc);
					mutex_unlock(&cfg_list_lock);
					return rc;
				}
				notsupport_param_type = false;
				break;
			}
			pr_err("param(=%d) update func is NULL\n", param_head->type);
			mutex_unlock(&cfg_list_lock);
			return -ENOTSUPP;
		}
		mutex_unlock(&cfg_list_lock);
		if (notsupport_param_type) {
			pr_err("param(=%d) not support\n", param_head->type);
			return -ENOTSUPP;
		}
	}

	return 0;
}

int oplus_cfg_register(struct oplus_cfg *cfg)
{
	struct oplus_cfg *cfg_tmp;

	if (cfg == NULL) {
		pr_err("cfg is NULL\n");
		return -EINVAL;
	}
	if (cfg->type < 0 || cfg->type > OPLUS_CFG_PARAM_MAX) {
		pr_err("cfg type error, type=%d\n", cfg->type);
		return -EINVAL;
	}
	if (cfg->update == NULL) {
		pr_err("cfg update func is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&cfg_list_lock);
	list_for_each_entry (cfg_tmp, &cfg_list, list) {
		if (cfg->type != cfg_tmp->type)
			continue;
		pr_err("type=%d is already registered\n");
		mutex_unlock(&cfg_list_lock);
		return -EINVAL;
	}
	list_add(&cfg->list, &cfg_list);
	mutex_unlock(&cfg_list_lock);

	return 0;
}
EXPORT_SYMBOL(oplus_cfg_register);

int oplus_cfg_unregister(struct oplus_cfg *cfg)
{
	if (cfg == NULL) {
		pr_err("cfg is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&cfg_list_lock);
	list_del(&cfg->list);
	mutex_unlock(&cfg_list_lock);

	return 0;
}
EXPORT_SYMBOL(oplus_cfg_unregister);

static ssize_t update_show(struct class *c, struct class_attribute *attr,
			   char *buf)
{
	return 0;
}

#define RECEIVE_START	0
#define RECEIVE_CFG	1
#define RECEIVE_END	2
static ssize_t update_store(struct class *c, struct class_attribute *attr,
			    const char *buf, size_t count)
{
	u8 temp_buf[sizeof(struct oplus_cfg_head)];
	static u8 *cfg_buf;
	static int receive_step = RECEIVE_START;
	static int cfg_index;
	static int cfg_size;
	struct oplus_cfg_head *cfg_head;
	ssize_t rc;

start:
	switch (receive_step) {
	case RECEIVE_START:
		if (count < sizeof(struct oplus_cfg_head)) {
			pr_err("cfg data format error\n");
			return -EINVAL;
		}
		memset(temp_buf, 0, sizeof(struct oplus_cfg_head));
		memcpy(temp_buf, buf, sizeof(struct oplus_cfg_head));
		cfg_head = (struct oplus_cfg_head *)temp_buf;
		if (cfg_head->magic == OPLUS_CFG_MAGIC) {
			cfg_size = cfg_head->size +
				   sizeof(struct oplus_cfg_head);
			cfg_buf = kzalloc(cfg_size, GFP_KERNEL);
			if (cfg_buf == NULL) {
				pr_err("alloc cfg_buf err\n");
				return -ENOMEM;
			}
			pr_info(
				"cfg data header verification succeeded, cfg_size=%d\n",
				cfg_size);
			memcpy(cfg_buf, buf, count);
			cfg_index = count;
			pr_info(
				"Receiving cfg data, cfg_size=%d, cfg_index=%d\n",
				cfg_size, cfg_index);
			if (cfg_index >= cfg_size) {
				receive_step = RECEIVE_END;
				goto start;
			} else {
				receive_step = RECEIVE_CFG;
			}
		} else {
			pr_err("cfg data format error\n");
			return -EINVAL;
		}
		break;
	case RECEIVE_CFG:
		memcpy(cfg_buf + cfg_index, buf, count);
		cfg_index += count;
		pr_info("Receiving cfg data, cfg_size=%d, cfg_index=%d\n",
			 cfg_size, cfg_index);
		if (cfg_index >= cfg_size) {
			receive_step = RECEIVE_END;
			goto start;
		}
		break;
	case RECEIVE_END:
		rc = oplus_check_cfg_data(cfg_buf, cfg_size);
		if (rc < 0) {
			kfree(cfg_buf);
			cfg_buf = NULL;
			receive_step = RECEIVE_START;
			pr_err("cfg data verification failed, rc=%d\n", rc);
			return rc;
		}

		rc = oplus_cfg_update_config(cfg_buf, cfg_size);
		if (rc != 0) {
			pr_err("update error, rc=%d\n", rc);
			kfree(cfg_buf);
			cfg_buf = NULL;
			receive_step = RECEIVE_START;
			return rc;
		}
		pr_info("update success\n");
		kfree(cfg_buf);
		cfg_buf = NULL;
		receive_step = RECEIVE_START;
		break;
	default:
		receive_step = RECEIVE_START;
		pr_err("status error\n");
		if (cfg_buf != NULL) {
			kfree(cfg_buf);
			cfg_buf = NULL;
		}
		break;
	}

	return count;
}
static CLASS_ATTR_RW(update);

static struct attribute *cfg_class_attrs[] = {
	&class_attr_update.attr,
	NULL
};
ATTRIBUTE_GROUPS(cfg_class);

static __init int oplus_cfg_init(void)
{
	int rc;

	oplus_cfg_class.name = "oplus_cfg";
	oplus_cfg_class.class_groups = cfg_class_groups;
	rc = class_register(&oplus_cfg_class);
	if (rc < 0) {
		pr_err("Failed to create oplus_cfg_class rc=%d\n", rc);
		return rc;
	}

	return 0;
}
module_init(oplus_cfg_init);

static __exit void oplus_cfg_exit(void)
{
	class_unregister(&oplus_cfg_class);
}
module_exit(oplus_cfg_exit);

MODULE_DESCRIPTION("oplus dynamic config");
MODULE_LICENSE("GPL v2");
