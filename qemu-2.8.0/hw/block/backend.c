#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <math.h>
#define SIZE 20971520
#define size1 1000000
#define VM_REGISTER 1
#define VM1 1

void register_vm(struct Memory *ShmPTR,int *block_list,VirtIOVssd *vssd){
	int count,position=0,i,consumed=0,start,end;
	double round;
	pthread_mutex_lock(&(ShmPTR->lock));
	printf("Lock aquired..\n");
	printf("shared memory is ready..\n");
	ShmPTR->msg_type  = VM_REGISTER;//msg type 1 corresponds to registration
	ShmPTR->id = VM1;
	ShmPTR->size = 20971520; //ask for 5GB ...i.e 10485760 number of 512 byte sectors
	ShmPTR->vm_done=1;
	while(!ShmPTR->free);
	vssd->available	= ShmPTR->free;
	round = (double) ShmPTR->free/size1;
	count = ceil(round);
	printf("Data written to shared memory\n"); 
	while(count--){
		start = position;
		while(ShmPTR->status != FILLED);	

		if(count==0){
			for(i=0;i<(ShmPTR->free)-consumed;i++){
				block_list[position++] = ShmPTR->ptr[i];
			}	
			goto X;	
		}	
		for(i=0;i<size1;i++){
			block_list[position++] = ShmPTR->ptr[i];    
		}
		consumed += size1;

X:ShmPTR->status = TAKEN;
  printf("Round %d consumed\n",count+1);
  end = position;
	}
	printf("VM Registered.....\n");
}
// We tell the guest which blocks to map again...giving blocks to guest
void virtio_vssd_map_blocks(struct VirtIOVssd *vssd, struct VirtIOVssdResizeInfo *resize_info,struct Memory *ShmPTR,int *block_list) {
	//printf("virtio_vssd_backend: Inside map blocks function\n");
	uint32_t i=0, j=0,k=0, total_sectors = resize_info->status;
	uint32_t *list = vssd->block_list;
	int count,consumed=0;
	double round;
	pthread_mutex_lock(&(ShmPTR->lock));
	ShmPTR->id = VM1;
	ShmPTR->msg_type = 2;
	ShmPTR->size_alloc = total_sectors;
	ShmPTR->vm_done = 1;
	round = (double)ShmPTR->size_alloc/size1;
	count = ceil(round);
	printf("Data written to shared memory\n"); 
	while(count--){
		while(ShmPTR->status != FILLED);	

		if(count==0){
			for(i=0;i<ShmPTR->size_alloc;i++){
				if(block_list[j]==-1)
					printf("Invalid mapping found...\n");
				block_list[j++] = ShmPTR->ptr[i];
				resize_info->sector_list[k++]= j-1;
			}	
			goto X;	
		}	
		for(i=0;i<ShmPTR->size_alloc;i++){
			if(block_list[j]==-1){      	      		
				block_list[j++] = ShmPTR->ptr[i];
				resize_info->sector_list[k++]= j-1;   
			} 
		}
		consumed += size1;

X:ShmPTR->status = TAKEN;
  printf("Round %d consumed\n",count+1);
	}
}
void virtio_vssd_free_blocks(struct VirtIOVssd *vssd, struct VirtIOVssdResizeInfo *resize_info,struct Memory *ShmPTR,int *block_list) {
	// printf("virtio_vssd_backend: Inside vssd free blocks function\n");	
	uint32_t i=0,j=0,total_sectors = abs(resize_info->status);
	uint64_t log_sector;
	uint32_t *list = vssd->block_list;
	int count,consumed=0;
	double round;
	//printf("Virtio_vssd_backend: Sectors freed by the guest = %d\n",total_sectors);
	/*    for(;i<abs(total_sectors);i++) {
	      sector_num = resize_info->sector_list[i];
	//printf("virtio_vssd_backend: count = %6d. logical block : %"PRIu64" \n",i,sector_num);
	list[sector_num/32] |= (1 << (sector_num % 32));//setting the bit
	}
	 */   	
	//printf("%u\n", virtio_vssd_get_balloon_size(vssd));
	/*     for(;i<abs(total_sectors);i++){
	       log_sector = resize_info->sector_list[i];
	       printf("sector received is--->%d\n",log_sector);	
	       }
	 */
	pthread_mutex_lock(&(ShmPTR->lock));
	printf("Lock acquired...\n");
	ShmPTR->id = 1;
	ShmPTR->flag = 0;
	ShmPTR->msg_type = 3;
	ShmPTR->size_dealloc = total_sectors;
	ShmPTR->vm_done = 1;\
printf("No of sectors to be removed are %d. no-of-blocks-retuned : %d\n",total_sectors, resize_info->number_of_return_sectors);
	round = (double)ShmPTR->size_dealloc/size1;
	printf("value of round is %d\n",round);
	count = ceil(round);
	ShmPTR->status = READY;
	//     printf("No of rounds required are %d\n",count);
	//communicate the list of sectors that guest has marked for deallocation...
	while(count--){
		while(ShmPTR->status!=READY);

  printf("Starting of round...\n");


		//          printf("Round =%d\n",count);         
		if(count==0){
		 	printf("Logical Sectors freed:.....................................\n");
			for(j=0;j<total_sectors;j++){	
				log_sector = resize_info->sector_list[j];
				printf("%d\t",log_sector);
				if(log_sector<0 ||log_sector>20971520){
					printf("\nInvalid sector returned...\n");  
				}    
				ShmPTR->ptr[i++] = block_list[log_sector];
				block_list[log_sector] = -1; 	
			}	


			ShmPTR->status = NOT_READY;
			printf("\n");

			goto X;	
		}	
		for(j=0;j<total_sectors;j++){
			log_sector = resize_info->sector_list[j++];
			//  if(log_sector>20971520 || log_sector <0)
			//             log_sector = 0;  	
			ShmPTR->ptr[i++] = block_list[log_sector];
			block_list[log_sector] = -1;
		}
		consumed += total_sectors; 
		ShmPTR->status = NOT_READY;    
	}
X: while(ShmPTR->status!=FILLED);
   printf("flag checked...!!\n");
   ShmPTR->status = TAKEN; 
   printf("status set to taken...\n");   
   resize_info->ack = resize_info->status;

}

uint32_t virtio_vssd_get_balloon_size(VirtIOVssd *vssd) {
	uint32_t i=0, total_sectors = 0;
	uint32_t *list = vssd->block_list;
	for(; i<vssd->capacity; i++) {
		if((list[i/32] & (1 << (i%32))) == (1 << (i%32))) {
			total_sectors++;
		}
	}
	return total_sectors;
}
int virtio_get_ssd_size(int *block_list){
	int count=0,i;
	for(i=0;i<SIZE;i++){
		if(block_list[i] != -1)
			count++;
	}
	return count;
}


