#include <linux/module.h> /* for modules */
#include <linux/fs.h> /* file_operations */
#include <linux/uaccess.h> /* copy_(to,from)_user */
#include <linux/init.h> /* module_init, module_exit */
#include <linux/slab.h> /* kmalloc */
#include <linux/cdev.h> /* cdev utilities */
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#define MYDEV_NAME "mycdrv"
#define ramdisk_size (size_t) (16 * PAGE_SIZE) // ramdisk size 
#define CDRV_IOC_MAGIC 'Z'
#define ASP_CLEAR_BUF _IOW(CDRV_IOC_MAGIC, 1, int)


static int NUM_DEVICES = 3;
module_param(NUM_DEVICES, int, S_IRUGO); // To Change the Num_Devices.


int major = 0;
int minor = 0;

//Structs Required
struct ASP_mycdrv {
    struct cdev cdev;
    char *ramdisk;
    struct semaphore sem;
    int devNo;// any other field you may want to add};
};


struct ASP_mycdrv* devices;

static struct class *mycdrv_class;
int mycdrv_open(struct inode* inode, struct file* filp); // inode and file pointer
int mycdrv_release(struct inode* inode, struct file* filp); // inode and file pointer
ssize_t mycdrv_read(struct file* filp,char __user* buf,size_t count,loff_t *f_pos);
ssize_t mycdrv_write(struct file* filp,const char __user* buf,size_t count,loff_t *f_pos);
static int mycdrv_setup_cdev(struct ASP_mycdrv *dev, int minor, struct class *class);
static void mycdrv_cleanup_module(void);
static int __init mycdrv_init(void);
static void __exit mycdrv_exit(void);
loff_t mycdrv_llseek(struct file *filp, loff_t off, int ori);
long mycdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

struct file_operations fileops = {
    .owner = THIS_MODULE,
    .read = mycdrv_read,
    .write = mycdrv_write,
    .open = mycdrv_open,
    .release = mycdrv_release,
    .llseek = mycdrv_llseek,
    .unlocked_ioctl = mycdrv_ioctl,
};

//Open Driver
int mycdrv_open(struct inode* inode, struct file* filp){
    int majorNum = imajor(inode);
    int minorNum = iminor(inode);
    struct ASP_mycdrv *dev = NULL;
    if(majorNum!=major || minorNum<0 || minorNum>NUM_DEVICES){
        printk(KERN_WARNING"Please access the correct Device");
        return -ENODEV;
    }
    dev = &devices[minorNum];
    filp->private_data = dev;
    return 0;
}

//Release Driver
int mycdrv_release(struct inode* inode,struct file* filp){
    return 0;
}

//Read
ssize_t mycdrv_read(struct file* filp,char __user* buf,size_t count,loff_t *f_pos)
{
    struct ASP_mycdrv *dev = (struct ASP_mycdrv*)filp->private_data;
    unsigned  char *buffer = NULL;
    int k;
    ssize_t returnval = 0;
    if(down_interruptible(&(dev->sem))!=0){
        pr_info("Failed to Lock the Semaphore");
    }
    if(count<0||*f_pos<0){
        returnval = -EINVAL;
        goto exit;
    }
    if(*f_pos>ramdisk_size){
        returnval = -EINVAL;
        goto exit;
    }
    buffer = (unsigned char*) kmalloc(count,GFP_KERNEL);
    if(buffer == NULL){
        printk("Memory allocation failed");
        returnval = -ENOMEM;
        goto exit;
    }
    memset(buffer, 0, count);
    for(k=0;k<count;k++){
        buffer[k] = dev->ramdisk[*f_pos+k];
    }
    *f_pos += count;
    copy_to_user(buf,buffer,count);
    returnval = count;

exit:
    printk("Exit from the Read Function");
    if(buffer!=NULL){
        kfree(buffer);
    }
    up(&(dev->sem));
    return returnval;

}

