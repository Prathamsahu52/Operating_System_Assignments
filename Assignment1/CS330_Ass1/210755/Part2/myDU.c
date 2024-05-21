#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <wait.h>

// only used for symbolic links


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Unable to execute");
        exit(1);
    }
	char *rootdir= argv[1];
	DIR* dir = opendir(rootdir);
	if(dir==NULL){
		perror("Unable to execute");
		exit(1);
	}

	int fd[2];

	if(pipe(fd)==-1){
		perror("Unable to execute");
		exit(1);
	}

	struct dirent* entry;
	entry= readdir(dir);
	struct stat filestat;
	stat(rootdir, &filestat);
	unsigned long total_size=filestat.st_size;


	
	while(entry!=NULL){
		if(strcmp(entry->d_name, ".")==0 || strcmp(entry->d_name,"..")==0) {
			entry= readdir(dir);
			continue;
		}
		char fullpath[4096];

		strcpy(fullpath, rootdir);
		strcat(fullpath, "/");
		strcat(fullpath, entry->d_name);

		if(stat(fullpath, &filestat)==-1){
			perror("stat()");
			exit(1);
		}

		if(S_ISREG(filestat.st_mode)){
			total_size+=filestat.st_size;
		}else if(S_ISDIR(filestat.st_mode)){
			total_size+=filestat.st_size;
			pid_t pid= fork();

			if(pid==0){

				dir=opendir(fullpath);
				strcpy(rootdir,fullpath);

				if(dir==NULL){
					perror("Unable to execute");
					exit(1);
				}
				total_size=0;
				dup2(fd[1], 1);
			}else{
				wait(NULL);
				char buffer[4096];
				unsigned long size=0;
				
				read(fd[0], buffer, sizeof(buffer));
				size=atoi(buffer);
				total_size+=size;
			}
	
		}
		entry= readdir(dir);

	}
    printf("%lu\n", total_size);	
	exit(0);
    return 0;
}
