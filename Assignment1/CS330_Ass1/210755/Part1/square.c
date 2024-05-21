#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<math.h>
#include<string.h>
#include<wait.h>

int main(int argc, char *argv[])
{
	double x= strtod(argv[argc-1],NULL);
	x= x*x;
	// printf("%lf\n",x);
	// printf("argc = %d\n", argc);
	if(argc==2){
		int out= round(x);
		printf("%d\n",out);
		exit(0);
	}

	pid_t pid= fork();
	char str[100];
	sprintf(str, "%lf", x);
	argv[argc-1]=str;

	if(pid==0){
		if(!strcmp(argv[1],"sqroot")){
			execv("./sqroot",argv+1);
		}
		else if(!strcmp(argv[1],"square")){
			execv("./square",argv+1);
		}
		else if(!strcmp(argv[1],"double")){
			execv("./double", argv+1);
		}else{
			perror("Unable to execute");
		}

	}
	else{
		wait(NULL);
	}


}
