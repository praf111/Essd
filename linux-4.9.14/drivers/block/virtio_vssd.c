/*
 * Virtio VSSD Support
 *
 * Copyright Indian Institute of Technology Bombay. 2017
 * Copyright Bhavesh Singh <bhavesh@cse.iitb.ac.in>
 *
 * This work is licensed under the terms of GNU GPL, version 3.
 * See the COPYING file in the top-level directory.
 */

#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/freezer.h>
#include <linux/cpu.h>

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>

#include <linux/vmalloc.h>
#include <linux/random.h>

//#include <linux/hrtimer.h>
//#include <linux/ktime.h>

#include <linux/proc_fs.h>

#define VIRTIO_ID_VSSD 54
#define SG_SIZE 1024 //128 // TODO: Figure out the optimal size for this. For virtio_blk, this comes from PCI configuration.
#define MINORS 16 // We set it to 16. Allowing partitions for now. // OLD TODO: Again, we want only one minor device number for this device.

#define SSD_BALLOON_UNIT 2046

//#define RESIZE_CHECK_DELAY 200E6L; // In nanoseconds; 200 ms * 10^6.

/* Added by Bhavesh Singh. 2017.06.15. Begin add */
struct proc_dir_entry *blknum_proc_file;
static u32 *blknums;
static u32 idx=0;
static u32 idx_remove=0;
/* Added by Bhavesh Singh. 2017.06.15. End add */

/*
 * struct virtio_vssd_resize_info - represents the resize information passed between host and guest
 * Usage:
 * GUEST-TO-HOST: (1): status < 0, ack = don't care, sector_list = list of sectors guest gives to host
 * 					   the guest gives back the number of sectors asked in HOST-TO-GUEST (2) below
 *		 		  (2): status > 0, ack = 1 , sector_list = don't care
 *		 		  	   the guest acknowledges the number of sectors it has recieved from the host
 *		 		  	   in HOST-TO-GUEST (1) below
 * HOST-TO-GUEST: (1): status > 0, ack = don't care, sector_list = list of sectors host returns to guest
 * 					   the host returns some sectors back to the guest which then acknowledges them
 * 			   	  (2): status < 0, ack = 1, sector_list = don't care
 * 			   	  	   the host acknowledges the number of sectors it has received from the guest
 */
struct virtio_vssd_resize_info {
	s32 status;
	s32 ack;
	//u64 *sector_list;
	s32 number_of_return_sectors;
	u64 sector_list[SSD_BALLOON_UNIT]; // Just less than a page length as there is a status field too here.

};

struct virtio_vssd {
	struct virtio_device *vdev;
	struct virtqueue *vq, *ctrl_vq;

	spinlock_t q_lock/*, list_lock*/;

	struct request_queue *queue;
	struct gendisk *gdisk;

	struct scatterlist sglist[SG_SIZE]/*, sg_resize_info*/;

	sector_t capacity;
	sector_t available;
	u32 *block_list; // bitmap of valid sector numbers
};

struct vssd_hdr {
	u32 type;
	u64 sector;
};

struct virtio_vssd_request {
//	struct bio *bio;
	struct request *request;
	struct vssd_hdr hdr;
	s32 status;
};

struct virtio_vssd_config {
	__le32 command; // this is little endian for some reason
	__le64 capacity;
};

static int virtio_vssd_major = 0;
//static struct resize_check_timer resize_timer;

