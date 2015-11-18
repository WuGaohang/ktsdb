#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

//-----leveldb-------
#include "db.h"
#include "dstr.h"
//-------------------

#define PAGE_SIZE 4096

int main(int argc , char *argv[])
{
	int fd;
	int i;
	unsigned char *p_map;
    
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

//	dstr dstr_input_key = dstr_new("");
//	dstr dstr_output_value = dstr_new("");
	char *input_key = (char*)malloc(sizeof(char)*100);
	char *input_value = (char*)malloc(sizeof(char)*100);
	char *output_value = (char*)malloc(sizeof(char)*100);

	while (1)
	{
		if (p_map[i] == '\0')
			break;
		char *temp = (char*)malloc(sizeof(char));

		temp[0] = p_map[i];
		//dstr_cat(dstr_input_key, temp);
		strcat(input_key, temp);

		i++;
	}

//	dstr dstr_input_value = dstr_new(dstr_input_key);
	strcpy(input_value, input_key);
//	printf("len %d   key: %s\n",dstr_length(dstr_input_key),dstr_input_key);
	db_t *db;
	static db_options_t *db_o;
	const char *tmp;
	char *dberr = NULL;
	size_t *output_len = (size_t*)malloc(sizeof(size_t));
	static db_writeoptions_t *db_wo;
	static db_readoptions_t *db_ro;

	db_o = db_options_create();
	db_wo = db_writeoptions_create();
	db_ro = db_readoptions_create();
	
	//tmp: address
	tmp = "/home/vincent/file/ktsdb/ktsdb_socket/leveldb";
	db = db_open(db_o, tmp, &dberr);
	if (dberr)
	{
		printf("Opening database: %s\n", dberr);
		db_free(dberr);
	}
	else
	{
//		db_put(db, db_wo, dstr_input_key, dstr_length(dstr_input_key), dstr_input_value, dstr_length(dstr_input_value), &dberr);
		printf("write %s\n",input_key);
		printf("len in:%d\n",strlen(input_key));
		db_put(db, db_wo, input_key, strlen(input_key), input_value, strlen(input_value), &dberr);

		if (dberr)
		{
			printf("input : %s\n", dberr);
			db_free(dberr);
		}
		else
		{
			printf("before read:%s\n",output_value);
			//db_get(db, db_ro, dstr_input_key, dstr_length(dstr_input_key), output_len, &dberr);
			output_value = db_get(db, db_ro, input_key, strlen(input_key), output_len, &dberr);
			printf("len out:%d\n",*output_len);
			output_value[*output_len] = '\0';
			printf("after read:%s\n",output_value);
		}
	}

	munmap(p_map, PAGE_SIZE);
	return 0;
}