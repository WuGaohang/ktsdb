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

#include<linux/sched.h>

//---workqueue-------
#include <linux/workqueue.h>
struct workqueue_struct *wq = NULL;
struct work_struct sock_work;
//-------------------

//�����豸�ļ�
#define DEVICE_READ "ktsdb_read"
#define DEVICE_WRITE "ktsdb_write"
//�ȴ�д���ʱ�� ��λ������
#define READ_WAIT_TIME 50
//�ȴ��ظ���ʱ�� ��λ������
#define WRITE_WAIT_TIME 1000

//work_machine״̬���еĶ���д���ر���������״̬����
#define READ_COMMAND 1
#define WRITE_COMMAND 2
#define CLOSE_SOCKET 3

//���Ŷ˿ں�
static const unsigned short server_port = 5555;
static struct socket *serversocket=NULL;
static DECLARE_COMPLETION(threadcomplete);

static int ktsdb_socket_server_pid;
//static struct task_struct *ktsdb_socket_server_pid;

//���ڴ������ڴ�Ķ�д�ַ�����ͨ���ı�read_buffers��write_buffers�ı乲���ڴ�����
static unsigned char *read_buffers;
static unsigned char *write_buffers;

size_t count = 0;//��¼��ǰ����workstruct����
size_t rmmod_flag = 0;//ж��ģ��ʱ�ı�־λ flag = 1 ��ʾģ�齫Ҫ��ж�أ���Ҫ�����пͻ����˳�

/**
 * param_struct : ���䵽workqueue�ϵĽṹ��
 * @client: �ͻ��˱�ʶ
 * @status: ״̬����ʼ״̬
 * @sock_work: ��ִ�к��� 
*/
struct param_struct
{
	struct socket *client;
	size_t status;
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

//��ͻ���sock���ͳ���Ϊlen����Ϣbuf
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

//�Է�����ģʽ�ӿͻ���sock��ȡ����Ϊlen����Ϣbuf
static int server_receive(struct socket *sock, unsigned char *buf, int len)
{
	if(sock->sk==NULL)
		return 0;

	struct kvec iov = {buf, len};
	struct msghdr msg = {.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL};//������ģʽ

//	len = kernel_recvmsg(sock, &msg, &iov, 1, len, 0);
	len = kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);

	return len;
}