/* Added by Bhavesh Singh. 2017.06.06. Begin add */
/* WARNING: Remove the #pragma GCC just below this function too if removing this */
#pragma GCC push_options
#pragma GCC optimize ("-O0")
/* Added by Bhavesh Singh. 2017.06.06. End add */
static void virtio_vssd_free_blocks(struct virtio_vssd *vssd, struct virtio_vssd_resize_info *resize_info) {
	printk(KERN_ALERT"Request from host to free blocks\n");
	u32 i, j=0, total_sectors = vssd->capacity < abs(resize_info->status) ? vssd->capacity : abs(resize_info->status);
	u32 *list = vssd->block_list;
	u64 blk_num, sector_num;
	int count=0;
	printk("Vssd capacity is  %d\n",vssd->capacity);
	printk("No of sectors to be freed %d \n",total_sectors);
	for(sector_num=0;sector_num<vssd->capacity && j<total_sectors && j<SSD_BALLOON_UNIT ;sector_num++)
	{
		spin_lock_irq(&vssd->q_lock);
		//printk(KERN_ALERT"virtio_vssd: checking status of %d\n",sector_num);
		if((list[sector_num/32] & (1 << (sector_num % 32))) == 0){
			//printk(KERN_ALERT"virtio_vssd: Freeing sector : %d\n",sector_num);
			list[sector_num/32] |= (1 << (sector_num % 32));
			resize_info->sector_list[j++] = sector_num;	
			count++;
		}
		
		spin_unlock_irq(&vssd->q_lock);
	}


	resize_info->number_of_return_sectors = j;

	printk("No of sectors freed are %d\t number_of_return_sectors %d\t and j %d \n",count,resize_info->number_of_return_sectors,j);
	/* Added by Bhavesh Singh. 2017.06.16. Begin add */
/*	
	for(;j<total_sectors;) {
		get_random_bytes_arch(&sector_num, 8); // evict blocks randomly
		sector_num = sector_num % vssd->capacity;
		spin_lock_irq(&vssd->q_lock);
		if((list[sector_num/32] & (1 << (sector_num % 32))) == 0) {
			list[sector_num/32] |= (1 << (sector_num % 32));
			resize_info->sector_list[j++] = sector_num;
		}
		spin_unlock_irq(&vssd->q_lock);
	}
*/

	/* Added by Bhavesh Singh. 2017.06.16. End add */

	/* Commented by Bhavesh Singh. 2017.06.16. Begin comment */
/*	for(;j<total_sectors && idx_remove < idx;) {
		blk_num = blknums[idx_remove++];
		for(i=0; i<8; i++) {
			sector_num = blk_num * 8 + i;
			spin_lock_irq(&vssd->q_lock);
			if((list[sector_num/32] & (1 << (sector_num % 32))) == 0) {
				list[sector_num/32] |= (1 << (sector_num % 32));
				resize_info->sector_list[j++] = sector_num;
			}
			spin_unlock_irq(&vssd->q_lock);
		}
	}

*/
	
/*	for(j=0;j<total_sectors;j++) {
//		get_random_bytes_arch(&sector_num, 8); // evict blocks randomly
		for (sector_num= 0; sector_num < vssd->capacity; sector_num++)
		{
			spin_lock_irq(&vssd->q_lock);

			if((list[sector_num/32] & (1 << (sector_num % 32))) == 0) {
				list[sector_num/32] |= (1 << (sector_num % 32));
				resize_info->sector_list[j++] = sector_num;
			}
			spin_unlock_irq(&vssd->q_lock);
		}
			
	}
*/



	/* Commented by Bhavesh Singh. 2017.06.16. End comment */
}

static void virtio_vssd_map_blocks(struct virtio_vssd *vssd, struct virtio_vssd_resize_info *resize_info) {
	printk(KERN_ALERT"Inside map blocks function\n");
	s32 i =0, total_sectors = resize_info->status - resize_info->ack;
	u64 sector_num;
	u32 *list = vssd->block_list;
	for(;i<abs(total_sectors);i++) {
		sector_num = resize_info->sector_list[i];
		spin_lock_irq(&vssd->q_lock);
		list[sector_num/32] &= ~(1 << (sector_num % 32));
		spin_unlock_irq(&vssd->q_lock);
	}
	resize_info->ack += total_sectors;
}
/* Added by Bhavesh Singh. 2017.06.06. Begin add */
/* WARNING: Remove the #pragma GCC just above this function too if removing this */
#pragma GCC pop_options
/* Added by Bhavesh Singh. 2017.06.06. End add */

