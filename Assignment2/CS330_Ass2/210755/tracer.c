#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>

#define DELIMITER 0x10000003B
///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

void print_unsigned_long(unsigned long value) {
    char buffer[21]; // Enough to hold all digits of an unsigned long in base 10
    int i = sizeof(buffer) - 1;

    buffer[i] = '\0'; // Null-terminate the string
    if (value == 0) {
        buffer[--i] = '0';
    } else {
        while (value > 0) {
            buffer[--i] = '0' + (value % 10); // Convert the least significant digit to a character
            value /= 10; // Remove the least significant digit
        }
    }

    printk("%s\n", &buffer[i]);
}


int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	struct exec_context *ctx= get_current_ctx();
	u32 read=0;
	u32 write=0;
	u32 MAX_MM_SEGS= sizeof(ctx->mms)/sizeof(struct mm_segment);

	for(int i=0;i<MAX_MM_SEGS;i++){
		if(i!=(MAX_MM_SEGS-1) && ctx->mms[i].start<=buff && ((ctx->mms[i].next_free)-1)>=(buff+count)){

			u32 access_b= ctx->mms[i].access_flags;
			read= access_b%2;
			access_b=access_b/2;
			write=access_b%2;	
		}else if(ctx->mms[i].start<=buff && ((ctx->mms[i].end)-1)>=(buff+count)){

			u32 access_b= ctx->mms[i].access_flags;
			read= access_b%2;
			access_b=access_b/2;
			write=access_b%2;	
		}
	}
	
	struct vm_area* vm= ctx->vm_area;
	while(vm!=NULL){
		if(vm->vm_start<=buff && (vm->vm_end - 1)>=(buff+count)){
			u32 access_b= vm->access_flags;
			read= access_b%2;
			access_b=access_b/2;
			write=access_b%2;
		}

		vm=vm->vm_next;
	}

	if(access_bit==1 && read==1){
		return 1;
	}else if(access_bit==0 && write==1){
		return 1;
	}else{
		return 0;
	}

}

long trace_buffer_close(struct file *filep)
{
	if (filep == NULL) {
		return -EINVAL;
	}
	os_page_free(USER_REG, filep->trace_buffer->trace_buffer_mem);
	os_page_free(USER_REG, filep->trace_buffer->usage_arr);
	os_page_free(USER_REG,filep->fops);
	os_page_free(USER_REG,filep->trace_buffer);
	os_page_free(USER_REG,filep);
	filep=NULL;

	return 0;	
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	
	if(filep->mode==O_WRITE){
			return -EINVAL;
	}	
	if(!is_valid_mem_range((unsigned long)buff,count, 0)){
		return -EBADMEM;
	}
	
	struct trace_buffer_info *trace_buf= filep -> trace_buffer;
	u32 read_index= trace_buf->offread;
	u32 count_read=0;
	
	while(count_read < count && trace_buf->usage_arr[read_index]==1){
		
		trace_buf->usage_arr[read_index]=0;
		buff[count_read]=trace_buf->trace_buffer_mem[read_index];
		read_index=(read_index+1)%TRACE_BUFFER_MAX_SIZE;
		count_read++;
	
	}

	filep->trace_buffer->offread= read_index;
	filep->offp= read_index;
	
	return count_read;
}


