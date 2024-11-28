#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
/*
 *@brief define macro vals
 */
#define SAMPLE 15
#define COOLDWN 500
#define LSRPIN 516
#define LSRPINTXT "rpi-gpio-4"
#define LDRPIN 528
#define LDRPINTXT "rpi-gpio-17"

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ishan Panwar 102115220");
MODULE_DESCRIPTION("A lifi driver for receiving data");

unsigned int irq;
static char rxchar;

static DEFINE_RWLOCK(lock);
static struct kobject *lifid_kobj;

/*
 * @brief receive data through gpio
 */
static irqreturn_t get_byte(int irq, void *dev_id){
	disable_irq(irq);
	unsigned long flags;
	char data_byte = 0;
	mdelay(SAMPLE*1.5);
	for(int i = 0; i<8; i++){
		data_byte = data_byte | (char) gpio_get_value(LDRPIN) << i;
		mdelay(SAMPLE);
	}
	write_lock_irqsave(&lock, flags);
	rxchar = data_byte;
	write_unlock_irqrestore(&lock, flags);
	printk("lifid - IRQ complete. Received val: %c\n", rxchar);
	enable_irq(irq);
	return IRQ_HANDLED;
}
static void send_byte(char data){
	//start preamble
	gpio_set_value(LSRPIN, 0);
	msleep(SAMPLE);

	//data pkts
	for(int i=0; i<0; i++){
		if ( (data&(0x01<<i)) != 0 ){
			gpio_set_value(LSRPIN,1);
		}else{
			gpio_set_value(LSRPIN,0);
		}
		msleep(SAMPLE);
	}

	gpio_set_value(LSRPIN, 1);
	msleep(SAMPLE);
}

static int send_data(const char *buffer, size_t count){
	printk("lifid - send thread active\n");
	for (int i=0; i<count; i++){
		send_byte(buffer[i]);
		printk("lifid - sending byte %d\n", buffer[i]);
		mdelay(COOLDWN);
	}
	return 0;
}

/**
 * @brief Read/Write callback for lifid/rx
 */
static ssize_t lifi_network_r(struct kobject *kobj, struct kobj_attribute *attr, char *buffer) {
	char result;
	unsigned long flags;

	read_lock_irqsave(&lock, flags);
	result = rxchar;
	read_unlock_irqrestore(&lock, flags);
	return result;
}

static ssize_t lifi_network_w(struct kobject *kobj, struct kobj_attribute *attr, const char *buffer, size_t count){
	send_data(buffer, count);
	return count;
}
static struct kobj_attribute lifi_network_attr = __ATTR(network, 0444, lifi_network_r, lifi_network_w);

/**
 * @brief This function is called, when the module is loaded into the kernel
 */
static int __init driver_init(void) {
	int ret;

	printk("lifid - Creating /sys/kernel/lifid/\n");

	/* Creating the folder lifid */
	lifid_kobj = kobject_create_and_add("lifid", kernel_kobj);
	if(!lifid_kobj) {
		printk("lifid - Error creating /sys/kernel/lifid\n");
		return -ENOMEM;
	}

	/* Create the sysfs file rx*/
	if(sysfs_create_file(lifid_kobj, &lifi_network_attr.attr)) {
		printk("lifid - Error creating /sys/kernel/lifid/rx\n");
		kobject_put(lifid_kobj);
		return -ENOMEM;
	}

	/*
	 * @brief Initialize gpio section
	 */
	if(gpio_request(LDRPIN, LDRPINTXT)){
		printk("Unable to allocate GPIO %d!\n", LDRPIN);
		gpio_free(LDRPIN);
	}
	if(gpio_request(LSRPIN, LSRPINTXT)){
		printk("Unable to allocate GPIO %d!\n", LSRPIN);
		gpio_free(LSRPIN);
	}

	/*
	 * @brief Set directions
	 */
	if(gpio_direction_input(LDRPIN)){
		printk("Unable to set GPIO %d to intput!\n", LDRPIN);
		gpio_free(LDRPIN);
	}
	if(gpio_direction_output(LSRPIN, 0)){
		printk("Unable to set GPIO %d to output!\n", LSRPIN);
		gpio_free(LDRPIN);
	}


	irq = gpio_to_irq(LDRPIN);
	if (irq < 0) {
		printk(KERN_ERR "Failed to get IRQ number for GPIO %d\n", LDRPIN);
	        gpio_free(LDRPIN);
		return irq;
	}

	ret = request_irq(irq, get_byte, IRQF_TRIGGER_LOW, "device-name", NULL);
	if (ret != 0){
		printk(KERN_ERR "Failed to request IRQ for GPIO %d\n", LDRPIN);
	        gpio_free(LDRPIN);
		return ret;
	}
	printk("Device initialization complete\n");
	return 0;
}

/**
 * @brief This function is called, when the module is removed from the kernel
 */
static void __exit driver_exit(void) {
	printk("lifid - Deleting /sys/kernel/lifid/rx\n");
	sysfs_remove_file(lifid_kobj, &lifi_network_attr.attr);
	kobject_put(lifid_kobj);

	free_irq(irq, NULL);
	gpio_free(LDRPIN);
	gpio_free(LSRPIN);

	printk("lifid - Exit complete\n");
}

module_init(driver_init);
module_exit(driver_exit);