static void virtio_vssd_resize_query(struct virtio_vssd *vssd, s32 status, s32 ack) {

	printk(KERN_ALERT"Resize query Received\n");
	struct scatterlist *sglist[2], res_info;
	struct virtio_vssd_resize_info *resize_info;
	int out = 0, in = 0, error;

	resize_info = kmalloc(sizeof(*resize_info), GFP_ATOMIC);
	resize_info->status = status;
	resize_info->ack = ack;
	resize_info->number_of_return_sectors=0;

	
	sg_init_one(&res_info, resize_info, sizeof(*resize_info));
	sglist[in++] = &res_info;


	printk("Status = %d \n", status);
	if(status < 0) {
		virtio_vssd_free_blocks(vssd, resize_info);
	}



	error = virtqueue_add_sgs(vssd->ctrl_vq, sglist, out, in, resize_info, GFP_ATOMIC);
	if (error == 0) {
		virtqueue_kick(vssd->ctrl_vq);
	}
}

static void virtio_vssd_request_completed(struct virtqueue *vq) {
	struct virtio_vssd_request *vssdreq;
	struct virtio_vssd *vssd = vq->vdev->priv;
	unsigned long flags;
	unsigned int len;
	bool request_done = false;

//	printk(KERN_ALERT "virtio_vssd: Callback called!\n");

	spin_lock_irqsave(&vssd->q_lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vssdreq = virtqueue_get_buf(vq, &len)) != NULL) {
			//printk(KERN_ALERT "virtio_vssd: Response popped! Sector: %llu\tType: %u\tStatus: %d\n", vssdreq->hdr.sector, vssdreq->hdr.type, vssdreq->status);
//			blk_complete_request(vssdreq->request);
			__blk_end_request_all(vssdreq->request, vssdreq->status); // Add these requests to a list and end them outside the spinlock of the vssd.
			//printk(KERN_ALERT "virtio_vssd: Request end returned successfully\n");
			request_done = true;
			kfree(vssdreq);
		}
		if (unlikely(virtqueue_is_broken(vq))) {
			printk(KERN_ALERT "virtio_vssd: virtqueue is broken\n");
			break;
		}
	} while (!virtqueue_enable_cb(vq));

	if(request_done && blk_queue_stopped(vssd->gdisk->queue)) {
		blk_start_queue(vssd->gdisk->queue);
	}
	spin_unlock_irqrestore(&vssd->q_lock, flags);
	return;
}

static void virtio_vssd_resize_callback(struct virtqueue *vq) {
	unsigned int len;
	struct virtio_vssd *vssd = vq->vdev->priv;
	struct virtio_vssd_resize_info *resize_info;
//	bool return_blocks = false;
	int status, ack;

	printk(KERN_ALERT "virtio_vssd: Resize callback called\n");
	//printk(KERN_ALERT "virtio_vssd: Resize response: Status: %d\n",);
	do {
		virtqueue_disable_cb(vq);
		while ((resize_info = virtqueue_get_buf(vq, &len)) != NULL) {
			printk(KERN_ALERT "virtio_vssd: Resize response popped! Status: %d, Ack: %d No of elements returned %d\n", resize_info->status, resize_info->ack,resize_info->number_of_return_sectors);
			status = resize_info->status;
			if(status < 0) {
				// The call goes from the handler for config change. So this is just the ack from the backend.
				ack = resize_info->ack;
				if(abs(status-ack) != 0) {
					printk("zero\n");
					virtio_vssd_map_blocks(vssd, resize_info);
				}
			} else if(status > 0) {
				// We need to process the blocks that the backend has given. And ack the same.
				virtio_vssd_map_blocks(vssd, resize_info);
				virtio_vssd_resize_query(vssd, resize_info->status, resize_info->ack);
			}
			kfree(resize_info);
		}
		if (unlikely(virtqueue_is_broken(vq))) {
			printk(KERN_ALERT "virtio_vssd: virtqueue is broken\n");
			break;
		}
	} while (!virtqueue_enable_cb(vq));

//	if(return_blocks)
//		virtio_vssd_resize_query(status);
}