int trace_buffer_write(struct file *filep, char *buff, u32 count)
{	
		if(!is_valid_mem_range((unsigned long)buff,count, 1)){
			printk("not in valid mem range\n");
			return -EBADMEM;
		}
		struct trace_buffer_info *trace_buf= filep->trace_buffer;
		u32 write_index= trace_buf->offwrite;
		u32 count_written=0;
		while(count_written< count && trace_buf->usage_arr[write_index]==0){

			trace_buf->usage_arr[write_index]=1;
			trace_buf->trace_buffer_mem[write_index] = buff[count_written];
			write_index=(write_index+1)%TRACE_BUFFER_MAX_SIZE;
			count_written++;
		}
		filep->trace_buffer->offwrite = write_index;
		filep->offp=write_index;
    	return count_written;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{

	if(mode<=0||mode>3){
		return -EINVAL;
	}
	for(int i=0;i<MAX_OPEN_FILES;i++){
		if(current->files[i]==NULL){
			struct file *filep=(struct file*)os_page_alloc(USER_REG);
			if(!filep){
				return -ENOMEM;
			}
			//regular->0 trace->1
			filep->type=TRACE_BUFFER;
			// O_READ->1, O_WRITE->2,O_RDWR->3(1|2)
			filep->mode=mode;
			filep->offp=0;
			filep->ref_count=1;
			filep->inode=NULL;
			struct trace_buffer_info *tracei=(struct trace_buffer_info *)os_page_alloc(USER_REG);
			if(!tracei){
				return -ENOMEM;
			}
			tracei->offread=0;
			tracei->offwrite=0;
			char *memory_buff=(char*)os_page_alloc(USER_REG);
			tracei->trace_buffer_mem=memory_buff;
			char *used_arr=(char*)os_page_alloc(USER_REG);
			tracei->usage_arr= used_arr;
			for(int j=0;j<TRACE_BUFFER_MAX_SIZE;j++){
				tracei->usage_arr[j]=0;
				if(tracei->usage_arr[j]!=0){
					printk("j:%d\n", tracei->usage_arr[j]);
				}
				
			}

			tracei->bufffd=i;
			filep->trace_buffer=tracei;
			struct fileops* fopt= (struct fileops*)os_page_alloc(USER_REG);
			if(!fopt){
				return -ENOMEM;
			}
			
			fopt->write= &trace_buffer_write;
			fopt->read= &trace_buffer_read;
			fopt->lseek=NULL;
			fopt->close= &trace_buffer_close;
			filep->fops=fopt;
			current->files[i]=filep;

			return i;
		}	
	}

	return -EINVAL;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


int num_arguments(int syscall_num) {
    switch(syscall_num) {
        case SYSCALL_EXIT: return 1; // exit(int)
        case SYSCALL_GETPID: return 0; // getpid()
        case SYSCALL_FORK: return 0; // fork() 
        case SYSCALL_CFORK: return 0; // cfork()
        case SYSCALL_VFORK: return 0; // vfork()
        case SYSCALL_GET_USER_P: return 0; // get_user_page_stats()
        case SYSCALL_GET_COW_F: return 0; // get_cow_fault_stats()
        case SYSCALL_SIGNAL: return 2; // signal(int, void*)
        case SYSCALL_SLEEP: return 1; // sleep(int)
        case SYSCALL_EXPAND: return 2; // expand(unsigned, int)
        case SYSCALL_CLONE: return 2; // clone(void*, long)
        case SYSCALL_DUMP_PTT: return 1; // dump_page_table(char*)
        case SYSCALL_PHYS_INFO: return 0; // physinfo()
        case SYSCALL_STATS: return 0; // get_stats()
        case SYSCALL_CONFIGURE: return 1; // configure(struct os_configs*)
        case SYSCALL_MMAP: return 4; // mmap(void*, int, int, int)
        case SYSCALL_MUNMAP: return 2; // munmap(void*, int)
        case SYSCALL_MPROTECT: return 3; // mprotect(void*, int, int)
        case SYSCALL_PMAP: return 1; // pmap(int)
        case SYSCALL_OPEN: return 3; // open(char*, int, ...)
        case SYSCALL_WRITE: return 3; // write(int, void*, int)
        case SYSCALL_READ: return 3; // read(int, void*, int)
        case SYSCALL_DUP: return 1; // dup(int)
        case SYSCALL_DUP2: return 2; // dup2(int, int)
        case SYSCALL_CLOSE: return 1; // close(int)
        case SYSCALL_LSEEK: return 3; // lseek(int, long, int)
        case SYSCALL_FTRACE: return 4; // ftrace(unsigned long, long, long, int)
		case SYSCALL_TRACE_BUFFER: return 1;
        case SYSCALL_START_STRACE: return -1; // start_strace(int, int)
        case SYSCALL_END_STRACE: return -1; // end_strace()
        case SYSCALL_STRACE: return 2; // strace(int, int)
        case SYSCALL_READ_STRACE: return 3; // read_strace(int, void*, int)
        case SYSCALL_READ_FTRACE: return 3; // read_ftrace(int, void*, int)
        default: return -1; // Unknown syscall number
    }
}


int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	if(!current->st_md_base){
		struct strace_head *st_head= (struct strace_head*)os_alloc(sizeof(struct strace_head));
		st_head->count=0;
		st_head->is_traced=0;
		st_head->tracing_mode=-1;
		st_head->strace_fd=-1;
		current->st_md_base=st_head;
		current->st_md_base->next = NULL;
		current->st_md_base->last = NULL;
	}
	
	struct strace_head *st_head=current->st_md_base;
	struct strace_info* pcrawl=st_head->next;
 
	if(current->st_md_base->count>STRACE_MAX){
		return -EINVAL;
	}

	if(action==ADD_STRACE){

		int found=0;
		while(pcrawl!=NULL && found==0){

			if(pcrawl->syscall_num==syscall_num){
				found=1;
			}
			pcrawl=pcrawl->next;
		}

		if(found==1){
			return -EINVAL;
		}

		struct strace_info* new_info= (struct strace_info*)os_alloc(sizeof(struct strace_info));
		new_info->syscall_num=syscall_num;
		new_info->next=NULL;
		if(st_head->next==NULL){
			st_head->next=new_info;
		}else{
			st_head->last->next=new_info;
		}
		st_head->count+=1;
		st_head->last=new_info;
		
	}else if(action==REMOVE_STRACE){
		int found=0;
		struct strace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->syscall_num==syscall_num){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}

		if(found==0){
			return -EINVAL;
		}
	
		if(pcrawl==st_head->next){
			st_head->next=st_head->next->next;
		}else if(pcrawl==st_head->last){
			st_head->last=prev;
			prev->next=NULL;
		}else{
			prev->next=pcrawl->next;
		}
		st_head->count-=1;
		os_free(pcrawl, sizeof(struct strace_info));
	}else{
		return -EINVAL;
	}
	return 0;
}


