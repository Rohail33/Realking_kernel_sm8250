/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016, 2018 The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_MSM_DMA_IOMMU_MAPPING_H
#define _LINUX_MSM_DMA_IOMMU_MAPPING_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

struct msm_iommu_data {
	struct list_head map_list;
	struct mutex lock;
};

#if IS_ENABLED(CONFIG_QCOM_LAZY_MAPPING)
/*
 * This function is not taking a reference to the dma_buf here. It is expected
 * that clients hold reference to the dma_buf until they are done with mapping
 * and unmapping.
 */
int msm_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		   enum dma_data_direction dir, struct dma_buf *dma_buf,
		   unsigned long attrs);

void msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
			    int nents, enum dma_data_direction dir,
			    struct dma_buf *dma_buf, unsigned long attrs);

void msm_dma_unmap_all_for_dev(struct device *dev);

/*
 * Below is private function only to be called by framework (ION) and not by
 * clients.
 */
void msm_dma_buf_freed(struct msm_iommu_data *data);

#else /*CONFIG_QCOM_LAZY_MAPPING*/

static inline int msm_dma_map_sg_attrs(struct device *dev,
			struct scatterlist *sg, int nents,
			enum dma_data_direction dir, struct dma_buf *dma_buf,
			unsigned long attrs)
{
	return -EINVAL;
}

static inline void
msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
		       int nents, enum dma_data_direction dir,
		       struct dma_buf *dma_buf, unsigned long attrs)
{
}

static inline int msm_dma_map_sg_lazy(struct device *dev,
			       struct scatterlist *sg, int nents,
			       enum dma_data_direction dir,
			       struct dma_buf *dma_buf)
{
	return -EINVAL;
}

static inline int msm_dma_map_sg(struct device *dev, struct scatterlist *sg,
				  int nents, enum dma_data_direction dir,
				  struct dma_buf *dma_buf)
{
	return -EINVAL;
}

static inline int msm_dma_unmap_all_for_dev(struct device *dev)
{
	return 0;
}

static inline void msm_dma_buf_freed(void *buffer) {}
#endif /*CONFIG_QCOM_LAZY_MAPPING*/

#endif
