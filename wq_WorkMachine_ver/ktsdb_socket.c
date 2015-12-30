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
//#include<linux/kthread.h>
//----------

//---workqueue-------
#include <linux/workqueue.h>
struct workqueue_struct *wq = NULL;
struct work_struct sock_work;
//-------------------

#define DEVICE_READ "ktsdb_read"
#define DEVICE_WRITE "ktsdb_write"
//等待回复的时间 单位：毫秒
#define WAIT_TIME 1000

#define READ_COMMAND 1
#define WRITE_COMMAND 2
#define CLOSE_SOCKET 3

//开放端口号
static const unsigned short server_port = 5555;
static struct socket *serversocket=NULL;
static DECLARE_COMPLETION(threadcomplete);

static int ktsdb_socket_server_pid;
//static struct task_struct *ktsdb_socket_server_pid;

static unsigned char *read_buffers;
static unsigned char *write_buffers;

size_t count = 0;//记录连接的客户端总数
size_t rmmod_flag = 0;//卸载模块时的标志位 flag = 1 表示模块将要被卸载，需要令所有客户端退出

struct param_struct
{
	struct socket *client;
	size_t status;
	struct work_struct sock_work;
};

//struct param_struct ps[100];
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

static void work_machine(struct work_struct *sock_work)
{
	bool re_read = true;
	bool stop = false;
	int len = 0;
	long start_time = 0;
	unsigned char buffer[100];
struct linger so_linger;
	struct param_struct *param_s = container_of(sock_work, struct param_struct, sock_work);
	while (!stop)
	{
printk("cli:%p,com:%d,count:%d\n",param_s->client,param_s->status,count);
	switch(param_s->status)
	{
		case READ_COMMAND:
			server_send(param_s->client, "READ\r\n", strlen("READ\r\n"));
			start_time = jiffies_64;
			while(jiffies_64 - start_time < WAIT_TIME)
			{
			re_read = true;
			len = server_receive(param_s->client, buffer, sizeof(buffer));
			if (len == 0)
			{
				param_s->status = CLOSE_SOCKET;
				re_read = false;
				break;
			}
			if (len > 0)
			{
				param_s->status = WRITE_COMMAND;
				re_read = false;
				break;
			}
			}
			if (re_read)
			{
	struct param_struct *ps = (struct param_struct *)kmalloc(sizeof(struct param_struct),GFP_KERNEL);
			//ps[count].status = READ_COMMAND;
			//ps[count].client = param_s->client;
ps->status = READ_COMMAND;
ps->client = param_s->client;
			//INIT_WORK(&(ps[count].sock_work),work_machine);
INIT_WORK(&(ps->sock_work),work_machine);
			//queue_work(wq,&(ps[count].sock_work));
queue_work(wq,&(ps->sock_work));
			count++;
			stop = true;
			}
			break;
		case WRITE_COMMAND:
			server_send(param_s->client, "WRITE\r\n", strlen("WRITE\r\n"));
			param_s->status = READ_COMMAND;
			break;
		default:		
printk("quit success:%p\n",param_s->client);
			so_linger.l_onoff = 1;
			so_linger.l_linger = 0;
			kernel_setsockopt(param_s->client, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));
			sock_release(param_s->client);
printk("quit:%p\n",param_s->client);
			param_s->client = NULL;
			kfree(param_s);
			stop = true;
			break;
	}
	}
	count--;
}