//work_machine״̬�����ڶ���д�ͶϿ���������״̬���л���workqueue����Ҫ���еĹ���
static void work_machine(struct work_struct *sock_work)
{
	bool re_read = true; //re_readΪtrue��ʾ�ڵ�ǰʱ��Ƭ��û�дӿͻ��˶�����Ϣ���´�����Ҫ�����״̬
	bool stop = false; //stopΪtrue��ʾ��Ҫ�˳���ǰ�������ճ�CPU�Ա��������������Ա�ִ��
		             //stopΪtrue�����������1�����ζ�״̬��һ��ʱ����û�ж�����Ϣ��2���Ͽ�����״ִ̬�����
	int len = 0;
	long read_start_time = 0;
	long write_start_time = 0;
	unsigned char buffer[100];

	//�ÿͻ��˱�ʶ+ʱ�������ʾ����ͷ���ֵ������һ���ͻ���
	unsigned char client[100]; //��¼�ͻ��˱�ʶ�ı���
	unsigned char timestamp[100]; //��¼ʱ����ı���
	unsigned char* temp;

	unsigned char *return_timestamp;
	unsigned char *return_client;
	unsigned char *return_result;
	return_result = (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	return_client= (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	return_timestamp= (unsigned char *)kmalloc(PAGE_SIZE,GFP_KERNEL);

	//��ʼ�������ڴ�Ķ�дbuffer��������ֵ��"AllowChange"ʱ����ʾ��һ�������Ѿ�������/���ͣ����Խ��串��
	strcpy(read_buffers,"AllowChange");
	read_buffers[11]='\0';
	strcpy(write_buffers,"AllowChange");
	write_buffers[11]='\0';

	struct linger so_linger;

	struct param_struct *param_s = container_of(sock_work, struct param_struct, sock_work);

	//��ģ�齫��ж��ʱ���������пͻ���ͣ��Ŀǰ������ִ�йر�socket���Ӳ���
	if (1 == rmmod_flag)
	{
		param_s->status = CLOSE_SOCKET;
	}
	while (!stop)
	{
		switch(param_s->status)
		{
			/* ��״̬����Ҫ����ӿͻ����ж�ȡ��Ϣ
			 * 1�������ʱ��Ƭ�ڶ�����Ϣ������Ϣ���д������빲���ڴ沢ת��д״̬
			 * 2�������ʱ��Ƭ���յ��ͻ��˶Ͽ�������Ϣ����ת��Ͽ�����״̬
			 * 3�������ʱ��Ƭ��û���յ���Ϣ������workqueue�����һ�����Լ���ͬ�������˳�
			 *      (�൱���ӳ�һ��ʱ���ٶ�)���ճ�CPUִ����������
			 */
			case READ_COMMAND:
				read_start_time = jiffies_64;
				//while�˳�����������ʱ��Ƭ����
				while(jiffies_64 - read_start_time < READ_WAIT_TIME)
				{
					re_read = true;
					len = server_receive(param_s->client, buffer, sizeof(buffer));
					//�յ��ͻ��˶Ͽ�������Ϣ��ת��Ͽ�����״̬
					if (0 == len)
					{
						param_s->status = CLOSE_SOCKET;
						re_read = false;
						break;
					}
					//�����ͻ�����Ϣ������ʱ�����ڴ治��д�룬��ȴ�һ��ʱ��
					if (len > 0 && strcmp(read_buffers,"AllowChange")) {schedule_timeout_uninterruptible(1000);}
					//len > 0: �����ͻ�����Ϣ; 0 == rmmod_flag: �ں�ģ�鲻���ڼ�����ж�ص�״̬;
					//!strcmp(read_buffers,"AllowChange"): �����ڴ��д��
					if (len > 0 && 0 == rmmod_flag && !strcmp(read_buffers,"AllowChange"))
					{
						//��������д��ڻس�����ȥ��
						if (strstr(buffer, "\r\n") && len > 2)
						{
							len-=2;
							buffer[len] = '\0';
						}	

						//��������д������ʱ����Լ�client��ʶ
						//���ʱ��������û�̬�����ж��Ƿ����������
						sprintf(timestamp,"%ld",jiffies_64);
						strcpy(read_buffers,timestamp);
						strcat(read_buffers," ");	
						len+=strlen(timestamp);
						len++;
						//���client��ʶ
						sprintf(client,"%p",param_s->client);
						strcat(read_buffers,client);
						strcat(read_buffers," ");
						len+=strlen(client);
						len++;
						//������д�빲���ڴ�
						strcat(read_buffers,buffer);
						//ת��д״̬
						param_s->status = WRITE_COMMAND;
						re_read = false;
						break;
					}
				}
				//����ʱ��Ƭ��û�ж�����Ϣ����Ҫ���´�ʱ��Ƭʱ���½��ж�����
				if (re_read)
				{
					//��workqueue�ϵ���һ���뱾����ͬ�Ķ����񣬲���stop��Ϊtrue�˳�����ִ��
					struct param_struct *ps = (struct param_struct *)kmalloc(sizeof(struct param_struct),GFP_KERNEL);
					ps->status = READ_COMMAND;
					ps->client = param_s->client;			
					INIT_WORK(&(ps->sock_work),work_machine);
					queue_work(wq,&(ps->sock_work));
					count++;
					stop = true;
				}
				break;
			/* д״̬����Ҫ�����ȡ�û�̬����ķ���ֵ�����͸��ͻ���
			 * ���һ��ʱ����û�ж����û�̬�ķ�����Ϣ������ͻ��˷��ͳ�ʱ��Ϣ
			 * ������Ϣ֮��ת���״̬
			*/
			case WRITE_COMMAND:
				//�ȴ����������write_buffers�еķ��ؽ�����ͳ�ȥ
				write_start_time = jiffies_64;
				while(1)
				{
					if (1 == rmmod_flag || jiffies_64-write_start_time > WRITE_WAIT_TIME)
					{
						server_send(param_s->client, "timeout\r\n", strlen("timeout\r\n"));
						break;
					}
					//�����ؽ�������ؽ���ĸ�ʽΪ��ʱ���+�ո�+�ͻ��˱�ʶ+�ո�+��ѯ���
					strcpy(return_result, write_buffers);
					if (strlen(return_result)!=0)
					{
						temp = strstr(return_result," ");
						if (temp != NULL)
						{
							//ȡ�����ؽ���е�ʱ����������ַ���return_timestamp�У�ʣ�²��ִ����ַ���return_result��
							strncpy(return_timestamp,return_result,strlen(return_result)-strlen(temp));
							return_timestamp[strlen(return_result)-strlen(temp)] = '\0';
							strcpy(return_result, temp);
							if (strlen(return_result)>1)
							{
								strcpy(return_result,return_result+1);//�����ո�
								temp = strstr(return_result," ");
								if (temp != NULL)
								{
									//ȡ�����ؽ���еĿͻ��˱�ʶ�������ַ���return_client�У�ʣ�²��ִ����ַ���return_result��
									strncpy(return_client,return_result,strlen(return_result)-strlen(temp));
									return_client[strlen(return_result)-strlen(temp)] = '\0';
								}
							}
						}
					}//end of if (strlen(return_result)!=0)
					//��write_buffers�е�ʱ����Ϳͻ��˱�ʶ�������ʱ������ͻ��˱�ʶƥ��ʱ������Ӧ�Ŀͻ��˷�����Ϣ
					if (!strcmp(return_timestamp,timestamp) && !strcmp(return_client,client))
					{
						//��ȡwrite_buffers�� "return_timestamp�ո�return_client�ո�" ֮�������(�����ķ��ؽ��)
						strcpy(return_result, write_buffers+strlen(return_timestamp)+strlen(return_client)+2);
						server_send(param_s->client, return_result, strlen(return_result));
						//send \r\n for test
						server_send(param_s->client, "\r\n", 2);
							break;
					}
				}// end of while(1)
				//������Ϣ��ϣ���write_buffers���Ϊ�����޸ģ���ת����״̬
				strcpy(write_buffers,"AllowChange");
				write_buffers[11]='\0';
				param_s->status = READ_COMMAND;
				break;
			// �Ͽ�����״̬: �Ͽ���ǰ�ͻ��˵�socket���ӣ�����stop��Ϊtrue�˳�����ִ��
			default:		
				so_linger.l_onoff = 1;
				so_linger.l_linger = 0;
				kernel_setsockopt(param_s->client, SOL_SOCKET, SO_LINGER,&so_linger,sizeof(so_linger));
				sock_release(param_s->client);
				printk("quit:%p\n",param_s->client);
				param_s->client = NULL;
				kfree(param_s);
				stop = true;
				break;
		}//end of switch
	}//end of while(!stop)
	kfree(return_result);
	kfree(return_client);
	kfree(return_timestamp);
	count--;
	return;
}

/*
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
		//server_receive���÷�����ģʽ���Ͽ����ӵ�����
		//1���ͻ��������˳�����server_receive�ķ���ֵlen < 0;
		//2���ں�ģ�鼴����ж�أ��������пͻ��˶Ͽ����ӣ���rmmod_flag = 1;

		if (len > 0 && 0 == rmmod_flag && !strcmp(read_buffers,"AllowChange"))
		{
server_send(param_s->client, "sendcommand\r\n", strlen("sendcommand\r\n"));
			if (strstr(buffer, "\r\n") && len > 2)
			{
				len-=2;
				buffer[len] = '\0';
			}

			//��������д������ʱ����Լ�client��ʶ
			//���ʱ��������û�̬�����ж��Ƿ����������
			sprintf(timestamp,"%ld",jiffies_64);
			strcpy(read_buffers,timestamp);
			strcat(read_buffers," ");	
			len+=strlen(timestamp);
			len++;
			//���client��ʶ
			sprintf(client,"%p",param_s->client);
			strcat(read_buffers,client);
			strcat(read_buffers," ");
			len+=strlen(client);
			len++;

			strcat(read_buffers,buffer);
			long start_time = 0;

			//�ȴ����������write_buffers�еķ��ؽ�����ͳ�ȥ
			start_time = jiffies_64;
server_send(param_s->client, read_buffers, strlen(read_buffers));
			while(1)
			{
				if (1 == rmmod_flag || jiffies_64-start_time > WAIT_TIME)
				{
					server_send(param_s->client, "timeout\r\n", strlen("timeout\r\n"));
					break;
				}
				//�����ؽ�������ؽ���ĸ�ʽΪ��ʱ���+�ո�+�ͻ��˱�ʶ+�ո�+��ѯ���
				strcpy(return_result, write_buffers);
				if (strlen(return_result)!=0)
				{
					temp = strstr(return_result," ");
					if (temp != NULL)
					{
						//ȡ�����ؽ���е�ʱ����������ַ���return_timestamp�У�ʣ�²��ִ����ַ���return_result��
						strncpy(return_timestamp,return_result,strlen(return_result)-strlen(temp));
						return_timestamp[strlen(return_result)-strlen(temp)] = '\0';
						strcpy(return_result, temp);
						if (strlen(return_result)>1)
						{
							strcpy(return_result,return_result+1);//�����ո�
							temp = strstr(return_result," ");
							if (temp != NULL)
							{
								//ȡ�����ؽ���еĿͻ��˱�ʶ�������ַ���return_client�У�ʣ�²��ִ����ַ���return_result��
								strncpy(return_client,return_result,strlen(return_result)-strlen(temp));
								return_client[strlen(return_result)-strlen(temp)] = '\0';
							}
						}
					}
				}//end of if (strlen(return_result)!=0)
				//��write_buffers�е�ʱ����Ϳͻ��˱�ʶ�������ʱ������ͻ��˱�ʶƥ��ʱ������Ӧ�Ŀͻ��˷�����Ϣ
				if (!strcmp(return_timestamp,timestamp) && !strcmp(return_client,client))
				{
					//��ȡwrite_buffers�� "return_timestamp�ո�return_client�ո�" ֮�������(�����ķ��ؽ��)
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
		else if (0 == len || 1 == rmmod_flag)
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
*/

//���socket�����Լ�workqueue�ĵ���
static int ktsdb_socket_server(void)
{
	daemonize("ktsdb_socket");
	//��������ź�SIGTERM(��ֹ����)
	allow_signal(SIGTERM);

	//�����յ��ź�ʱsignal_pending����ֵ��Ϊ0
	while(!signal_pending(current))
	{
		struct socket *clientsocket;

		//�ȴ��ͻ���socket����
		clientsocket = socket_accept(serversocket);
		printk("clientsocket(%p)\n", clientsocket);

		//���пͻ������ӳɹ�ʱ����������workqueue���е���
		while(clientsocket)
		{
			//��ز�������ṹ����
			struct param_struct *ps = (struct param_struct *)kmalloc(sizeof(struct param_struct),GFP_KERNEL);
			ps->client = clientsocket;
			ps->status=READ_COMMAND;
			//workqueue ���ȹ���
			INIT_WORK(&(ps->sock_work),work_machine);
			queue_work(wq,&(ps->sock_work));
			count++;
			clientsocket = NULL;
		}
	}
	complete(&threadcomplete);
	return 0;
}

//�ں�ģ����ڣ���Ҫ����ڴ�ӳ���豸�ĳ�ʼ����workqueue�Ĵ����Լ�socket�����̵߳Ĵ���
static int __init server_init(void)
{
	dev_init();
	wq = create_workqueue("workqueue");
	//����3.10.94�ںˣ������ڵ���CPU�Ͽɲ��л�(Concurrency Managed Workqueue)��workqueue(linux/workqueue.h)
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
	//����3.10.94�ںˣ�kernel_threadû��EXPORT_SYMBOL������kthread_run(linux/kthread.h)
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

//ж���ں�ģ��ʱִ�У���Ҫ��ɸ�����Դ���ͷ�
static void __exit server_exit(void)
{
	printk("server_exit() %d\n",ktsdb_socket_server_pid);
	
	rmmod_flag = 1;//��ж��ģ��ı�־λ��Ϊ1��֪ͨ�ͻ��˶Ͽ�����
	while (count > 0){schedule_timeout_uninterruptible(100);}//�ȴ����пͻ��˶Ͽ�����

	//��socket�����̷߳�����ֹ����ȴ���ִ�н���
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