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
#define WAIT_TIME 999999

//开放端口号
static const unsigned short server_port = 5555;
static struct socket *serversocket=NULL;
static DECLARE_COMPLETION(threadcomplete);

static int ktsdb_socket_server_pid;

static unsigned char *read_buffers;
static unsigned char *write_buffers;

size_t count = 0;//记录连接的客户端总数
size_t rmmod_flag = 0;//卸载模块时的标志位 flag = 1 表示模块将要被卸载，需要令所有客户端退出

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

static int dev_init(void)
{
	int ret1,ret2;

	read_buffers = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	write_buffers = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);

	SetPageReserved(virt_to_page(read_buffers));
	SetPageReserved(virt_to_page(write_buffers));
	ret1 = misc_register(&misc_read);
	ret2 = misc_register(&misc_write);

	return 0;
}

static void dev_free(void)
{
	misc_deregister(&misc_read);
	misc_deregister(&misc_write);
	ClearPageReserved(virt_to_page(read_buffers));
	ClearPageReserved(virt_to_page(write_buffers));
	kfree(read_buffers);
	kfree(write_buffers);
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
	struct msghdr msg = {.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL};//非阻塞模式

//	len = kernel_recvmsg(sptr, &msg, &iov, 1, len, 0);
	len = kernel_recvmsg(sptr, &msg, &iov, 1, len, msg.msg_flags);

	return len;
}


static void socket_work(struct work_struct *sock_work)
{
	struct param_struct *ps = container_of(sock_work, struct param_struct, sock_work);

	unsigned char buffer[100];
	unsigned char client[100];
	unsigned char timestamp[100];
	unsigned char* temp;
	static int len = 0;

	unsigned char *return_timestamp;
	unsigned char *return_client;
	unsigned char *return_result;
	return_result = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	return_client= (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	return_timestamp= (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);

	while (1)
	{
		len = server_receive(ps->client, buffer, sizeof(buffer));
		//server_receive采用非阻塞模式，断开连接的条件有两个：
		//1：客户端主动退出，即server_receive的返回值len < 0;
		//2：内核模块即将被卸载，命令所有客户端断开连接，即rmmod_flag == 1;
		if (len > 0 && rmmod_flag == 0)
		{
			if (strstr(buffer, "\r\n") && len > 2)
			{
				len-=2;
				buffer[len] = '\0';
			}

			//对命令进行处理，添加时间戳以及client标识
			//添加时间戳，让用户态服务判断是否有新命令到达
			sprintf(timestamp,"%ld",jiffies_64);
			strcpy(read_buffers,timestamp);
			strcat(read_buffers," ");	
			len+=strlen(timestamp);
			len++;
			//添加client标识
			sprintf(client,"%p",ps->client);
			strcat(read_buffers,client);
			strcat(read_buffers," ");
			len+=strlen(client);
			len++;

			strcat(read_buffers,buffer);
			int i = 0;

			//等待结果，并将write_buffers中的返回结果发送出去
			while(1)
			{
				i++;
				if (rmmod_flag == 1 || i > WAIT_TIME)
					break;
				//处理返回结果，返回结果的格式为：时间戳+空格+客户端标识+空格+查询结果
				strcpy(return_result, write_buffers);
				if (strlen(return_result)!=0)
				{
					temp = strstr(return_result," ");
					if (temp != NULL)
					{
						//取出返回结果中的时间戳，存在字符串return_timestamp中，剩下部分存在字符串return_result中
						strncpy(return_timestamp,return_result,strlen(return_result)-strlen(temp));
						return_timestamp[strlen(return_result)-strlen(temp)] = '\0';
						strcpy(return_result, temp);
						if (strlen(return_result)>1)
						{
							strcpy(return_result,return_result+1);//跳过空格
							temp = strstr(return_result," ");
							if (temp != NULL)
							{
								//取出返回结果中的客户端标识，存在字符串return_client中，剩下部分存在字符串return_result中
								strncpy(return_client,return_result,strlen(return_result)-strlen(temp));
								return_client[strlen(return_result)-strlen(temp)] = '\0';
							}
						}

					}
				}//end of if (strlen(return_result)!=0)
				//当write_buffers中的时间戳和客户端标识与命令的时间戳、客户端标识匹配时，向相应的客户端发送消息
				if (!strcmp(return_timestamp,timestamp) && !strcmp(return_client,client))
				{
					//读取write_buffers中 "return_timestamp空格return_client空格" 之后的内容(真正的返回结果)
					strcpy(return_result, write_buffers+strlen(return_timestamp)+strlen(return_client)+2);
					server_send(ps->client, return_result, strlen(return_result));
					//send \r\n for test
					server_send(ps->client, "\r\n", 2);
					break;
				}
			}// end of while(1)
		}
		else if (len == 0 || rmmod_flag == 1)
		{
			struct linger so_linger;
			so_linger.l_onoff = 1;
			so_linger.l_linger = 0;
			kernel_setsockopt(ps->client, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));
			sock_release(ps->client);
			ps->client = NULL;
			count--;//客户端计数减一
			break;
		}
	}
	kfree(return_result);
	kfree(return_client);
	kfree(return_timestamp);
	return;
}

//完成socket连接以及workqueue的调度
static int ktsdb_socket_server(void)
{
	daemonize("ktsdb_socket");
	//允许接收信号SIGTERM(终止进程)
	allow_signal(SIGTERM);

	struct param_struct ps[10];

	//当接收到信号时signal_pending返回值不为0
	while(!signal_pending(current))
	{
		struct socket *clientsocket;

		//等待客户端socket连接
		clientsocket = socket_accept(serversocket);
		printk("clientsocket(%p)\n", clientsocket);

		//当有客户端连接成功时，将其分配给workqueue进行调度
		while(clientsocket)
		{
			//相关参数填入结构体中
			ps[count].client = clientsocket;
			//workqueue 调度工作
			INIT_WORK(&(ps[count].sock_work),socket_work);
			queue_work(wq,&(ps[count].sock_work));
			count++;//客户端计数加一
			clientsocket = NULL;
		}
	}
	complete(&threadcomplete);
	return 0;
}

//内核模块入口，主要完成内存映射设备的初始化，workqueue的创建以及socket连接线程的创建
static int __init server_init(void)
{
	dev_init();
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

//卸载内核模块时执行，主要完成各种资源的释放
static void __exit server_exit(void)
{
	printk("server_exit() %d\n",ktsdb_socket_server_pid);
	
	rmmod_flag = 1;//将卸载模块的标志位置为1，通知客户端断开连接
	while (count != 0){schedule_timeout_uninterruptible(100);}//等待所有客户端断开连接

	//向socket连接线程发送终止命令并等待其执行结束
	if(ktsdb_socket_server_pid)
	{
		kill_pid(find_pid_ns(ktsdb_socket_server_pid, &init_pid_ns),
			SIGTERM, 1);
		wait_for_completion(&threadcomplete);
	}
	if(serversocket)
		sock_release(serversocket);

	destroy_workqueue(wq);
	dev_free();
}

module_init(server_init);
module_exit(server_exit);
MODULE_LICENSE("GPL");