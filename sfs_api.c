#include "sfs_api.h"
#include "bitmap.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <strings.h>
#include <inttypes.h>
#include "disk_emu.h"
#define MONFOURNYDAIGNEAULT_SANDRINE_DISK "sfs_monfournydaigneault_sandrine.disk"
#define NUM_BLOCKS 1024  //maximum number of data blocks on the disk.
#define BLOCK_SIZE 1024
#define BITMAP_ROW_SIZE (NUM_BLOCKS/8) // this essentially mimcs the number of rows we have in the bitmap. we will have 128 rows.
#define NUM_INODES 100 
#define ROOTDIR_INODE 0
#define MAGIC 0xABCD0005
#define MAX_FILE_SIZE BLOCK_SIZE * ((BLOCK_SIZE / sizeof(int)) + 12) 

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

// Allows us to keep track of occupied inodes. 1 = occupied, 0 = not
int inTableStatus[NUM_INODES]; 

inode_t inTable[NUM_INODES];
file_descriptor fdTable[NUM_INODES];
directory_entry rootDir[NUM_INODES];

int locus; // Keeps track of our location in the directory, for sfs_getnextfilename

superblock_t super;

int indy_block[BLOCK_SIZE/sizeof(int)];

// Sets the values of inode i in the inode table in memory
void set_inode(int i, int mode, int link_cnt, int uid, int gid, int size, int data_ptrs[12], int indirectPointer){
	inTable[i].mode = mode;
	inTable[i].link_cnt = link_cnt;
	inTable[i].uid = uid;
	inTable[i].gid = gid;
	inTable[i].size = size;
	inTable[i].indirectPointer = indirectPointer;
	for (int j = 0; j < 12; j++){
		inTable[i].data_ptrs[j] = data_ptrs[j];
	}

	inTableStatus[i] = 1;
}

// The same as set_inode, but instead sets the inode as unused
void rm_inode(int i){
	inTable[i].mode = -1;
	inTable[i].link_cnt = -1;
	inTable[i].uid = -1;
	inTable[i].gid = -1;
	inTable[i].size = -1;
	inTable[i].indirectPointer = -1;
	inTable[i].uid = -1;

	for (int j = 0; j < 12; j++){
		inTable[i].data_ptrs[j] = -1;
	}

	inTableStatus[i] = 0;
}

// Initializes file descriptor table
void init_fdt(){
	for (int i = 0; i < NUM_INODES; i++){
		fdTable[i].inodeIndex = -1;
	}
}

// Initializes the inode table, by setting all inodes as unused
void init_in_table(){
	for (int i = 0; i < NUM_INODES; i++){
		rm_inode(i);
	}
}

// Initializes the superblock in memory
void init_super(){
	super.magic = MAGIC;
    super.block_size = BLOCK_SIZE;
    super.fs_size = NUM_BLOCKS;
    super.inode_table_len = NUM_INODES;
    super.root_dir_inode = ROOTDIR_INODE;
}

// Initializes the root directory in memory
void init_root(){
	for (int i = 0; i < NUM_INODES; i++){
		rootDir[i].num = -1;
		for (int j = 0; j < MAX_FILE_NAME; j++){
			rootDir[i].name[j] = '\0';
		}
	}	
}

// Returns the inode corresponding to a given file in the root dir, or -1 if it doesn't exist
int retrieve_file(char *path){
	char *buffer = malloc(sizeof(char) * (MAX_FILE_NAME + 5)); // Slightly bigger than max filename

	for (int i = 0; i < NUM_INODES; i++){
		if (rootDir[i].num != -1){
			strcpy(buffer, rootDir[i].name);
			if (strcmp(buffer, path) == 0){
				free(buffer);
				return rootDir[i].num;
			}
		}
	}
	free(buffer);
	return -1;
}

// Returns first available fd, or -1 if none is available
int first_open_fd(){
	for (int i = 0; i < NUM_INODES; i++){
		if (fdTable[i].inodeIndex == -1){
			return i;
		}
	}
	return -1;
}

// Returns first available inode, or -1 if none is available
int first_open_inode(){
	for (int i = 1; i < NUM_INODES; i++){
		if (inTableStatus[i] == 0){
			inTableStatus[i] = 1;
			return i;
		}
	}
	return -1;
}

