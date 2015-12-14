#include<stdio.h>
#include <string.h>
#ifdef WIN32
char * lltochar(__int64 a )
{
	char buf[20]={0};
	sprintf(buf,"%I64d",a);
	return buf;
}
#else
char * lltochar(long long a )
{
	char buf[20]={0};
	sprintf(buf,"%lld",a);
	
	printf("%d\n",strlen(buf));
	buf[strlen(buf)] = '\0';

	printf("%p\n",buf);
	return buf;

}
#endif
int main()
{
	char out[21]={0};
	long long  num=99999999999999;

	strcpy(out,lltochar(num));

//	out[strlen(out)] = '\0';

	printf("%s\n",out);
	return 1;
}