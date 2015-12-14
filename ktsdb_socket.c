#include <linux/module.h>
#include <linux/init.h>
#include <linux/in.h>
#include <net/sock.h>

#include <linux/fs.h>

#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/gpio.h>

//--sleep---
#include<linux/sched.h>
//----------

//---workqueue-------
#include <linux/workqueue.h>
struct workqueue_struct *wq = NULL;
struct work_struct sock_work;
//-------------------

#define DEVICE_READ "ktsdb_read"
#define DEVICE_WRITE "ktsdb_write"

static const unsigned short server_port = 5555;
static struct socket *serversocket=NULL;
static DECLARE_COMPLETION(threadcomplete);
static DECLARE_COMPLETION(IOcomplete);

static int ktsdb_socket_server_pid;

//static unsigned char *buffers;
static unsigned char *read_buffers;
static unsigned char *write_buffers;
static unsigned char *timestamp;

struct param_struct
{
	struct socket *client;
	struct work_struct sock_work;
};
//---------------------------------------mmap-------------------------------------------------------------

static int my_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int read_map(struct file *filp, struct vm_area_struct *vma)
{    
	unsigned long page;
	unsigned long start = (unsigned long)vma->vm_start;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	page = virt_to_phys(read_buffers);
 
	if(remap_pfn_range(vma,start,page>>PAGE_SHIFT,size,PAGE_SHARED))
		return -1;

	return 0;
}

static int write_map(struct file *filp, struct vm_area_struct *vma)
{    
	unsigned long page;
	unsigned long start = (unsigned long)vma->vm_start;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	page = virt_to_phys(write_buffers);
 
	if(remap_pfn_range(vma,start,page>>PAGE_SHIFT,size,PAGE_SHARED))
		return -1;

	return 0;
}

static struct file_operations dev_read_fops =
{
	.owner = THIS_MODULE,
	.open = my_open,
	.mmap = read_map,
};

static struct file_operations dev_write_fops =
{
	.owner = THIS_MODULE,
	.open = my_open,
	.mmap = write_map,
};

static struct miscdevice misc_read = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_READ,
	.fops = &dev_read_fops,
};

static struct miscdevice misc_write = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_WRITE,
	.fops = &dev_write_fops,
};

static int dev_init()
{

	int ret1,ret2;

	read_buffers = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	write_buffers = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);

	timestamp = (unsigned char*)kmalloc(sizeof(char)*15,GFP_KERNEL);

	SetPageReserved(virt_to_page(read_buffers));
	SetPageReserved(virt_to_page(write_buffers));
	ret1 = misc_register(&misc_read);
	ret2 = misc_register(&misc_write);

	return 0;
}

static void dev_free()
{
	misc_deregister(&misc_read);
	misc_deregister(&misc_write);
	ClearPageReserved(virt_to_page(read_buffers));
	ClearPageReserved(virt_to_page(write_buffers));
	kfree(read_buffers);
	kfree(write_buffers);

	kfree(timestamp);
}


//---------------------------------------end of mmap------------------------------------------------------
static int create_socket(void)
{
	struct sockaddr_in server;
	int servererror;

	if(sock_create_kern(PF_INET,SOCK_STREAM,IPPROTO_TCP,&serversocket)<0)
	{
		printk(KERN_ERR "server: Error creating serversocket.\n");
		return -EIO;
	}
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons((unsigned short)server_port);

	servererror = kernel_bind(serversocket,
			(struct sockaddr *) &server, sizeof(server));
	if(servererror)
		goto release;
	servererror = kernel_listen(serversocket, 3);
	if(servererror)
		goto release;
	return 0;

release:
	sock_release(serversocket);
	printk(KERN_ERR "server: Error serversocket\n");
	return -EIO;
}

static struct socket *socket_accept(struct socket *server)
{
	struct sockaddr address;
	int error, len;
	struct socket *clientsocket=NULL;
	if(server==NULL) return NULL;
	error = kernel_accept(server, &clientsocket, 0);
	if(error<0)
	{
		return NULL;
	}
	error = kernel_getpeername(clientsocket, 
		(struct sockaddr *)&address, &len); 

	if(error<0)
	{
		sock_release(clientsocket);
		return NULL;
	}