static int init_virtqueues(struct virtio_vssd *vssd) {
	struct virtqueue *vqs[1];

	vq_callback_t *callbacks[] = { virtio_vssd_request_completed, virtio_vssd_resize_callback };
	const char *names[] = { "virtio_vssd_request_completed", "virtio_vssd_resize_callback" };

	int err;
	int nvqs = 2;

	err = vssd->vdev->config->find_vqs(vssd->vdev, nvqs, vqs, callbacks, names);
	if (err) {
		return err;
	}

	vssd->vq = vqs[0];
	vssd->ctrl_vq = vqs[1];

//	sg_init_one(&vssd->sg_resize_info, &vssd->resize_info, sizeof(struct virtio_vssd_resize_info));
//	if (virtqueue_add_inbuf(vssd->ctrl_vq, &vssd->sg_resize_info, 1, vssd, GFP_KERNEL) < 0)
//		BUG();
//	virtqueue_kick(vssd->ctrl_vq);

	return 0;
}

/* Added by Bhavesh Singh. 2017.06.19. Begin add */
/* WARNING: Remove the #pragma GCC just below this function too if removing this */
#pragma GCC push_options
#pragma GCC optimize ("-O0")
/* Added by Bhavesh Singh. 2017.06.19. End add */
static bool virtio_vssd_request_valid(const struct request *req, const struct virtio_vssd *vssd) {
	struct bio *bio;
	struct bio_vec bvec;
	struct bvec_iter iter;
	u64 sector_num;
	u32 *list = vssd->block_list;
	//bool invalid_block = false;
	bio = req->bio;
	for_each_bio(bio) {
		bio_for_each_segment(bvec, bio, iter) {
//			printk(KERN_ALERT "virtio_vssd: Sector number: %lu\n", iter.bi_sector);
			//printk(KERN_ALERT "virtio_vssd: BIO Flags: %u\n", bio_flags(bio) & BIO_NULL_MAPPED);
			sector_num = iter.bi_sector;
			if((list[sector_num/32] & (1 << (sector_num % 32))) == (1 << (sector_num % 32))) {
				//printk(KERN_ALERT "virtio_vssd: BIO Flags: %d\n", bio_flags(bio) | ~BIO_SEG_VALID);
				//invalid_block = true;
				//printk(KERN_ALERT "virtio_vssd: Sector number: %lu\n", iter.bi_sector);
				blk_start_request(req); // This is necessary, otherwise it BUGs on blk_queued_req(req)
				__blk_end_request_all(req, -EIO);
				//bio->bi_error = -EIO;
				return false;
			}
		}
	}
	return true;
}
/* Added by Bhavesh Singh. 2017.06.19. Begin add */
/* WARNING: Remove the #pragma GCC just above this function too if removing this */
#pragma GCC pop_options
/* Added by Bhavesh Singh. 2017.06.19. End add */

static void virtio_vssd_request(struct request_queue *q) {
	struct request *req;
	struct virtio_vssd *vssd = q->queuedata;
	struct virtio_vssd_request *vssdreq;
	struct scatterlist *sglist[3];
	struct scatterlist hdr, status;
	unsigned int num = 0, out = 0, in = 0;
	int error;

  //  printk(KERN_ALERT "virtio_vssd: Request fn called!\n");

   if(unlikely((req = blk_peek_request(q)) == NULL))
	    goto no_out;

   if(req->cmd_type != REQ_TYPE_FS)
   		 goto no_out;

   if(!virtio_vssd_request_valid(req, vssd))
	   goto no_out;

	vssdreq = kmalloc(sizeof(*vssdreq), GFP_ATOMIC);
	if (unlikely(!vssdreq)) {
		goto no_out;
	}

	vssd = req->rq_disk->private_data;
	vssdreq->request = req;

	num = blk_rq_map_sg(q, req, vssd->sglist);
	//printk(KERN_ALERT "virtio_vssd: Request mapped to sglist. Count: %u\n", num);

	if(unlikely(!num))
		goto free_vssdreq;

	vssdreq->hdr.sector = cpu_to_virtio32(vssd->vdev, blk_rq_pos(req));
	vssdreq->hdr.type = cpu_to_virtio32(vssd->vdev, rq_data_dir(req));

	sg_init_one(&hdr, &vssdreq->hdr, sizeof(vssdreq->hdr));
	sglist[out++] = &hdr;

	if (rq_data_dir(req) == WRITE) {
		sglist[out++] = vssd->sglist;
	} else {
		sglist[out + in++] = vssd->sglist;
	}

	sg_init_one(&status, &vssdreq->status, sizeof(vssdreq->status));
	sglist[out + in++] = &status;

//	printk(KERN_ALERT "virtio_vssd: Sector: %llu\tDirection: %u\n", vssdreq->hdr.sector, vssdreq->hdr.type);
	error = virtqueue_add_sgs(vssd->vq, sglist, out, in, vssdreq, GFP_ATOMIC);
	if (error < 0) {
		//printk(KERN_ALERT "virtio_vssd: Error adding scatterlist to virtqueue. Stopping request queue\n");
		blk_stop_queue(q);
		goto free_vssdreq;
	}
	blk_start_request(req);
	virtqueue_kick(vssd->vq);

no_out:
	return;

free_vssdreq:
	kfree(vssdreq);
}