static void socket_work(struct work_struct *sock_work)
{
	struct param_struct *param_s = container_of(sock_work, struct param_struct, sock_work);

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
	strcpy(read_buffers,"AllowChange");
	read_buffers[11]='\0';
	strcpy(write_buffers,"AllowChange");
	write_buffers[11]='\0';

	while (1)
	{
		len = server_receive(param_s->client, buffer, sizeof(buffer));
		//server_receive采用非阻塞模式，断开连接的?跫?
		//1：客户端主动退出，即server_receive的返回值len < 0;
		//2：内核模块即将被卸载，命令所有客户端断开连接，即rmmod_flag == 1;

		if (len > 0 && rmmod_flag == 0 && !strcmp(read_buffers,"AllowChange"))
		{
server_send(param_s->client, "sendcommand\r\n", strlen("sendcommand\r\n"));
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
			sprintf(client,"%p",param_s->client);
			strcat(read_buffers,client);
			strcat(read_buffers," ");
			len+=strlen(client);
			len++;

			strcat(read_buffers,buffer);
			long start_time = 0;

			//等待结果，并将write_buffers中的返回结果发送出去
			start_time = jiffies_64;
server_send(param_s->client, read_buffers, strlen(read_buffers));
			while(1)
			{
				if (rmmod_flag == 1 || jiffies_64-start_time > WAIT_TIME)
				{
					server_send(param_s->client, "timeout\r\n", strlen("timeout\r\n"));
					break;
				}
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
					server_send(param_s->client, return_result, strlen(return_result));
					//send \r\n for test
					server_send(param_s->client, "\r\n", 2);

					break;
				}
			}// end of while(1)
			strcpy(write_buffers,"AllowChange");
			write_buffers[11]='\0';
		}
		else if (len == 0 || rmmod_flag == 1)
		{
			struct linger so_linger;
			so_linger.l_onoff = 1;
			so_linger.l_linger = 0;
			kernel_setsockopt(param_s->client, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));
			sock_release(param_s->client);
			param_s->client = NULL;
			count--;
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
			//ps[count].client = clientsocket;
			//ps[count].status=READ_COMMAND;
struct param_struct *ps = (struct param_struct *)kmalloc(sizeof(struct param_struct),GFP_KERNEL);
	ps->client = clientsocket;
	ps->status=READ_COMMAND;
			//workqueue 调度工作
//INIT_WORK(&(ps[count].sock_work),work_machine);
INIT_WORK(&(ps->sock_work),work_machine);

			//queue_work(wq,&(ps[count].sock_work));
queue_work(wq,&(ps->sock_work));
			count++;//客?Ф思剖右?
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
	//升级3.10.94内核，采用在单个CPU上可并行化(Concurrency Managed Workqueue)的workqueue(linux/workqueue.h)
	/**
	 * alloc_ordered_workqueue - allocate an ordered workqueue
	 * @fmt: printf format for the name of the workqueue
	 * @flags: WQ_* flags (only WQ_FREEZABLE and WQ_MEM_RECLAIM are meaningful)
	 * @args: args for @fmt
	 *
	 * Allocate an ordered workqueue.  An ordered workqueue executes at
	 * most one work item at any given time in the queued order.  They are
	 * implemented as unbound workqueues with @max_active of one.
	 *
	 * RETURNS:
	 * Pointer to the allocated workqueue on success, %NULL on failure.
	 */
//	wq = alloc_workqueue("workqueue",WQ_MEM_RECLAIM, 100);

	if(create_socket()<0)
		return -EIO;

	ktsdb_socket_server_pid=kernel_thread(ktsdb_socket_server,NULL, CLONE_KERNEL);
	//升级3.10.94内核，kernel_thread没有EXPORT_SYMBOL，采用kthread_run(linux/kthread.h)
	/*
	* kthread_run - create and wake a thread.
	* @threadfn: the function to run until signal_pending(current).
	* @data: data ptr for @threadfn.
	* @namefmt: printf-style name for the thread.
	*/
//	ktsdb_socket_server_pid=kthread_run(ktsdb_socket_server,NULL, "kernel_socket");
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
//	while (count != 0){schedule_timeout_uninterruptible(100);}//等待所有客户端断开连接

	//向socket连接线程发送终止命令并等待其执行结束
	if(ktsdb_socket_server_pid)
	{
		kill_pid(find_pid_ns(ktsdb_socket_server_pid, &init_pid_ns),
			SIGTERM, 1);
//		kthread_stop(ktsdb_socket_server_pid);
printk("waiting\n");
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