// Returns first available spot in the root dir, or -1 if none is available
int first_open_rootDir(){
	for (int i = 0; i < NUM_INODES; i++){
		if (rootDir[i].num == -1){
			return i;
		}
	}
	return -1;
}

int calculate_inode_table_blocks(){
	int numInodeBlocks = (sizeof(inTable)/BLOCK_SIZE);
	if (sizeof(inTable) % BLOCK_SIZE != 0){
		numInodeBlocks += 1;
	}
	return numInodeBlocks;
}

void mksfs(int fresh) {
	if (fresh == 1){

		init_fdt();

		init_root();
		
		init_fresh_disk(MONFOURNYDAIGNEAULT_SANDRINE_DISK, BLOCK_SIZE, NUM_BLOCKS);

		init_in_table();

		locus = 0;

		// Calculate the number of blocks occupied by the inode table
		int numInodeBlocks = calculate_inode_table_blocks();

		// Calculate the number of blocks occupied by the root directory
		int numRootDirBlocks = (sizeof(rootDir)/BLOCK_SIZE);
		if (sizeof(rootDir) % BLOCK_SIZE != 0){
			numRootDirBlocks += 1;
		}


		// Set aside space on the bitmap for superblock, inode table, root dir free bitmap and inodeTableStatus
		force_set_index(0);
		for (int i = 1; i < numInodeBlocks + 1; i++){
			force_set_index(i); // offset by one, because our superblock is in block 0
		}

		for (int i = numInodeBlocks+1; i < numRootDirBlocks + (numInodeBlocks + 1); i++){
			force_set_index(i); 
		}

		force_set_index(1022);
		force_set_index(1023);

		int rootptrs[12];
		for (int i = 0; i < numRootDirBlocks; i++){
			rootptrs[i] = i + numInodeBlocks+1;
		}

		set_inode(ROOTDIR_INODE, 0, numRootDirBlocks, 0, 0, -1, rootptrs, -1);

		// Write superblock to disk 
		init_super();
		write_blocks(0, 1, &super);

		// Write inode table to disk
		write_blocks(1, numInodeBlocks, &inTable);

		void *bufferino = malloc(BLOCK_SIZE * numRootDirBlocks);
		memcpy(bufferino, &rootDir, sizeof(rootDir));
		// Write rootDir to disk
		write_blocks(inTable[ROOTDIR_INODE].data_ptrs[0], numRootDirBlocks, bufferino);
		free(bufferino);

		// Write status of inode table to disk
		write_blocks(1022, 1, &inTableStatus);

		// Write bitmap to disk
		write_blocks(1023, 1, &free_bit_map);

	} else {

		init_fdt();

		init_disk(MONFOURNYDAIGNEAULT_SANDRINE_DISK, BLOCK_SIZE, NUM_BLOCKS);

		locus = 0;

		void *bufferino = malloc(BLOCK_SIZE);

		// Read the superblock into memory
		read_blocks(0, 1, bufferino);
		memcpy(&super, bufferino, sizeof(superblock_t));

		// Read the inode table into memory
		// Calculate the number of blocks occupied by the inode table
		int numInodeBlocks = calculate_inode_table_blocks();

		free(bufferino);

		bufferino = malloc(BLOCK_SIZE * numInodeBlocks);
		read_blocks(1, numInodeBlocks, bufferino);
		memcpy(&inTable, bufferino, sizeof(inTable));

		free(bufferino);

		// Read the inode table status into memory
		bufferino = malloc(BLOCK_SIZE);
		read_blocks(1022, 1 , bufferino);
		memcpy(&inTableStatus, bufferino, sizeof(inTableStatus));

		free(bufferino);

		// Read the bitmap into memory
		bufferino = malloc(BLOCK_SIZE);
		read_blocks(1023, 1, bufferino);
		memcpy(&free_bit_map, bufferino, sizeof(free_bit_map));

		free(bufferino);

		// Read the root directory into memory
		bufferino = malloc(BLOCK_SIZE * (inTable[ROOTDIR_INODE].link_cnt));		
		read_blocks(inTable[ROOTDIR_INODE].data_ptrs[0], inTable[ROOTDIR_INODE].link_cnt, bufferino);
		memcpy(&rootDir, bufferino, sizeof(rootDir));

		free(bufferino);
	}
}