//Write
ssize_t mycdrv_write(struct file* filp,const char __user* buf,size_t count,loff_t *f_pos){
    struct ASP_mycdrv *dev = (struct ASP_mycdrv*)filp->private_data;
    ssize_t returnval = 0;
    unsigned  char *buffer = NULL;
    int i;
    if(down_interruptible(&dev->sem)!=0){
        pr_info("Failed to Lock the Semaphore");
    }
    if(count<0 || *f_pos<0){
        returnval = -EINVAL;
        goto exit;
    }
    if(*f_pos > ramdisk_size){
        returnval = -EINVAL;
        goto exit;
    }
    buffer = (unsigned char*) kmalloc(count,GFP_KERNEL);
    if(buffer == NULL){
        printk("Memory allocation failed");
        returnval = -ENOMEM;
        goto exit;
    }
    memset(buffer, 0, count);
    copy_from_user(buffer,buf,count);
    for(i=0;i<count;i++){
        dev->ramdisk[(*f_pos+i)] = buffer[i];
    }
    *f_pos+=count;
    returnval = count;

exit:
    if(buffer!=NULL){
        kfree(buffer);
    }
    up(&(dev->sem));
    return returnval;
    
}

//llseek
loff_t mycdrv_llseek(struct file *filp, loff_t off, int ori){
    struct ASP_mycdrv *dev = (struct ASP_mycdrv *)filp->private_data;
    loff_t new_pos;
    
    if(down_interruptible(&(dev->sem))!=0){
        pr_info("Cannot be Locked");
    }
    switch(ori){
        case 0:
            new_pos = off;
            break;
        case 1:
            new_pos = filp->f_pos + off;
            break;
        case 2:
            new_pos = ramdisk_size + off;
            break;
        default:
            new_pos = -22;
        goto exit;
    }
    if(new_pos<0||new_pos>ramdisk_size-1){
        new_pos = -22;
        goto exit;
    }

    filp->f_pos = new_pos;
exit:
    up(&(dev->sem));
    return new_pos;

}

//ioctl
long mycdrv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    struct ASP_mycdrv *dev = (struct ASP_mycdrv *)filp->private_data;
    if(cmd!=ASP_CLEAR_BUF){
        return -1;
    }
    if(down_interruptible(&(dev->sem))!=0){
        pr_info("Cannot be Locked");
    }
    memset((void *)dev->ramdisk,0,ramdisk_size);
    filp->f_pos = 0;
    up(&(dev->sem));
    return 1;
}

//Driver setup
static int mycdrv_setup_cdev(struct ASP_mycdrv *dev, int minor, struct class *class){
    dev_t devno = MKDEV(major,minor);
    struct device *device = NULL;
    dev->devNo = minor;
    dev->ramdisk = NULL;
    dev->cdev.owner = THIS_MODULE;
    sema_init(&(dev->sem),1);
    cdev_init(&dev->cdev,&fileops);
    dev->ramdisk = (unsigned char*) kmalloc(ramdisk_size,GFP_KERNEL);
    cdev_add(&dev->cdev,devno,1);
    device = device_create(class,NULL,devno,NULL,MYDEV_NAME "%d",minor);
    return 0;
}

static int __init mycdrv_init(void){
    dev_t dev = 0;
    int res = 0;
    int i;
    res = alloc_chrdev_region(&dev,0,NUM_DEVICES,MYDEV_NAME);
    if(res<0){
        pr_info("Failed to allocate the device region");
        return res;
    }
    major = MAJOR(dev);
    mycdrv_class = class_create(THIS_MODULE,MYDEV_NAME);
    devices = (struct ASP_mycdrv*)kmalloc(NUM_DEVICES*sizeof(struct ASP_mycdrv),GFP_KERNEL);
    for(i=0;i<NUM_DEVICES;i++){
        res = mycdrv_setup_cdev(&devices[i],i,mycdrv_class);
        if(res){
            mycdrv_cleanup_module();
            return res;
        }
    }

    return 0;

}

static void __exit mycdrv_exit(void){
    mycdrv_cleanup_module();
    return;
}

//Cleanup
static void mycdrv_cleanup_module(void){
    int i;
    if(devices){
        for(i=0;i<NUM_DEVICES;i++){
            device_destroy(mycdrv_class,MKDEV(major,i));
            cdev_del(&devices[i].cdev);
            kfree(&devices[i].ramdisk);
        }
        kfree(devices);
        if(mycdrv_class){
            class_destroy(mycdrv_class);
        }
        unregister_chrdev_region(MKDEV(major,0),NUM_DEVICES);
    }
    return;
}

module_init(mycdrv_init);
module_exit(mycdrv_exit);

MODULE_AUTHOR("Ananda Bhaasita Desiraju");
MODULE_LICENSE("GPL v2");
