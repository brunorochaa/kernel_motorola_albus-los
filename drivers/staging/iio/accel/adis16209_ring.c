#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "accel.h"
#include "../trigger.h"
#include "adis16209.h"

/**
 * adis16209_read_ring_data() read data registers which will be placed into ring
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read
 **/
static int adis16209_read_ring_data(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16209_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[ADIS16209_OUTPUTS + 1];
	int ret;
	int i;

	mutex_lock(&st->buf_lock);

	spi_message_init(&msg);

	memset(xfers, 0, sizeof(xfers));
	for (i = 0; i <= ADIS16209_OUTPUTS; i++) {
		xfers[i].bits_per_word = 8;
		xfers[i].cs_change = 1;
		xfers[i].len = 2;
		xfers[i].delay_usecs = 30;
		xfers[i].tx_buf = st->tx + 2 * i;
		st->tx[2 * i]
			= ADIS16209_READ_REG(ADIS16209_SUPPLY_OUT + 2 * i);
		st->tx[2 * i + 1] = 0;
		if (i >= 1)
			xfers[i].rx_buf = rx + 2 * (i - 1);
		spi_message_add_tail(&xfers[i], &msg);
	}

	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem when burst reading");

	mutex_unlock(&st->buf_lock);

	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static irqreturn_t adis16209_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->private_data;
	struct adis16209_state *st = iio_dev_get_devdata(indio_dev);
	struct iio_ring_buffer *ring = indio_dev->ring;

	int i = 0;
	s16 *data;
	size_t datasize = ring->access.get_bytes_per_datum(ring);

	data = kmalloc(datasize , GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return -ENOMEM;
	}

	if (ring->scan_count &&
	    adis16209_read_ring_data(&st->indio_dev->dev, st->rx) >= 0)
		for (; i < ring->scan_count; i++)
			data[i] = be16_to_cpup((__be16 *)&(st->rx[i*2]));

	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = pf->timestamp;

	ring->access.store_to(ring, (u8 *)data, pf->timestamp);

	iio_trigger_notify_done(st->indio_dev->trig);
	kfree(data);

	return IRQ_HANDLED;
}

void adis16209_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc->name);
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int adis16209_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct iio_ring_buffer *ring;

	ring = iio_sw_rb_allocate(indio_dev);
	if (!ring) {
		ret = -ENOMEM;
		return ret;
	}
	indio_dev->ring = ring;
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&ring->access);
	ring->bpe = 2;
	ring->scan_timestamp = true;
	ring->preenable = &iio_sw_ring_preenable;
	ring->postenable = &iio_triggered_ring_postenable;
	ring->predisable = &iio_triggered_ring_predisable;
	ring->owner = THIS_MODULE;

	/* Set default scan mode */
	iio_scan_mask_set(ring, ADIS16209_SCAN_SUPPLY);
	iio_scan_mask_set(ring, ADIS16209_SCAN_ACC_X);
	iio_scan_mask_set(ring, ADIS16209_SCAN_ACC_Y);
	iio_scan_mask_set(ring, ADIS16209_SCAN_AUX_ADC);
	iio_scan_mask_set(ring, ADIS16209_SCAN_TEMP);
	iio_scan_mask_set(ring, ADIS16209_SCAN_INCLI_X);
	iio_scan_mask_set(ring, ADIS16209_SCAN_INCLI_Y);
	iio_scan_mask_set(ring, ADIS16209_SCAN_ROT);

	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;
	}
	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->pollfunc->h = &iio_pollfunc_store_time;
	indio_dev->pollfunc->thread = &adis16209_trigger_handler;
	indio_dev->pollfunc->type = IRQF_ONESHOT;
	indio_dev->pollfunc->name =
		kasprintf(GFP_KERNEL, "adis16209_consumer%d", indio_dev->id);
	if (indio_dev->pollfunc->name == NULL) {
		ret = -ENOMEM;
		goto error_free_poll_func;
	}

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_free_poll_func:
	kfree(indio_dev->pollfunc);
error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}