int sfs_getnextfilename(char *fname){
	if (locus < NUM_INODES){
		while (rootDir[locus].num == -1){
			locus++;
			if (locus >= NUM_INODES){
				locus = 0;
				return 0;
			}
		}
		strcpy(fname, rootDir[locus].name);
		locus++;
		return 1; 
	} else {
		locus = 0;
		return 0;
	}

}
int sfs_getfilesize(const char* path){
	int fileInode = retrieve_file(path);
	if (fileInode != -1){
		return inTable[fileInode].size;
	} else {
		return -1;
	}
}

int sfs_fopen(char *name){
	// Check for invalid filename 
	int count = 0;
	while (*(name + count) != '\0'){
		count++;
	}
	if (count > MAX_FILE_NAME + 1){
		return -1;
	}

	int newFd = first_open_fd();
	if (newFd != -1){	
		int fileInode = retrieve_file(name);
		if (fileInode != -1){ // file exists

			for (int i = 0; i < NUM_INODES; i++){
				if (fdTable[i].inodeIndex == fileInode){
					return -1;
				}
			}

			fdTable[newFd].inodeIndex = fileInode;

			// eventually, might want to keep less in active memory...
			fdTable[newFd].inode = &(inTable[fileInode]);

			// We open in append mode, so rwptr is at the end of the file
			fdTable[newFd].rwptr = inTable[fileInode].size;

			return newFd;
		} else { // file does not exist, we need to create it

			// Pick an inode for it 
			int newInodeIndex = first_open_inode();
			if (newInodeIndex == -1){// No more free inodes !
				return -1;
			}
			// Set up the directory entry
			int newDirEntryIndex = first_open_rootDir(); 
			if (newDirEntryIndex == -1){// No more open spots in rootDir
				return -1;
			}


			// Pick a data block for it
			int newDataPtr = get_index();
			if (newDataPtr == 0){
				return -1;
			}

			int data_ptrs[12];
			data_ptrs[0] = newDataPtr;
			for (int i = 1; i < 12; i++){
				data_ptrs[i] = -1;
			}

			fdTable[newFd].inodeIndex = newInodeIndex;		

			rootDir[newDirEntryIndex].num = newInodeIndex;
			strcpy(rootDir[newDirEntryIndex].name, name);

			// Set up the inode 
			set_inode(newInodeIndex, 0, 1, 0, 0, 0, data_ptrs, -1);
			fdTable[newFd].inode = &(inTable[newInodeIndex]);
			fdTable[newFd].rwptr = inTable[newInodeIndex].size;

			int numRootDirBlocks = (sizeof(rootDir)/BLOCK_SIZE);
			if (sizeof(rootDir) % BLOCK_SIZE != 0){
				numRootDirBlocks += 1;
			}

			inTable[ROOTDIR_INODE].size += 1;

			void *bufferino = malloc(BLOCK_SIZE * numRootDirBlocks);
			memcpy(bufferino, &rootDir, sizeof(rootDir));
			// Write rootDir to disk
			write_blocks(inTable[ROOTDIR_INODE].data_ptrs[0], numRootDirBlocks, bufferino);
			free(bufferino);

			// Write the inode table to disk
			int numInodeBlocks = calculate_inode_table_blocks();
			write_blocks(1, numInodeBlocks, &inTable);

			// Write the inode status to disk
			write_blocks(1022, 1, &inTableStatus);

			// Write bitmap to disk
			write_blocks(1023, 1, &free_bit_map);
			return newFd;
		}
	} else {
		return -1;
	}
}

int sfs_fclose(int fileID) {
	if (fdTable[fileID].inodeIndex == -1){
		return -1;
	} else {
		fdTable[fileID].inodeIndex = -1;
		return 0;	
	}
}

