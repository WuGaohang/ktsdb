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

#define DEVICE_NAME "ktsdb"

static const unsigned short server_port = 5555;
static struct socket *serversocket=NULL;
static DECLARE_COMPLETION(threadcomplete);
static int ktsdb_socket_server_pid;

static unsigned char *buffers;
//---------------------------------------mmap------------------------------------------------------------------

static int my_open(struct inode *inode, struct file *file)
{
	return 0;
}


static int my_map(struct file *filp, struct vm_area_struct *vma)
{    
	unsigned long page;
	unsigned long start = (unsigned long)vma->vm_start;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	page = virt_to_phys(buffers);
 
	if(remap_pfn_range(vma,start,page>>PAGE_SHIFT,size,PAGE_SHARED))
		return -1;

	return 0;
}


static struct file_operations dev_fops = {
	.owner    = THIS_MODULE,
	.open    = my_open,
	.mmap   = my_map,
};

static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dev_fops,
};


static int dev_init()
{

	int ret;

	buffers = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	SetPageReserved(virt_to_page(buffers));
	ret = misc_register(&misc);

	return ret;
}

static void dev_free()
{
	misc_deregister(&misc);
	ClearPageReserved(virt_to_page(buffers));
	kfree(buffers);
}


//---------------------------------------end of mmap-----------------------------------------------------------
static int create_socket(void)
{
	struct sockaddr_in server;
	int servererror;

	if(sock_create_kern(PF_INET,SOCK_STREAM,IPPROTO_TCP,&serversocket)<0) {
		printk(KERN_ERR "server: Error creating serversocket.\n");
		return -EIO;
	}
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons((unsigned short)server_port);
printk(KERN_ERR "%p | %p\n",serversocket,server);
	servererror = kernel_bind(serversocket,
			(struct sockaddr *) &server, sizeof(server));
printk(KERN_ERR "%d\n",servererror);
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
	if(error<0) {
		return NULL;
	}
	error = kernel_getpeername(clientsocket, 
		(struct sockaddr *)&address, &len); 

	if(error<0) {
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
	struct msghdr msg;
	struct kvec iov;

	if(sptr->sk==NULL) 
	  return 0;
	iov.iov_base = buf;
	iov.iov_len  = len;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	len = kernel_recvmsg(sptr, &msg, &iov, 1, len, 0);	
	return len;
}

static int ktsdb_socket_server(void *data)
{
	struct socket *clientsocket;

	unsigned char buffer[100];

	static int len;

	daemonize("ktsdb_socket");
	allow_signal(SIGTERM);

	while(!signal_pending(current)) {
		clientsocket = socket_accept(serversocket);
		printk("clientsocket(%p)\n", clientsocket);
		while(clientsocket) {
			len = server_receive(clientsocket, buffer, sizeof(buffer));
			if(len>0) {
				if (strstr(buffer, "\r\n") && len > 2)
				{
					len-=2;
					buffer[len] = '\0';
				}

				int i = 0;

				for (i = 0; i < len; i++)
					buffers[i] = buffer[i];
				buffers[len] = '\0';
			
				//----------------------------------------------------------------------
				schedule_timeout_uninterruptible(5000);
				//----------------------------------------------------------------------

				server_send(clientsocket, buffers, strlen(buffers));
				//send \r\n for test
				server_send(clientsocket, "\r\n", 2);

				//---------------------------------------------------------------------- 
			} else {
		//-------------------------------------------
		printk("sock rele 3\n");
		//-------------------------------------------
//--------------------------------
//release socket time = 0
struct linger so_linger;
so_linger.l_onoff = 1;
so_linger.l_linger = 0;
kernel_setsockopt(clientsocket, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));
//-------------------------------------------------------------------------------------------------
				sock_release(clientsocket);
				clientsocket=NULL;
			}
		}
	}
	complete(&threadcomplete);
	return 0;
}

static int __init server_init(void)
{
//------------------------------------
	dev_init();
//------------------------------------

	if(create_socket()<0)
		return -EIO;
	ktsdb_socket_server_pid=kernel_thread(ktsdb_socket_server,NULL, CLONE_KERNEL);
	printk("ktsdb_socket_server_pid: %d\n", ktsdb_socket_server_pid);
	if(ktsdb_socket_server_pid < 0) {
		printk(KERN_ERR "server: Error creating ktsdb_socket_server\n");
		sock_release(serversocket);
		return -EIO;
	}

	return 0;
}

static void __exit server_exit(void)
{
	printk("server_exit() %d\n",ktsdb_socket_server_pid);
	if(ktsdb_socket_server_pid) {
		kill_pid(find_pid_ns(ktsdb_socket_server_pid, &init_pid_ns),
			SIGTERM, 1);
		wait_for_completion(&threadcomplete);
	}
	if(serversocket) {
		sock_release(serversocket);
	}
	//------------------------------
	dev_free();
	//------------------------------
}

module_init(server_init);
module_exit(server_exit);
MODULE_LICENSE("GPL");