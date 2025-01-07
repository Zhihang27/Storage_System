#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"

int mount = 0;
int permission = 0;
const uint32_t total_storage = JBOD_NUM_DISKS * JBOD_DISK_SIZE;

//fixed
uint32_t translate(uint32_t command, uint32_t disk_number, uint32_t block_number) {
    uint32_t op = 0;

    op |= (disk_number);
    op |= (block_number << 4);
    op |= (command << 12);

    return op;
}

//fixed
int mdadm_mount(void) {
    if (mount == 1) {
        return -1;
    }
    uint32_t op = translate(JBOD_MOUNT, 0, 0);
    if (jbod_client_operation(op, NULL) == 0) {
        mount = 1;
        return 1;
    } else {
        return -1;
    }
}

//fixed
int mdadm_unmount(void) {
    if (mount == 0) {
        return -1;
    }
    uint32_t op = translate(JBOD_UNMOUNT, 0, 0);
    if (jbod_client_operation(op, NULL) == 0) {
        mount = 0;
        return 1;
    } else {
        return -1;
    }
}

int mdadm_write_permission(void) {
    uint32_t op = translate(JBOD_WRITE_PERMISSION, 0, 0);
    if (jbod_client_operation(op, NULL) == 0) {
        permission = 1;
        return 0;
    } else {
        return -1;
    }
}

int mdadm_revoke_write_permission(void) {
    uint32_t op = translate(JBOD_REVOKE_WRITE_PERMISSION, 0, 0);
    if (jbod_client_operation(op, NULL) == 0) {
        permission = 0;  
        return 0; 
     } else {
        return -1;
    }
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf) {
    // Error checking
    if (start_addr + read_len > total_storage) {
        return -1; 
    }

    if (read_len > 1024) {
        return -2; 
    }

    if (mount != 1) {
        return -3; 
    }

    if (read_len > 0 && read_buf == NULL) {
        return -4;
    }

    //initialize variables 
    uint32_t curr_addr = start_addr;     
    uint32_t bytes_remaining = read_len; 
    uint32_t bytes_read = 0;             

    //loop until all requested bytes have been read
    while (bytes_remaining > 0) {
        //calculate disk and block ids
        uint32_t disk_num = curr_addr / JBOD_DISK_SIZE;
        uint32_t disk_ofs = curr_addr % JBOD_DISK_SIZE;
        uint32_t block_num = disk_ofs / JBOD_BLOCK_SIZE;
        uint32_t block_ofs = disk_ofs % JBOD_BLOCK_SIZE;

        uint8_t block[JBOD_BLOCK_SIZE];
        if (cache_enabled() && cache_lookup(disk_num, block_num, block) == 1) {
           
        } else{
            //seek to the correct disk
            uint32_t op = translate(JBOD_SEEK_TO_DISK, disk_num, 0);
            if (jbod_client_operation(op, NULL) != 0) {
                return -4;
            }

            //seek to the correct block 
            op = translate(JBOD_SEEK_TO_BLOCK, 0, block_num);
            if (jbod_client_operation(op, NULL) != 0) {
                return -4; 
            }

            //read the block into a temporary buffer 
            op = translate(JBOD_READ_BLOCK, 0, 0);
            if (jbod_client_operation(op, block) != 0) {
                return -4; 
            }

            if (cache_enabled()) {
                cache_insert(disk_num, block_num, block);
            }
        }
        //determine how many bytes to copy from the current block
        uint32_t bytes_block = JBOD_BLOCK_SIZE - block_ofs; 
        uint32_t bytes_copy = (bytes_block < bytes_remaining) ? bytes_block : bytes_remaining;

        //copy the data from the block by iterations
        memcpy(read_buf + bytes_read, block + block_ofs, bytes_copy);

        //update counters for the next iteration
        curr_addr += bytes_copy;       
        bytes_read += bytes_copy;      
        bytes_remaining -= bytes_copy; 
    }

    return bytes_read;
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {

    if (start_addr + write_len > total_storage) {
        return -1;  
    }

    if (write_len > 1024) {
        return -2;  
    }

    if (mount != 1) {
        return -3;  
    }

    if (write_len > 0 && write_buf == NULL) {
        return -4;  
    }

    if (permission != 1) {
        return -5;  
    }

    //initialize variables for tracking 
    uint32_t curr_addr = start_addr;
    uint32_t bytes_remaining = write_len;
    uint32_t bytes_written = 0;

    //process each block 
    while (bytes_remaining > 0) {
        uint32_t disk_num = curr_addr / JBOD_DISK_SIZE;
        uint32_t disk_ofs = curr_addr % JBOD_DISK_SIZE;
        uint32_t block_num = disk_ofs / JBOD_BLOCK_SIZE;
        uint32_t block_ofs = disk_ofs % JBOD_BLOCK_SIZE;

        uint8_t block[JBOD_BLOCK_SIZE];
        bool in_cache = false;
        
        if (cache_enabled() && cache_lookup(disk_num, block_num, block) == 1) {
            in_cache = true;
        } else {

            //seek to the target disk
            uint32_t op = translate(JBOD_SEEK_TO_DISK, disk_num, 0);
            if (jbod_client_operation(op, NULL) != 0) {
                return -4;
            }
            //seek to the correct block
            op = translate(JBOD_SEEK_TO_BLOCK, 0, block_num);
            if (jbod_client_operation(op, NULL) != 0) {
                return -4;
            }
            //read the block into a buffer
            op = translate(JBOD_READ_BLOCK, 0, 0);
            if (jbod_client_operation(op, block) != 0) {
                return -4;
            }
        }
        
        //calc bytes to write within block
        uint32_t bytes_block = JBOD_BLOCK_SIZE - block_ofs;
        uint32_t bytes_copy = (bytes_block < bytes_remaining) ? bytes_block : bytes_remaining;

        //copy the data from write_buf into the block buffer to ofs
        memcpy(block + block_ofs, write_buf + bytes_written, bytes_copy);

        if (cache_enabled()) {
            if (in_cache) {
                cache_update(disk_num, block_num, block);
            } else {
                cache_insert(disk_num, block_num, block);
            }
        }
        
        uint32_t op = translate(JBOD_SEEK_TO_DISK, disk_num, 0);
        //seek to the target disk
        op = translate(JBOD_SEEK_TO_DISK, disk_num, 0);
        if (jbod_client_operation(op, NULL) != 0) {
            return -4;
        }

        //seek to the correct block
        op = translate(JBOD_SEEK_TO_BLOCK, 0, block_num);
        if (jbod_client_operation(op, NULL) != 0) {
            return -4;
        }

        //modified block back to the disk
        op = translate(JBOD_WRITE_BLOCK, 0, 0);
        if (jbod_client_operation(op, block) != 0) {
            return -4;
        }

        //update counters 
        curr_addr += bytes_copy;
        bytes_written += bytes_copy;
        bytes_remaining -= bytes_copy;
    }

    return bytes_written;
}