int sfs_fread(int fileID, char *buf, int length) {
	int inodeRD = fdTable[fileID].inodeIndex;
	int toRead;
	int end;

	// Handle "wrong input" situations
	if (fileID < 0){ return -1;} // not a valid fileID
	if (inodeRD == -1){ return -1;}// no corresponding fd
	if (inTable[inodeRD].size <= 0){ return length;} // nothing to read
	if (inTable[inodeRD].size < (fdTable[fileID].rwptr + length)){// we're asked to read too much for the size of the file
		toRead = inTable[inodeRD].size - fdTable[fileID].rwptr;
		end = inTable[inodeRD].size / BLOCK_SIZE;
		if ((inTable[inodeRD].size % BLOCK_SIZE) != 0) {
			end += 1;
		}
	} else {
		toRead = length;
		end = (fdTable[fileID].rwptr + length) / BLOCK_SIZE;
		if ((fdTable[fileID].rwptr + length) % BLOCK_SIZE != 0) {
			end += 1;
		}
	}

	// Calculate index of first block 
	int start = fdTable[fileID].rwptr / BLOCK_SIZE;
	// Calculate offset 
	int startOffset = fdTable[fileID].rwptr % BLOCK_SIZE;

	// If the indirect pointer is used, we'll need additional addresses 
	void *buffer = malloc(BLOCK_SIZE);
	if (inTable[inodeRD].link_cnt > 12){
		read_blocks(inTable[inodeRD].indirectPointer, 1, buffer);
		memcpy(&indy_block, buffer, BLOCK_SIZE);
	}


	// Create a buffer to store the file
	void *bufferino = malloc(BLOCK_SIZE * end);

	// Load up the file in memory; first the direct pointers, then the indirect
	for (int i = start; i < inTable[inodeRD].link_cnt && i < end; i++){
		if (i >= 12){
			read_blocks(indy_block[i-12], 1, (bufferino + (i-start) * BLOCK_SIZE));
		} else {
			read_blocks(inTable[inodeRD].data_ptrs[i], 1, (bufferino + (i-start) * BLOCK_SIZE));
		}
	}
	// Copy the part we want to buf
	memcpy(buf, (bufferino + startOffset), toRead);

	// Set the rwptr to its new home
	fdTable[fileID].rwptr += toRead;

	free(bufferino);
	free(buffer);

	return toRead;
}

int sfs_fwrite(int fileID, const char *buf, int length) {
	int inodeWR = fdTable[fileID].inodeIndex;
	int toWrite = length;

	// Handle "wrong input" situations
	if (fileID < 0){ return -1;} // not a valid fileID
	if (inodeWR == -1){ return -1;}// no corresponding fd

	// If the write doesn't fit in the currently allocated space, we'll need to free up more
	int bytesNeeded = fdTable[fileID].rwptr + length; 
	if (bytesNeeded > MAX_FILE_SIZE){
		bytesNeeded = MAX_FILE_SIZE;
		toWrite = MAX_FILE_SIZE - fdTable[fileID].rwptr;
	}

	int currentBlocks = inTable[inodeWR].link_cnt; 


	int blocksNeeded = bytesNeeded / BLOCK_SIZE;
	if (bytesNeeded % BLOCK_SIZE != 0){
		blocksNeeded += 1;
	}

	int additionalBlocks = blocksNeeded - currentBlocks;

	// We might need the indirect pointer
	void *buffer = malloc(BLOCK_SIZE);
	if (inTable[inodeWR].link_cnt > 12){
		read_blocks(inTable[inodeWR].indirectPointer, 1, buffer);
		memcpy(&indy_block, buffer, BLOCK_SIZE);
	} else if (inTable[inodeWR].link_cnt + additionalBlocks > 12) {
		int data_ptr = get_index();
		if (data_ptr == 0){
			return -1;
		}		
		inTable[inodeWR].indirectPointer = data_ptr;
	}
	free(buffer);

	if (additionalBlocks > 0){
		// Set aside space for the new bytes
		for (int i = inTable[inodeWR].link_cnt; i < inTable[inodeWR].link_cnt + additionalBlocks; i++){
			int new_datablock = get_index();
			if (new_datablock == 0){ // We don't have enough blocks and need to abort
				// Better handling needed here for large overflows?
				// Since this behavior is undefined, I simply cancelled the write
				return -1;
			} else {
				if (i >= 12){
					indy_block[i - 12] = new_datablock;
				} else {
					inTable[inodeWR].data_ptrs[i] = new_datablock;	
				}
			}
		}
	} else {
		additionalBlocks = 0;
	}

	// Find the starting and ending block, and the startOffset
	int start = fdTable[fileID].rwptr / BLOCK_SIZE;
	int startOffset = fdTable[fileID].rwptr % BLOCK_SIZE;

	int end = blocksNeeded;

	// Now load up the current file
	void *bufferino = malloc(BLOCK_SIZE * blocksNeeded);

	for (int i = start; i < inTable[inodeWR].link_cnt && i < end; i++){
		if (i >= 12){
			read_blocks(indy_block[i-12], 1, (bufferino + (i-start) * BLOCK_SIZE));
		} else {
			read_blocks(inTable[inodeWR].data_ptrs[i], 1, (bufferino + (i-start) * BLOCK_SIZE));
		}
	}

	memcpy((bufferino + startOffset), buf, toWrite);

	// Write the inode status to disk
	write_blocks(1022, 1, &inTableStatus);

	// Write the bufferino back to disk
	for (int i = start; i < end; i++){
		if (i >= 12){
			write_blocks(indy_block[i-12], 1, (bufferino + (i-start) * BLOCK_SIZE));
		} else {
			write_blocks(inTable[inodeWR].data_ptrs[i], 1, (bufferino + ((i-start) * BLOCK_SIZE)));
		}
	}

	// Save the changes we made to the file system

	// Update the inode
	if (inTable[inodeWR].size < bytesNeeded){
		inTable[inodeWR].size = bytesNeeded;
	} 

	inTable[inodeWR].link_cnt += additionalBlocks;
	fdTable[fileID].rwptr = bytesNeeded;

	if (inTable[inodeWR].link_cnt > 12){
		write_blocks(inTable[inodeWR].indirectPointer, 1, &indy_block);
	}

	// Write the inode table to disk
	int numInodeBlocks = calculate_inode_table_blocks();
	write_blocks(1, numInodeBlocks, &inTable);

	// Write the inode status to disk
	write_blocks(1022, 1, &inTableStatus);

	// Write bitmap to disk
	write_blocks(1023, 1, &free_bit_map);

	free(bufferino);

	return toWrite;
}