int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{	
	u64 parameters[5]={syscall_num, param1, param2, param3, param4};
	struct exec_context* ctx= get_current_ctx();	
	struct strace_head *st_head= ctx->st_md_base;
	struct file* trace_buffer_file= ctx->files[st_head->strace_fd];
	struct trace_buffer_info* trace_buffer= trace_buffer_file->trace_buffer;
	int params = num_arguments(syscall_num);
	if(st_head->is_traced==1){

		if(st_head->tracing_mode==FULL_TRACING){
			for(int i=0;i<(params+1);i++){
				u32 write_index= trace_buffer->offwrite;
				u32 count_written=0;
				while(count_written < sizeof(u64) && trace_buffer->usage_arr[write_index]==0){
					trace_buffer->usage_arr[write_index]=1;
					char* buff= (char*)(&parameters[i]);
					trace_buffer->trace_buffer_mem[write_index]=buff[count_written];
					write_index=(write_index+1)%TRACE_BUFFER_MAX_SIZE;
					count_written++;
				}

				trace_buffer->offwrite=write_index;
				trace_buffer_file->offp=write_index;

			}
		}else if(st_head->tracing_mode==FILTERED_TRACING){
			struct strace_info *pcrawl=st_head->next;
			for(int j=0;j<st_head->count;j++){
				
				if(pcrawl->syscall_num==syscall_num){

					for(int i=0;i<(params+1);i++){
						u32 write_index= trace_buffer->offwrite;
						u32 count_written=0;
						while(count_written < sizeof(u64) && trace_buffer->usage_arr[write_index]==0){
							trace_buffer->usage_arr[write_index]=1;
							char* buff= (char*)(&parameters[i]);
							trace_buffer->trace_buffer_mem[write_index]=buff[count_written];
							write_index=(write_index+1)%TRACE_BUFFER_MAX_SIZE;
							count_written++;
						}

						trace_buffer->offwrite = write_index;
						trace_buffer_file->offp = write_index;
					}
				}
				pcrawl=pcrawl->next;

			}

		}
	}
    return 0;
}


