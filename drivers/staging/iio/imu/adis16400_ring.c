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
#include <linux/bitops.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../ring_sw.h"
#include "../accel/accel.h"
#include "../trigger.h"
#include "adis16400.h"

/**
 * adis16400_spi_read_burst() - read all data registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @rx: somewhere to pass back the value read (min size is 24 bytes)
 **/
static int adis16400_spi_read_burst(struct device *dev, u8 *rx)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	u32 old_speed_hz = st->us->max_speed_hz;
	int ret;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
		}, {
			.rx_buf = rx,
			.bits_per_word = 8,
			.len = 24,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16400_READ_REG(ADIS16400_GLOB_CMD);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);

	st->us->max_speed_hz = min(ADIS16400_SPI_BURST, old_speed_hz);
	spi_setup(st->us);

	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem when burst reading");

	st->us->max_speed_hz = old_speed_hz;
	spi_setup(st->us);
	mutex_unlock(&st->buf_lock);
	return ret;
}

static const u16 read_all_tx_array[] = {
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_SUPPLY_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_XGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_YGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_ZGYRO_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_XACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_YACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_ZACCL_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_XTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_YTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16350_ZTEMP_OUT)),
	cpu_to_be16(ADIS16400_READ_REG(ADIS16400_AUX_ADC)),
};

static int adis16350_spi_read_all(struct device *dev, u8 *rx)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);

	struct spi_message msg;
	int i, j = 0, ret;
	struct spi_transfer *xfers;

	xfers = kzalloc(sizeof(*xfers)*
			st->indio_dev->ring->scan_count + 1,
		GFP_KERNEL);
	if (xfers == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(read_all_tx_array); i++)
		if (st->indio_dev->ring->scan_mask & (1 << i)) {
			xfers[j].tx_buf = &read_all_tx_array[i];
			xfers[j].bits_per_word = 16;
			xfers[j].len = 2;
			xfers[j + 1].rx_buf = rx + j*2;
			j++;
		}
	xfers[j].bits_per_word = 16;
	xfers[j].len = 2;

	spi_message_init(&msg);
	for (j = 0; j < st->indio_dev->ring->scan_count + 1; j++)
		spi_message_add_tail(&xfers[j], &msg);

	ret = spi_sync(st->us, &msg);
	kfree(xfers);

	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is to device
 * specific to be rolled into the core.
 */
static irqreturn_t adis16400_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->private_data;
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	struct iio_ring_buffer *ring = indio_dev->ring;
	int i = 0, j, ret = 0;
	s16 *data;
	size_t datasize = ring->access.get_bytes_per_datum(ring);
	unsigned long mask = ring->scan_mask;

	data = kmalloc(datasize , GFP_KERNEL);
	if (data == NULL) {
		dev_err(&st->us->dev, "memory alloc failed in ring bh");
		return -ENOMEM;
	}

	if (ring->scan_count) {
		if (st->variant->flags & ADIS16400_NO_BURST) {
			ret = adis16350_spi_read_all(&indio_dev->dev, st->rx);
			if (ret < 0)
				return ret;
			for (; i < ring->scan_count; i++)
				data[i]	= *(s16 *)(st->rx + i*2);
		} else {
			ret = adis16400_spi_read_burst(&indio_dev->dev, st->rx);
			if (ret < 0)
				return ret;
			for (; i < indio_dev->ring->scan_count; i++) {
				j = __ffs(mask);
				mask &= ~(1 << j);
				data[i] = be16_to_cpup(
					(__be16 *)&(st->rx[j*2]));
			}
		}
	}
	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		*((s64 *)(data + ((i + 3)/4)*4)) = pf->timestamp;
	ring->access.store_to(indio_dev->ring, (u8 *) data, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);
	kfree(data);

	return IRQ_HANDLED;
}

void adis16400_unconfigure_ring(struct iio_dev *indio_dev)
{
	kfree(indio_dev->pollfunc->name);
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

int adis16400_configure_ring(struct iio_dev *indio_dev)
{
	int ret = 0;
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
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
	ring->scan_mask = st->variant->default_scan_mask;
	ring->scan_count = hweight_long(st->variant->default_scan_mask);
	/* Set default scan mode */

	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_sw_rb_free;
	}
	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->pollfunc->h = &iio_pollfunc_store_time;
	indio_dev->pollfunc->thread = &adis16400_trigger_handler;
	indio_dev->pollfunc->type = IRQF_ONESHOT;
	indio_dev->pollfunc->name =
		kasprintf(GFP_KERNEL, "adis16400_consumer%d", indio_dev->id);
	if (ret)
		goto error_iio_free_pollfunc;

	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_iio_free_pollfunc:
	kfree(indio_dev->pollfunc);
error_iio_sw_rb_free:
	iio_sw_rb_free(indio_dev->ring);
	return ret;
}
