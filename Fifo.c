#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/mutex.h>

#define BUFF_SIZE 254
MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

struct mutex  mut;

int fifo[16];
int pos = 0;
int pos_cit = 0;
int broj_citanja = 1;
int help = 0;

wait_queue_head_t readQ;
DECLARE_WAIT_QUEUE_HEAD (readQ);

wait_queue_head_t writeQ;
DECLARE_WAIT_QUEUE_HEAD(writeQ);

int endRead = 0;
int fifo_open(struct inode *pinode, struct file *pfile);
int fifo_close(struct inode *pinode, struct file *pfile);
ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
int bintochar(char *buff);

struct file_operations my_fops =
{
        .owner = THIS_MODULE,
        .open = fifo_open,
        .read = fifo_read,
        .write = fifo_write,
        .release = fifo_close,
};


int fifo_open(struct inode *pinode, struct file *pfile)
{
                printk(KERN_INFO "Succesfully opened fifo\n");
                return 0;
}

int fifo_close(struct inode *pinode, struct file *pfile)
{
                printk(KERN_INFO "Succesfully closed fifo\n");
                return 0;
}

ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset)
{
        int ret;
        char buff[BUFF_SIZE];
        long int len = 0;
        int temp;

        temp = 0;

        if (endRead){
                endRead = 0;
                return 0;
        }

        if(mutex_lock_interruptible(&mut))
          return -ERESTARTSYS;
        while (help == 0)
          {
            mutex_unlock(&mut);
            if(wait_event_interruptible(readQ,(help > 0)))
              return -ERESTARTSYS;
            if(mutex_lock_interruptible(&mut))
              return -ERESTARTSYS;
          }

        while(temp < broj_citanja)
        {
                if(pos_cit > -1)
                {

                        len = scnprintf(buff, BUFF_SIZE, "Procitao %d na poziciji %d ,broj citanja %d ", fifo[pos_cit],pos_cit,temp);
                        printk(KERN_INFO "vrednost %d na poziciji %d\n", fifo[pos_cit], pos_cit);
                        ret = copy_to_user(buffer, buff, len);
                        help--;

                        if(ret)
                                return -EFAULT;

                        printk(KERN_INFO "Succesfully read\n");
                        endRead = 1;
                }
                else
                {
                        printk(KERN_WARNING "Lifo is empty\n");
                }

                if(pos_cit == 16)
                {
                        pos_cit = 0;
                        pos = 0;
                        help = 0;
                }

                temp++;
                pos_cit++;
        }

        mutex_unlock(&mut);
        wake_up_interruptible(&writeQ);

        return len;
}

int bintochar(char *buff)
{
        int sum = 0;
        int pom = 1;
        int i = 0;

        for(i = 0; i<9 ; i++)
        {
                if(buff[i]==49)
                {
                        sum = sum + pom;
                }
                pom = pom * 2;
        }
        return sum;
}

ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset)
{
        char buff[BUFF_SIZE];
        char value[BUFF_SIZE];
        char temp[9];
        int ret;
        int dig;
        int broj;
        int flag = 1;
        int petlja_poc = 2 ;
        int petlja_kraj = 10;

        ret = copy_from_user(buff, buffer, length);
        if(ret)
                return -EFAULT;
        buff[length-1] = '\0';

         while(pos == 16)
         {
                mutex_unlock(&mut);
                if(wait_event_interruptible(writeQ,(pos < 16)))
                         return -ERESTARTSYS;
                if(mutex_lock_interruptible(&mut))
                        return -ERESTARTSYS;
        }

        if(pos<16)
        {
                ret = sscanf(buff,"%s",value);

                if(value[0] == '0')
                {
                        while(flag)
                        {
                                int pom = petlja_poc;
                                int leva = 0;
                                for (petlja_poc;petlja_poc < petlja_kraj; petlja_poc++)
                                {
                                        temp[leva] = value[petlja_poc];
                                        leva++;
                                }

                                leva = 0;
                                dig = bintochar(temp);

                                if(ret==1)//one parameter parsed in sscanf
                                {
                                        printk(KERN_INFO "Succesfully wrote value %d on position %d", dig,pos);
                                        fifo[pos] = dig;
                                        pos=pos+1;
                                }
                                else
                                {
                                        printk(KERN_WARNING "Wrong command format\n");
                                }

                                petlja_poc = pom + 11;
                                petlja_kraj = petlja_kraj + 11;

                                if(petlja_kraj > length)
                                {
                                        flag = 0;
                                        petlja_poc = 2;
                                        petlja_kraj = 10;
                                }
                        }
                }
                else if(value[0] == 'n')
                {
                        broj = strchr(buff, 'n');

                        if(broj != NULL)
                        {
                                broj += 4;
                                ret = sscanf(broj,"%d",&broj_citanja);
                                printk(KERN_INFO "successfully written value n=%d", broj_citanja);
                        }
                        printk(KERN_INFO "Broj pozicija kojih treba da se ocita je %d \n", broj_citanja);
                }
                else
                {
                        printk(KERN_WARNING "WRONG COMMAND FORMAT \n");
                }
        }
        else
        {
                printk(KERN_WARNING "Lifo is full\n");
        }
        help++;

        wake_up_interruptible(&readQ);
        mutex_unlock(&mut);
        return length;
}

static int __init fifo_init(void)
{
   int ret = 0;
        int i=0;

        //Initialize array
        for (i=0; i<10; i++)
                fifo[i] = 0;

        init_waitqueue_head(&writeQ);
        init_waitqueue_head(&readQ);
        mutex_init(&mut);

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fifo");
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "fifo_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");

   my_device = device_create(my_class, NULL, my_dev_id, NULL, "fifo");
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

        my_cdev = cdev_alloc();
        my_cdev->ops = &my_fops;
        my_cdev->owner = THIS_MODULE;
        ret = cdev_add(my_cdev, my_dev_id, 1);
        if (ret)
        {
      printk(KERN_ERR "failed to add cdev\n");
                goto fail_2;
        }
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   return 0;

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit fifo_exit(void)
{
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(fifo_init);
module_exit(fifo_exit);