int sfs_fseek(int fileID, int loc) {
	// As a precaution, to avoid invalid seeks
	if (inTable[fdTable[fileID].inodeIndex].size < loc){ 
		return -1;
	}

	fdTable[fileID].rwptr = loc;
	return 0;
}

int sfs_remove(char *file) {
	int fileInode = retrieve_file(file);
	if (fileInode > 0) {

		// Unallocate all the data blocks
		for (int i = 0; i < inTable[fileInode].link_cnt && i < 12; i++){
			rm_index(inTable[fileInode].data_ptrs[i]);
		}

		// If there's an indirect pointer:
		if (inTable[fileInode].link_cnt > 12){
			// Get the indirect pointer
			void *buffer = malloc(BLOCK_SIZE);
			read_blocks(inTable[fileInode].indirectPointer, 1, buffer);
			memcpy(&indy_block, buffer, BLOCK_SIZE);

			for (int i = 12; i < inTable[fileInode].link_cnt; i++){
				rm_index(indy_block[i-12]);
			}

			free(buffer);
		}

		// Remove the inode
		rm_inode(fileInode);

		// Remove the directory entry
		for (int i = 0; i < NUM_INODES; i++){
			if (strcmp(rootDir[i].name, file) == 0){
				rootDir[i].num = -1;
				for (int j = 0; j < MAX_FILE_NAME; j++){
					rootDir[i].name[0]='\0';
				}
				break;
			}
		}

		inTable[ROOTDIR_INODE].size -= 1;

		int numRootDirBlocks = (sizeof(rootDir)/BLOCK_SIZE);
		if (sizeof(rootDir) % BLOCK_SIZE != 0){
			numRootDirBlocks += 1;
		}

		// Write rootDir to disk
		void *bufferino = malloc(BLOCK_SIZE * numRootDirBlocks);
		memcpy(bufferino, &rootDir, sizeof(rootDir));
		write_blocks(inTable[ROOTDIR_INODE].data_ptrs[0], numRootDirBlocks, bufferino);
		free(bufferino);

		int numInodeBlocks = calculate_inode_table_blocks();
		// Write the inode table to disk
		write_blocks(1, numInodeBlocks, &inTable);

		// Write the inode status to disk
		write_blocks(1022, 1, &inTableStatus);

		// Write bitmap to disk
		write_blocks(1023, 1, &free_bit_map);

		return 0;
	} else {
		return -1;
	}
}