//static int virtio_vssd_getgeo(struct block_device *bd, struct hd_geometry *geo)
//{
//	/* some standard values, similar to sd */
//	geo->heads = (1 << 6);
//	geo->sectors = (1 << 5);
//	geo->cylinders = get_capacity(bd->bd_disk) >> 11;
//	return 0;
//}

static struct block_device_operations virtio_vssd_ops = {
	.owner           = THIS_MODULE,
//	.open 	         = virtio_vssd_open,
//	.release 	 	 = virtio_vssd_release,
//	.media_changed   = virtio_vssd_media_changed,
//	.revalidate_disk = virtio_vssd_revalidate,
//	.ioctl	         = virtio_vssd_ioctl,
//	.getgeo 		 = virtio_vssd_getgeo,
};

static int virtio_vssd_probe(struct virtio_device *vdev) {
	struct virtio_vssd *vssd;
	int err;
	__le64 capacity;
	__le64 available;

	vssd = kzalloc(sizeof(*vssd), GFP_KERNEL);
	if (!vssd) {
		err = -ENOMEM;
		goto out;
	}

	vssd->vdev = vdev;

	// TODO: We need to figure out capacity too and tell it to the kernel. Read it off the PCI configuration.
	//vssd->capacity = 2097152; // =2^21 (# of 512 byte sectors; therefore 2^30 bytes or 1GB)
	virtio_cread(vdev, struct virtio_vssd_config, capacity, &capacity);
	vssd->capacity = __le64_to_cpu(capacity);
	vssd->available = __le64_to_cpu(available);

//        vssd->capacity = 20971520;

	vssd->block_list = vzalloc(sizeof(u32) * vssd->capacity/32); // We divide by 32 as our array is of unsigned 32 bit integers.
	 
	err = init_virtqueues(vssd);
	if (err) {
		goto out_free_vssd;
	}

	spin_lock_init(&vssd->q_lock);
//	spin_lock_init(&vssd->list_lock);

	vssd->queue = blk_init_queue(virtio_vssd_request, &vssd->q_lock);
	if(vssd->queue == NULL) {
		err = -ENOMEM;
		goto out_free_vssd;
	}

	vssd->queue->queuedata = vssd;
	vdev->priv = vssd;

	vssd->gdisk = alloc_disk(MINORS);
		if (!vssd->gdisk) {
			//printk (KERN_ALERT "virtio_vssd: Call to alloc_disk failed\n");
			goto out_free_vssd;
		}
	vssd->gdisk->major = virtio_vssd_major;
	vssd->gdisk->first_minor = MINORS;
	vssd->gdisk->fops = &virtio_vssd_ops;
	vssd->gdisk->queue = vssd->queue;
	vssd->gdisk->private_data = vssd;
	snprintf (vssd->gdisk->disk_name, 32, "vssda"); // TODO: A crude hard-coding, since we are creating only one device of this kind.

	set_capacity(vssd->gdisk, vssd->capacity);
	add_disk(vssd->gdisk);

	sg_init_table(vssd->sglist, SG_SIZE);

	printk(KERN_ALERT "virtio_vssd: Device initialized\n");
	return 0;

out_free_vssd:
	vssd->vdev->config->del_vqs(vssd->vdev);
	kfree(vssd);

out:
	return err;
}

