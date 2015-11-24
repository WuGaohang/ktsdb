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

unsigned char *p_map;

void usage()
{
	strcat(p_map, "-----------------------------\r\n");
	strcat(p_map, "here printf usage\r\n");
	strcat(p_map, "-----------------------------");
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

	int fd;
	int i;
    
	fd = open("/dev/ktsdb",O_RDWR);
	if(fd < 0)
	{
		printf("open fail\n");
		exit(1);
	}

	p_map = (unsigned char *)mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,fd, 0);
	if(p_map == MAP_FAILED)
	{
		printf("mmap fail\n");
		munmap(p_map, PAGE_SIZE);
		return 0;
	}

	i = 0;

	char *line = (char*)malloc(sizeof(char)*100);
	
	while (1)
	{
		if (p_map[i] == '\0')
			break;
		line[i] = p_map[i];
		i++;
	}
	line[i] = '\0';

	dstr *fields = NULL;
	int count = 0;

	fields = dstr_split_len(line, strlen(line), " ", 1, &count);

	if (count > 0)
	{
		if (!strcmp(fields[0], "put"))
		{
			int status = put_command(fields, count);
			switch (status)
			{
				case DB_ERR:
					strcpy(p_map, "put error");
					break;
				case PARAM_ERR:
					strcpy(p_map, "not enough parameter");
					usage();
					break;
				default:
					strcpy(p_map, "put success");
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
					strcpy(p_map, "get error");
					break;
				case PARAM_ERR:
					strcpy(p_map, "not enough parameter");
					usage();
					break;
				case GET_NOT_FOUND:
					strcpy(p_map, "not found");
					break;
				default:
					strcpy(p_map, "Get from leveldb:");
					strcat(p_map,output_value);
			}
		}
		else
		{
			strcpy(p_map, "unknown command\n");
			usage();
		}
	}

	munmap(p_map, PAGE_SIZE);
	return 0;
}