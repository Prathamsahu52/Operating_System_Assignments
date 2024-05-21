#include <stdio.h>
#include <stdlib.h>
#include<sys/mman.h>
#include<unistd.h>
#include<string.h>

#define _4MB (4096*1024)

void *head=NULL;

void *memalloc(unsigned long size) 
{	
	
	printf("memalloc() called\n");
	

	if(size==0){
		return NULL;
	}
	// change size to 8 aligned
	unsigned long mod=size%8;

	if(mod==0){
		size=size;
	}else{
		size=8*((size/8)+1);
	}
	
	if(size<16){
		size=16;
	}

	//crawl through the linked list until you find a free chunk of memory that can be allotted	
	void *pcrawl=head;
	void *prev=NULL;
	while(pcrawl!=NULL){
		if(*(unsigned long*)pcrawl<size+8){
			prev=pcrawl;
			pcrawl= *((void**)pcrawl+8);
		}else{
			if(*(unsigned long*)pcrawl==size+8){
				if(prev==NULL){
					head= *((void**)pcrawl+8);
					return pcrawl+8;
				}else{
					void* temp= *((void**)pcrawl+8);
					*((void**)prev+8)=temp;
					*((void**)temp+16)=prev;
					return pcrawl+8;
				}
			}else if(*(unsigned long*)pcrawl-size-8<24){
				if(prev==NULL){
					head= *((void**)pcrawl+8);
					return pcrawl+8;
				}else{
					void* temp= *((void**)pcrawl+8);
					*((void**)prev+8)=temp;
					*((void**)temp+16)=prev;
					return pcrawl+8;
				}
			}else{
				void* temp=pcrawl;
				void* temp1= *((void**)pcrawl+16);
				void* temp2= *((void**)pcrawl+8);

				if(head==pcrawl){
					head= pcrawl+size+8;
				}

				pcrawl=pcrawl+size+8;
				*((unsigned long*)pcrawl)=*(unsigned long*)temp-size-8;
				*((void**)pcrawl+16)=temp1;
				*((void**)pcrawl+8)=temp2;


				*((unsigned long*)temp)=size+8;
				return temp+8;
			}
		}
		
	}
	// if no free chunk is found, mmap a new chunk of memory and add it to the linked list
	
	size_t len;

	if(size%_4MB==0){
		len= size;
	}else{
		len= _4MB*(size/_4MB + 1);
	}
	len=len+8;
	
	char* ptr = mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,0,0);
	
	if(ptr==MAP_FAILED){
		perror("Unable to execute");
		return NULL;
	}

	if(prev==NULL){
		head=ptr;
		*((unsigned long*)ptr)=len;
		*((void**)ptr+8)=NULL;
		*((void**)ptr+16)=NULL;
	}else{
		
		*((unsigned long*)ptr)=len;
		*((void**)ptr+16)=prev;
		*((void**)ptr+8)=NULL;
	}
	

	void* temp=ptr;
	void* temp1= *((void**)ptr+16);
	

	if(len-size-8==0){
		
		

		if(temp1==NULL){
			head=NULL;
			
		}else{
			*((void**)temp1+8)=ptr;
		}

		*((unsigned long*)temp)=size+8;
		
		return temp+8;
	}
	ptr=ptr+size+8;
	*((unsigned long*)ptr)=len-size-8;

	*((void**)ptr+16)=temp1;
	*((void**)ptr+8)=NULL;
	


	*((unsigned long*)temp)=size+8;
	if(temp1==NULL){
		head=ptr;
	}else{
		*((void**)temp1+8)=ptr;
	}
	return temp+8;

}

int memfree(void *ptr)
{
	printf("memfree() called\n");
	if(ptr==NULL){
		perror("Unable to execute");
		return -1;
	}
	unsigned long size = *((unsigned long *)(ptr - 8));
	void *pcrawl=head;
	void *left_chunk = NULL;
    void *right_chunk = NULL;

	unsigned long left_size= 0;
	unsigned long right_size = 0;
	//FIND LEFT CHUNK
	while(pcrawl!=NULL){
		
		unsigned long size1= *((unsigned long*)pcrawl);		
		if(pcrawl+size1==ptr-8){
			left_chunk=pcrawl;
			left_size=size1;
			// printf("left chunk found\n");
			break;
		}else{
			pcrawl= *((void**)(pcrawl+8));
			
		}
		
	}
	
	pcrawl=head;
	//FIND RIGHT CHUNK
	while(pcrawl!=NULL){
		unsigned long size1= *((unsigned long*)pcrawl);

		if(pcrawl==(ptr+size-8)){
			right_chunk=pcrawl;
			right_size=size1;
			// printf("right chunk found\n");
			break;
		}else{
			pcrawl= *((void**)(pcrawl+8));
		}
	}
	
	//MERGE CHUNKS IF POSSIBLE
	if(left_chunk==NULL && right_chunk==NULL){
		*((void**)ptr)=head;		
		*((void**)(ptr+8))=NULL;
		
		if(head!=NULL){
			*((void**)(head+16))=ptr-8;
		}
		head=(ptr-8);

	}else if(left_chunk==NULL && right_chunk!=NULL){
		*((unsigned long*)(ptr-8)) = size+right_size;
		*((void**)ptr)=head;
		*((void**)(ptr+8))=NULL;
		*((void**)(head+16))=ptr-8;

		void* prev= *((void**)(right_chunk+16));
		void* next= *((void**)(right_chunk+8));


		if(prev!=NULL){
			*((void**)(prev+8))=next;

		}
		
		if(next!=NULL){
			*((void**)(next+16))=prev;
		}
		head=ptr-8;
	}else if(left_chunk!=NULL && right_chunk==NULL){
		

		void* prev= *((void**)(left_chunk+16));
		void* next= *((void**)(left_chunk+8));
		
		*(unsigned long*)left_chunk=size+left_size;
		*((void**)(left_chunk+8))=head;
		*((void**)(left_chunk+16))=NULL;
		*((void**)(head+16))=left_chunk;
		if(prev!=NULL){
			*((void**)(prev+8))=next;
		}
		if(next!=NULL){
			*((void**)(next+16))=prev;
		}
				
		head=left_chunk;
	}else{

		void* prev= *((void**)(left_chunk+16));
		void* next= *((void**)(left_chunk+8));
		*(unsigned long*)left_chunk=size+left_size;
		*((void**)(left_chunk+8))=head;
		*((void**)(left_chunk+16))=NULL;
		*((void**)(head+16))=left_chunk;
		head=left_chunk;


		if(prev!=NULL){
			*((void**)(prev+8))=next;
		}		
		if(next!=NULL){
			*((void**)(next+16))=prev;
		}

		ptr=left_chunk+8;

		*((unsigned long*)(ptr-8)) = size+left_size+right_size;
		prev= *((void**)(right_chunk+16));
		next= *((void**)(right_chunk+8));


		if(prev!=NULL){
			*((void**)(prev+8))=next;

		}	
		if(next!=NULL){
			*((void**)(next+16))=prev;
		}
		head=ptr-8;

	}
	
	return 0;
        
}
