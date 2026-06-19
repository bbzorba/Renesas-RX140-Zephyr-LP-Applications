#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Increased from 512: GPIO driver + kernel frame on RX needs headroom. */
#define STACK_SIZE    1024
#define PRIORITY      5
#define FAST_BLINK_MS 250
#define SLOW_BLINK_MS 750
#define BTN_POLL_MS   20

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec btn  = GPIO_DT_SPEC_GET(DT_ALIAS(sw1),  gpios);

/* Shared flag: toggled by ISR, read by threads. */
static volatile bool fast_mode;

/* Use a polling thread for S1 to avoid RX external IRQ routing pitfalls. */
static void button_thread(void *a, void *b, void *c)
{
	int prev_pressed = 0;

	for (;;) {
		int value = gpio_pin_get_dt(&btn);
		int pressed = (value == 0); /* active-low */

		if (pressed && !prev_pressed) {
			fast_mode = !fast_mode;
			printk("SW1 pressed - %s mode\n", fast_mode ? "fast" : "slow");
		}

		prev_pressed = pressed;
		k_msleep(BTN_POLL_MS);
	}
}

/* Thread 1: blink LED1 */
static void led1_thread(void *a, void *b, void *c)
{
	for (;;) {
		gpio_pin_toggle_dt(&led1);
		k_msleep(fast_mode ? FAST_BLINK_MS : SLOW_BLINK_MS);
	}
}

/* Thread 2: blink LED2 (opposite phase) */
static void led2_thread(void *a, void *b, void *c)
{
	for (;;) {
		gpio_pin_toggle_dt(&led2);
		k_msleep(fast_mode ? SLOW_BLINK_MS : FAST_BLINK_MS);
	}
}

K_THREAD_DEFINE(led1_tid, STACK_SIZE, led1_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(led2_tid, STACK_SIZE, led2_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(btn_tid, STACK_SIZE, button_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void)
{
	int ret;

	printk("Booting multithreaded_buttons_LEDs\n");

	/* Configure LEDs — GPIO_OUTPUT_ACTIVE turns them ON immediately so
	 * a successful configure is visible even before threads start. */
	if (!gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2)) {
		printk("LED GPIO not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	if (ret) { printk("LED1 configure failed: %d\n", ret); return ret; }
	ret = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);
	if (ret) { printk("LED2 configure failed: %d\n", ret); return ret; }

	/* Configure SW1 (P30) input. */
	if (!gpio_is_ready_dt(&btn)) {
		printk("Button GPIO not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&btn, GPIO_INPUT);
	if (ret) { printk("Button configure failed: %d\n", ret); return ret; }

	printk("Ready - press SW1 to toggle blink speed\n");
	return 0;
}

