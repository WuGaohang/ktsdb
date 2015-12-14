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
#define DB_ERR -1
#define PARAM_ERR -2
#define GET_NOT_FOUND -3

db_t *db;
static db_options_t *db_o;
const char *db_addr = "/home/vincent/file/ktsdb/ktsdb_socket/leveldb";
char *dberr = NULL;
static db_writeoptions_t *db_wo;
static db_readoptions_t *db_ro;

unsigned char *read_map;
unsigned char *write_map;

void usage()
{
	strcat(write_map, "-----------------------------\r\n");
	strcat(write_map, "here printf usage\r\n");
	strcat(write_map, "-----------------------------");
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
		*status PARAM_ERR;
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

	fd_read = open("/dev/ktsdb_read",O_RDONLY);
	fd_write = open("/dev/ktsdb_write",O_RDWR);
	char *line = (char*)malloc(sizeof(char)*100);

	char *prior_timestamp = (char*)malloc(sizeof(char)*100);
	dstr *command_with_timestamp = NULL;
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
		strcpy(line, read_map);
		command_with_timestamp = dstr_split_len(line, strlen(line), " ", 1, &count1);
		if (count1 > 0 && strcmp(command_with_timestamp[0],prior_timestamp))
		{			
			strcpy(prior_timestamp,command_with_timestamp[0]);
			strcpy(line, read_map+strlen(command_with_timestamp[0])+1);

			fields = dstr_split_len(line, strlen(line), " ", 1, &count);
			if (count > 0)
			{
				if (!strcmp(fields[0], "put"))
				{
					int status = put_command(fields, count);
					switch (status)
					{
						case DB_ERR:
							strcpy(write_map, "put error");
							break;
						case PARAM_ERR:
							strcpy(write_map, "not enough parameter");
							usage();
							break;
						default:
							strcpy(write_map, "put success");
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
							strcpy(write_map, "get error");
							break;
						case PARAM_ERR:
							strcpy(write_map, "not enough parameter");
							usage();
							break;
						case GET_NOT_FOUND:
							strcpy(write_map, "not found");
							break;
						default:
							strcpy(write_map, "Get from leveldb:");
							strcat(write_map,output_value);
					}
					free(output_len);
					free(status);
				}
				else
				{
					strcpy(write_map, "unknown command\n");
					usage();
				}
			}
			dstr_free_tokens(fields, count);
		}
		dstr_free_tokens(command_with_timestamp, count1);
	}
//----------------------------------------------------------------------------------
/*
	char *prior_map = (char*)malloc(sizeof(char)*100);
	strcpy(prior_map,read_map);

 	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN;

	while(1)
	{
		if (strcmp(prior_map,read_map))
		{
			printf("input events\n");
			strcpy(write_map,"from user space: input events");
		}
	}
*/
//-----------------------------------------------------------------------------------
	dstr_free(fields);
	dstr_free(command_with_timestamp);
	munmap(read_map, PAGE_SIZE);
	munmap(write_map, PAGE_SIZE);
	return 0;
}