	printk(KERN_INFO "new connection (%d) from %u.%u.%u.%u\n", error,
		(unsigned char)address.sa_data[2],
		(unsigned char)address.sa_data[3],
		(unsigned char)address.sa_data[4],
		(unsigned char)address.sa_data[5]);
	return clientsocket;
}

static int server_send(struct socket *sock, unsigned char *buf, int len)
{
	struct msghdr msg;
	struct kvec iov;

	if(sock->sk==NULL) 
		return 0;
	iov.iov_base = buf;
	iov.iov_len  = len;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	len = kernel_sendmsg(sock, &msg, &iov, 1, len);
	
	return len;
}

static int server_receive(struct socket *sptr, unsigned char *buf, int len)
{
	if(sptr->sk==NULL)
		return 0;

	struct kvec iov = {buf, len};
	struct msghdr msg = {.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL};


//	len = kernel_recvmsg(sptr, &msg, &iov, 1, len, 0);
	len = kernel_recvmsg(sptr, &msg, &iov, 1, len, msg.msg_flags);

	return len;
}


static void socket_work(struct work_struct *sock_work)
{
	struct param_struct *ps = container_of(sock_work, struct param_struct, sock_work);

	unsigned char buffer[100];
	static int len = 0;
	int timestamp_len = 0;

	while (1)
	{
		len = server_receive(ps->client, buffer, sizeof(buffer));
		if (len > 0)
		{
			if (strstr(buffer, "\r\n") && len > 2)
			{
				len-=2;
				buffer[len] = '\0';
			}

			//add timestamp
			sprintf(timestamp,"%ld",jiffies);
			strcpy(read_buffers,timestamp);
			strcat(read_buffers," ");
			timestamp_len = strlen(timestamp);
			len+=timestamp_len;
			len++;
			strcat(read_buffers,buffer);
		
			schedule_timeout_uninterruptible(200);

			server_send(ps->client, write_buffers, strlen(write_buffers));
			//send \r\n for test
			server_send(ps->client, "\r\n", 2);

		}
		else if (len == 0)
		{
			struct linger so_linger;
			so_linger.l_onoff = 1;
			so_linger.l_linger = 0;

			kernel_setsockopt(ps->client, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));

			sock_release(ps->client);
			ps->client = NULL;

			break;
		}
	}
	return;
}

static int ktsdb_socket_server(void *data)
{
	daemonize("ktsdb_socket");
	allow_signal(SIGTERM);

	struct param_struct ps[10];
	int i = 0;

	while(!signal_pending(current))
	{
		unsigned char buffer[100];
		struct socket *clientsocket;
		static int len;

		clientsocket = socket_accept(serversocket);
		printk("clientsocket(%p)\n", clientsocket);


		while(clientsocket)
		{
			ps[i].client = clientsocket;
			INIT_WORK(&(ps[i].sock_work),socket_work);
			queue_work(wq,&(ps[i].sock_work));
			clientsocket = NULL;
			i++;
		}
	}

	complete(&threadcomplete);
	return 0;
}

static int __init server_init(void)
{
	dev_init();
printk("jiff start:%ld\n",jiffies);
	wq = create_workqueue("workqueue");

	if(create_socket()<0)
		return -EIO;

	ktsdb_socket_server_pid=kernel_thread(ktsdb_socket_server,NULL, CLONE_KERNEL);
	printk("ktsdb_socket_server_pid: %d\n", ktsdb_socket_server_pid);
	if(ktsdb_socket_server_pid < 0)
	{
		printk(KERN_ERR "server: Error creating ktsdb_socket_server\n");
		sock_release(serversocket);
		return -EIO;
	}
	return 0;
}

static void __exit server_exit(void)
{
	printk("server_exit() %d\n",ktsdb_socket_server_pid);
	if(ktsdb_socket_server_pid)
	{
		kill_pid(find_pid_ns(ktsdb_socket_server_pid, &init_pid_ns),
			SIGTERM, 1);
		wait_for_completion(&threadcomplete);
	}
	if(serversocket)
	{
		sock_release(serversocket);
	}

	destroy_workqueue(wq);
printk("jiff end:%ld\n",jiffies);
	dev_free();
}

module_init(server_init);
module_exit(server_exit);
MODULE_LICENSE("GPL");