int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	struct exec_context *ctx= get_current_ctx();	
	struct strace_head* st_head= ctx->st_md_base;
	int tracing_mode = st_head->tracing_mode;
	struct trace_buffer_info* trace_buffer= filep->trace_buffer;
	int ret_count=0;
	int i=0;
	while(i<count){
		u32 read_offset= trace_buffer->offread;
		u64 syscall_num= trace_buffer->trace_buffer_mem[read_offset];
		int params= num_arguments(syscall_num);
		params=params+1;
		if(read_offset>=trace_buffer->offwrite){
			return ret_count;
		}
		for(int k=0;k<params;k++){
			ret_count+=trace_buffer_read(filep, buff+ret_count, sizeof(u64));
		}
		i++;

	}
	return ret_count;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if(!current->st_md_base){
		struct strace_head *st_head= (struct strace_head*)os_alloc(sizeof(struct strace_head));
		st_head->count=0;
		st_head->is_traced=1;
		st_head->tracing_mode=tracing_mode;
		st_head->strace_fd=fd;
		current->st_md_base=st_head;
		current->st_md_base->next = NULL;
		current->st_md_base->last = NULL;

		return 0;
	}
	current->st_md_base->is_traced=1;
	current->st_md_base->tracing_mode=tracing_mode;
	current->st_md_base->strace_fd=fd;

	if(!current->st_md_base){
		return -EINVAL;
	}
	return 0;

	
}

