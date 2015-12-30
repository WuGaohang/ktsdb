#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "db.h"
#include "dstr.h"

//------------------------------
//poll
#include <poll.h>
#define TIME_DELAY 100000
//------------------------------

#define PAGE_SIZE 4096
#define DB_ERR -1 //DB_ERR数据库错误
#define PARAM_ERR -2 //PARAM_ERR 命令参数错误
#define GET_NOT_FOUND -3 //GET_NOT_FOUND get命令没有找到相应结果

db_t *db;
static db_options_t *db_o;
const char *db_addr = "/home/vincent/file/ktsdb/ktsdb_socket/leveldb";
char *dberr = NULL;
static db_writeoptions_t *db_wo;
static db_readoptions_t *db_ro;

unsigned char *read_map;
unsigned char *write_map;


//result记录返回结果
char *result;

void usage()
{
	strcat(result, "-----------------------------\r\n");
	strcat(result, "here printf usage\r\n");
	strcat(result, "-----------------------------");
}

//在字符串src之前加上前缀和空格，并将结果赋值给dest，即dest=prefix+空格+src
void add_prefix(char* dest, char* src,char* prefix)
{
	char *temp = (char*)malloc(sizeof(char)*200);
	strcpy(temp,prefix);
	strcat(temp," ");
	strcat(temp,src);
	strcpy(dest,temp);
}

size_t put_command(dstr* fields,int count)
{
	if (count >= 3)
	{
		db_put(db, db_wo, fields[1], strlen(fields[1]), fields[2], strlen(fields[2]), &dberr);
		if (dberr)
		{
			db_free(dberr);
			return DB_ERR;
		}
		else
			return 0;
	}
	else
		return PARAM_ERR;
}

char* get_command(dstr* fields,int count, size_t *status, size_t *value_len)
{
	if (count >= 2)
	{
		char *value = db_get(db, db_ro, fields[1], strlen(fields[1]), value_len, &dberr);
		if (dberr)
		{
			db_free(dberr);
			*status = DB_ERR;
			return;
		}
		else if (value == NULL)
		{
			*status = GET_NOT_FOUND;
		}
		else
		{
			value[*value_len] = '\0';
			return value;
		}
	}
	else
	{
		*status = PARAM_ERR;
		return;
	}
}

int main(int argc , char *argv[])
{
	db_o = db_options_create();
	db_wo = db_writeoptions_create();
	db_ro = db_readoptions_create();
	
	db = db_open(db_o, db_addr, &dberr);
	if (dberr)
	{
		printf("Opening database: %s\n", dberr);
		db_free(dberr);
		return 0;
	}

	int fd_read, fd_write;

	fd_read = open("/dev/ktsdb_read",O_RDONLY); // 只读映射
	fd_write = open("/dev/ktsdb_write",O_RDWR); // 读写映射

	//line记录发送的原始消息
	char *line = (char*)malloc(sizeof(char)*2048);
	//prior_timestamp保存前一个时间戳
	char *prior_timestamp = (char*)malloc(sizeof(char)*2048);
	//client保存客户端标识
	char *client = (char*)malloc(sizeof(char)*2048);
	result= (char*)malloc(sizeof(char)*2048);

	dstr *command_with_prefix = NULL;
	dstr *fields = NULL;

	int count1 = 0;
	int count = 0;

	if (fd_read < 0||fd_write < 0)
	{
		printf("open fail\n");
		exit(1);
	}

	write_map = (unsigned char *)mmap(0, PAGE_SIZE, PROT_WRITE, MAP_SHARED,fd_write, 0);
	read_map = (unsigned char *)mmap(0, PAGE_SIZE, PROT_READ, MAP_SHARED,fd_read, 0);

	if (read_map == MAP_FAILED || write_map == MAP_FAILED)
	{
		printf("mmap fail %p  %p\n",read_map, write_map);
		munmap(read_map, PAGE_SIZE);
		munmap(write_map, PAGE_SIZE);
		return 0;
	}
	while (1)
	{
		command_with_prefix = dstr_split_len(read_map, strlen(read_map), " ", 1, &count1);
		//通过比较当前read_map中命令的时间戳与上一条命令的时间戳来判断是否有新的命令到达
//		if (count1 > 2 && strcmp(command_with_prefix[0],prior_timestamp))
		if (count1 > 2)
		{
printf("command:%s\n",read_map);
			//初始化result;
			strcpy(result,"");
			//改写prior_timestamp时间戳
			strcpy(prior_timestamp,command_with_prefix[0]);
			//记录client标识
			strcpy(client,command_with_prefix[1]);

			strcpy(line, read_map+strlen(command_with_prefix[0])+strlen(command_with_prefix[1])+2);

			fields = dstr_split_len(line, strlen(line), " ", 1, &count);

			if (count > 0)
			{
				if (!strcmp(fields[0], "put"))
				{
					int status = put_command(fields, count);
					switch (status)
					{
						case DB_ERR:
							strcpy(result, "put error");
							break;
						case PARAM_ERR:
							strcpy(result, "not enough parameter\r\n");
							usage();
							break;
						default:
							strcpy(result,fields[1]);
							strcat(result, " put success");
					}
				}
				else if (!strcmp(fields[0], "get"))
				{
					size_t *output_len = (size_t*)malloc(sizeof(size_t));
					size_t *status = (size_t*)malloc(sizeof(size_t));
					char *output_value = get_command(fields, count, status, output_len);
					switch (*status)
					{
						case DB_ERR:
							strcpy(result, "get error");
							break;
						case PARAM_ERR:
							strcpy(result, "not enough parameter\r\n");
							usage();
							break;
						case GET_NOT_FOUND:
							strcpy(result, "not found");
							break;
						default:
							strcpy(result, "Get from leveldb:");
							strcat(result,output_value);
					}
					free(output_len);
					free(status);
				}
				else
				{
					strcpy(result, "unknown command\n");
					usage();
				}
				//当写缓存中不是允许写的状态时，等待
				while (strcmp(write_map,"AllowChange"));
				//add_prefix: 将字符串result与prior_timestamp连接并赋值给write_map
				add_prefix(result,result,client);
				add_prefix(write_map,result,prior_timestamp);
printf("result:%s\n",write_map);
			}//end of if (count > 0)
			dstr_free_tokens(fields, count);
			//向内核回复“可读”
			strcpy(read_map,"AllowChange");
			read_map[11]='\0';
		}//end of if (count1 > 2 && strcmp(command_with_prefix[0],prior_timestamp))
		dstr_free_tokens(command_with_prefix, count1);
	}//end of while (1)

	dstr_free(fields);
	dstr_free(command_with_prefix);
	munmap(read_map, PAGE_SIZE);
	munmap(write_map, PAGE_SIZE);
	return 0;
}