static void virtio_vssd_remove(struct virtio_device *vdev) {
	struct virtio_vssd *vssd;
	vssd = vdev->priv;
	vssd->vdev->config->reset(vssd->vdev);
	vssd->vdev->config->del_vqs(vssd->vdev);

	del_gendisk(vssd->gdisk);
	blk_cleanup_queue(vssd->gdisk->queue);

	kfree(vssd);

	printk(KERN_ALERT "virtio_vssd: Device removed\n");
}

static void virtio_vssd_conf_changed(struct virtio_device *vdev) {
	struct virtio_vssd *vssd = vdev->priv;
	__le32 command;

	virtio_cread(vdev, struct virtio_vssd_config, command, &command);
	//printk(KERN_ALERT "virtio_vssd: Config change detected! Command: %d\n", command);

	virtio_vssd_resize_query(vssd, __le32_to_cpu(command), -1); // ack = -1 for new requests.
}

static unsigned int features[] = { 0 };

static struct virtio_device_id id_table[] = {
		{ VIRTIO_ID_VSSD, VIRTIO_DEV_ANY_ID },
		{ 0 },
};

static struct virtio_driver virtio_vssd_driver = {
		.feature_table = features,
		.feature_table_size = ARRAY_SIZE(features),
		.driver.name = KBUILD_MODNAME,
		.driver.owner = THIS_MODULE,
		.id_table = id_table,
		.probe = virtio_vssd_probe,
		.remove = virtio_vssd_remove,
		.config_changed = virtio_vssd_conf_changed,
};

/* Added by Bhavesh Singh. 2017.06.15. Begin add */

ssize_t write_blknum_proc(struct file *filp, const char __user *buf, size_t count, loff_t *offp) {
	char num[256];
	unsigned int number;
	strncpy_from_user(num, buf, count);
	if(unlikely(kstrtouint(num, 10, &number) < 0))
		goto out;
	//printk(KERN_ALERT "virtio_vssd: Write called: %u\n", number);
	blknums[idx++] = number;
out:
	return count;
}

static struct file_operations fops_blknum = {
  .owner   = THIS_MODULE,
  .write   = write_blknum_proc,
};
/* Added by Bhavesh Singh. 2017.06.15. End add */


static __init int virtio_vssd_driver_init(void) {
	int error;

	virtio_vssd_major = register_blkdev(virtio_vssd_major, "virtio_vssd");
	if(virtio_vssd_major <= 0) {
		printk(KERN_ALERT "virtio_vssd: Unable to get major device number\n");
		error = -EBUSY;
		goto out;
	}

	error = register_virtio_driver(&virtio_vssd_driver);
	if(error) {
		unregister_blkdev(virtio_vssd_major, "virtio_vssd");
	}

	/* Added by Bhavesh Singh. 2017.06.15. Begin add */
	blknums = vzalloc(sizeof(u32) * 65536);
	proc_create("vssd_balloon_blknum", S_IWUSR|S_IWGRP|S_IWOTH, NULL, &fops_blknum);
	/* Added by Bhavesh Singh. 2017.06.15. End add */

out:
	return error;
}

static __exit void virtio_vssd_driver_exit(void) {
	unregister_virtio_driver(&virtio_vssd_driver);
	unregister_blkdev(virtio_vssd_major, "virtio_vssd");
	/* Added by Bhavesh Singh. 2017.06.15. Begin add */
	remove_proc_entry("vssd_balloon_blknum", NULL);
	kfree(blknums);
	/* Added by Bhavesh Singh. 2017.06.15. End add */
}

module_init(virtio_vssd_driver_init);
module_exit(virtio_vssd_driver_exit);

//module_virtio_driver(virtio_vssd_driver);
MODULE_DEVICE_TABLE(virtio, id_table);

MODULE_AUTHOR("Bhavesh Singh <bhavesh@cse.iitb.ac.in>");
MODULE_DESCRIPTION("VirtIO vSSD driver");
MODULE_LICENSE("GPL");