int sys_end_strace(struct exec_context *current)
{

	struct strace_info *pcrawl= current->st_md_base->next;
	for(int j=0;j<current->st_md_base->count;j++){

		os_free(pcrawl, sizeof(struct strace_info));
		pcrawl=pcrawl->next;
	}
	current->st_md_base->is_traced=0;
	current->st_md_base->count=0;
	current->st_md_base->tracing_mode=-1;
	current->st_md_base->strace_fd=-1;
	current->st_md_base->next=NULL;
	current->st_md_base->last=NULL;
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{

	if(ctx->ft_md_base==NULL){
		struct ftrace_head* ft_head= (struct ftrace_head*)os_alloc(sizeof(struct ftrace_head));

	}
	struct ftrace_head* ft_head= ctx->ft_md_base;

	struct user_regs registers= ctx->regs;

	if(action==ADD_FTRACE){

		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
			}
			pcrawl=pcrawl->next;
		}

		if(found==1){
			return -EINVAL;
		}
		struct ftrace_info* new_info= (struct ftrace_info*)os_page_alloc(USER_REG);
		new_info->faddr=faddr;
		new_info->num_args=nargs;
		new_info->fd= fd_trace_buffer;
		new_info->capture_backtrace=0;
		new_info->next=NULL;
		if(ft_head->next==NULL){
			ft_head->next=new_info;
		}else{
			ft_head->last->next=new_info;
		}
		ft_head->count+=1;
		ft_head->last=new_info;
	}else if(action==REMOVE_FTRACE){

		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		struct ftrace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}

		if(found==0){
			return -EINVAL;
		}
		if(pcrawl==ft_head->next){
			ft_head->next=ft_head->next->next;
		}else if(pcrawl==ft_head->last){
			ft_head->last=prev;
			prev->next=NULL;
		}else{
			prev->next=pcrawl->next;
		}


		ft_head->count-=1;
		os_page_free(USER_REG, pcrawl);

	}else if(action==ENABLE_FTRACE){
		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		struct ftrace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}


		if(found==0){
			return -EINVAL;
		}

		char* fad = (char*)pcrawl->faddr;
		for(int i=0; i<4; i++){
			if(((u8*)fad)[i]!=INV_OPCODE){
				pcrawl->code_backup[i] = ((u8*)fad)[i];
				((u8*)fad)[i] = INV_OPCODE;
			}
		}


	}else if(action==DISABLE_FTRACE){

		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		struct ftrace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}


		if(found==0){
			return -EINVAL;
		}

		u8* fad = (u8*)pcrawl->faddr;
		for(int i=0; i<4; i++){
			((u8*)fad)[i] = pcrawl->code_backup[i];
			if(pcrawl->code_backup[i]==INV_OPCODE){
				// printk("the backup is wrong\n");
			}
			if(fad[i]==INV_OPCODE){
				// printk("not changed\n");
			}
		}


	}else if(action==ENABLE_BACKTRACE){
		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		struct ftrace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}
		if(found==0){
			return -EINVAL;
		}
		
		char* fad = (char*)pcrawl->faddr;
		for(int i=0; i<4; i++){
			if(((u8*)fad)[i]!=INV_OPCODE){
				pcrawl->code_backup[i] = ((u8*)fad)[i];
				((u8*)fad)[i] = INV_OPCODE;
			}
		}
		pcrawl->capture_backtrace=1;


	}else if(action==DISABLE_BACKTRACE){
		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		struct ftrace_info* prev= pcrawl;
		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			prev=pcrawl;
			pcrawl=pcrawl->next;
		}


		if(found==0){
			return -EINVAL;
		}

		char* fad = (char*)pcrawl->faddr;
		for(int i=0; i<4; i++){
			 ((u8*)fad)[i] = pcrawl->code_backup[i];
		}

		pcrawl->capture_backtrace=0;

	}
    return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
		regs->entry_rsp-=8;
		*((u64*)regs->entry_rsp)=regs->rbp;
		regs->rbp=regs->entry_rsp;
		struct exec_context* ctx = get_current_ctx();
		struct ftrace_head* ft_head= ctx->ft_md_base;
		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		unsigned long faddr= regs->entry_rip;

		while(pcrawl!=NULL && found==0){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			pcrawl=pcrawl->next;
		}


		if(found==0){
			regs->entry_rip+=4;
			return -EINVAL;
		}
		struct file* trace_file=ctx->files[pcrawl->fd];
		struct trace_buffer_info* trace_buffer= trace_file->trace_buffer;
		char* trace_buffer_mem= trace_buffer->trace_buffer_mem;
		u64 parameters[6]={faddr, regs->rdi, regs->rsi,regs->rdx, regs->rcx, regs->r8};
		u32 write_index= trace_buffer->offwrite;

		for(int i=0;i<(pcrawl->num_args+1);i++){
				
				u32 count_written=0;
				((u64*)trace_buffer_mem)[write_index/8]=parameters[i];
				for(int j=write_index;j<write_index+8;j++){
					// j=j%TRACE_BUFFER_MAX_SIZE;
					// if(trace_buffer->usage_arr[write_index]==1){
					// 	return -EINVAL;
					// }else{
						trace_buffer->usage_arr[write_index]==1;
					// }
				}
				write_index=(write_index+8)%TRACE_BUFFER_MAX_SIZE;
				trace_buffer->offwrite=write_index;
				trace_file->offp=write_index;

		}

		regs->entry_rip+=4;

		if(pcrawl->capture_backtrace==1){
			u32 count_written=0;
			((u64*)trace_buffer_mem)[write_index/8]=faddr;
	
		
			for(int j=write_index;j<write_index+8;j++){
					// j=j%TRACE_BUFFER_MAX_SIZE;
					// if(trace_buffer->usage_arr[write_index]==1){
					// 	return -EINVAL;
					// }else{
						trace_buffer->usage_arr[write_index]=1;
					// }
			}
			write_index=(write_index+8)%TRACE_BUFFER_MAX_SIZE;
			trace_buffer->offwrite=write_index;
			trace_file->offp=write_index;
			u64 rbp_crawl=regs->rbp;
			unsigned long return_addr=*((u64*)(rbp_crawl+8));
			while(return_addr!=(unsigned long)END_ADDR){
				u32 count_written=0;
				((u64*)trace_buffer_mem)[write_index/8]=return_addr;
				for(int j=write_index;j<write_index+8;j++){
					// j=j%TRACE_BUFFER_MAX_SIZE;
					// if(trace_buffer->usage_arr[write_index]==1){
					// 	return -EINVAL;
					// }else{
						trace_buffer->usage_arr[write_index]=1;
					// }
				}
				write_index=(write_index+8)%TRACE_BUFFER_MAX_SIZE;
				trace_buffer->offwrite=write_index;
				trace_file->offp=write_index;

				rbp_crawl=*((u64*)(rbp_crawl));
				return_addr=*((u64*)(rbp_crawl+8));
			}

		}

		u64 delimiter=DELIMITER;
		u32 count_written=0;


		((u64*)trace_buffer_mem)[write_index/8]=DELIMITER;
		for(int j=write_index;j<write_index+8;j++){
				// 	j=j%TRACE_BUFFER_MAX_SIZE;
				// if(trace_buffer->usage_arr[write_index]==1){
				// 	return -EINVAL;
				// }else{
					trace_buffer->usage_arr[write_index]=1;
				// }
			}
		write_index=(write_index+8)%TRACE_BUFFER_MAX_SIZE;
		trace_buffer->offwrite=write_index;
		trace_file->offp=write_index;
		

        return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	struct exec_context *ctx= get_current_ctx();	
	struct ftrace_head* ft_head= ctx->ft_md_base;
	struct trace_buffer_info* trace_buffer= filep->trace_buffer;
	int ret_count=0;
	int i=0;

	while(i<count){
		u32 read_index= trace_buffer->offread;
		u64 faddr = ((u64*)trace_buffer->trace_buffer_mem)[read_index/8];
		int found=0;
		struct ftrace_info* pcrawl= ft_head->next;
		for(int j=0;j<ft_head->count;j++){
			if(pcrawl->faddr==faddr){
				found=1;
				break;
			}
			pcrawl=pcrawl->next;
		}
		if(found==0){
			printk("not found\n");
			i++;
			continue;
		}
		
		((u64*)buff)[ret_count]=((u64*)trace_buffer->trace_buffer_mem)[read_index/8];
		for(int j=read_index;j<read_index+8;j++){
					// j=j%TRACE_BUFFER_MAX_SIZE;
				// if(trace_buffer->usage_arr[read_index]==0){
				// 	return -EINVAL;
				// }else{
					trace_buffer->usage_arr[read_index]=0;
				// }
		}
		read_index=(read_index+8)%TRACE_BUFFER_MAX_SIZE;
		ret_count=ret_count+1;
		trace_buffer->offread=read_index;
		filep->offp=read_index;
		while(1){
			if((((u64*)trace_buffer->trace_buffer_mem)[read_index/8])==DELIMITER){
				break;
			}
			((u64*)buff)[ret_count]=(((u64*)trace_buffer->trace_buffer_mem)[read_index/8]);
			for(int j=read_index;j<read_index+8;j++){
				// 	j=j%TRACE_BUFFER_MAX_SIZE;
				// if(trace_buffer->usage_arr[read_index]==0){
				// 	return -EINVAL;
				// }else{
					trace_buffer->usage_arr[read_index]=0;
				// }
			}		
			read_index=(read_index+8)%TRACE_BUFFER_MAX_SIZE;
			ret_count=ret_count+1;
			trace_buffer->offread=read_index;
			filep->offp=read_index;
			
		}
		read_index = (read_index+ 8)%TRACE_BUFFER_MAX_SIZE;
		for(int j=read_index;j<read_index+8;j++){
			// 		j=j%TRACE_BUFFER_MAX_SIZE;
			// if(trace_buffer->usage_arr[read_index]==0){
			// 	return -EINVAL;
			// }else{
				trace_buffer->usage_arr[read_index]=0;
			// }
		}
		trace_buffer->offread=read_index;
		filep->offp=read_index;
		i++;
	}
	
	return ret_count*8;
}


