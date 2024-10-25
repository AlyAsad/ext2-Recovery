// helper files
#include "ext2fs.h"
#include "ext2fs_print.h"
#include "identifier.h"

// c libaries
#include <sys/stat.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// c++ libraries
#include <iostream>
#include <vector>


using namespace std;


//////////////////////////// global variable declarations
// useless
int ret;


// related to input/file
int fd; // file descriptor for the ext2 file
uint8_t* data_identifier; // unique identifier in start of data blocks


ext2_super_block super; // super block
int block_size;
int blocks_per_group;
int num_of_groups;
int zeroth_group_starting_block;
int last_group_blocks;
vector<ext2_block_group_descriptor> block_group_tables;

//////////////////////////// main/helper function declarations
// main functions
void bitmapRecovery();
void pointerRecovery();
void printAfterFixing();

// helper functions
void parseInput();
ext2_inode findiNode(int inode);

int main(int argc, char *argv[]) {

  fd = open(argv[1], O_RDWR);
  data_identifier = parse_identifier(argc, argv);
  
  parseInput();
  
  bitmapRecovery();
  
  pointerRecovery();
  
  printAfterFixing();
  
  delete[] data_identifier;
  close(fd);

}



// TODO
void pointerRecovery() {
  
  
  
}

void inodeRecovery() {
  
  // main for loop, for each group
  int j = -1;
  for (ext2_block_group_descriptor &group : block_group_tables) {
    j++;
    ext2_inode inode;
    uint8_t byte;
    // for each inode
    int i = 0;
    
    if (j == 0) {
      byte = 0b11111111;
      lseek(fd, group.inode_bitmap*block_size, SEEK_SET);
      ret = write(fd, &byte, 1);
  
      ret = read(fd, &byte, 1);
      byte = byte | (0b00000011);
      lseek(fd, group.inode_bitmap*block_size + 1, SEEK_SET);
      ret = write(fd, &byte, 1);
      
      i = 10;
    }
    for (; i < super.inodes_per_group; i++) {
      lseek(fd, group.inode_table*block_size + i*super.inode_size, SEEK_SET);
      ret = read(fd, &inode, sizeof(inode));
      
      // deleted inode
      if (inode.creation_time == 0 || inode.deletion_time != 0) continue;
      
      lseek(fd, group.inode_bitmap*block_size + (i/8), SEEK_SET);
      ret = read(fd, &byte, 1);
      
      byte = byte | (0b00000001 << (((int)i)%8));
      lseek(fd, group.inode_bitmap*block_size + (i/8), SEEK_SET);
      ret = write(fd, &byte, 1);
    }
    
  } // end of main for loop
  
}


bool isDataBlockEmpty(int block) {
  
  lseek(fd, block*block_size, SEEK_SET);
  
  uint32_t bytes;
  for (int i = 0; i < block_size/4; i++) {
      ret = read(fd, &bytes, sizeof(bytes));
      if (bytes) return false;
    }
  
  return true;
}

void helperindirect(int p, int level) {
  
  int g = (p - zeroth_group_starting_block)/blocks_per_group;
  int pointer = p - g*blocks_per_group - zeroth_group_starting_block;
  uint8_t byte;
  
        
  lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
  ret = read(fd, &byte, 1);
  
  byte = byte | (0b00000001 << (pointer%8));
  lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
  ret = write(fd, &byte, 1);
  
  uint32_t dpointer;
  int numPointers = block_size/4;
  
  for (int i = 0; i < numPointers; i++) {
    
    lseek(fd, p*block_size + i*4, SEEK_SET);
    ret = read(fd, &dpointer, 4);
    
    if (dpointer == 0) continue;
    
    g = (dpointer - zeroth_group_starting_block)/blocks_per_group;
    pointer = dpointer - g*blocks_per_group - zeroth_group_starting_block;
    
    lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
    ret = read(fd, &byte, 1);
    
    byte = byte | (0b00000001 << (pointer%8));
    lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
    ret = write(fd, &byte, 1);
    
    if (level > 1)
      helperindirect(dpointer, level - 1);
  }
  
}



void dataRecovery() {
  
  ext2_inode inode;
  uint8_t byte;
  int gn = -1;
  
      
  // first check all inodes for their data blocks
  for (ext2_block_group_descriptor &group : block_group_tables) {
    gn++;
    // for each inode
    for (int i = 0; i < super.inodes_per_group; i++) {
    
      if (gn == 0 && i != 1 && i <= 9) continue;
      
      lseek(fd, group.inode_bitmap*block_size + (i/8), SEEK_SET);
      ret = read(fd, &byte, 1);
      
      // inode not active
      if ((byte && (0b00000001 << (i%8))) == 0) continue;
      
      lseek(fd, group.inode_table*block_size + i*super.inode_size, SEEK_SET);
      ret = read(fd, &inode, sizeof(inode));
      
      for (int j = 0; j < EXT2_NUM_DIRECT_BLOCKS; j++) {
        if (inode.direct_blocks[j] == 0) continue;
        
        int g = (inode.direct_blocks[j] - zeroth_group_starting_block)/blocks_per_group;
        int pointer = inode.direct_blocks[j] - g*blocks_per_group - zeroth_group_starting_block;
        
        
        lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
        ret = read(fd, &byte, 1);
        
        byte = byte | (0b00000001 << (pointer%8));
        lseek(fd, block_group_tables[g].block_bitmap*block_size + (pointer/8), SEEK_SET);
        ret = write(fd, &byte, 1);
        
      }
      
      
      if (inode.single_indirect != 0) {
        helperindirect(inode.single_indirect, 1);
      }
      
      if (inode.double_indirect != 0) {
        helperindirect(inode.double_indirect, 2);
      }
      
      if (inode.triple_indirect != 0) {
        helperindirect(inode.triple_indirect, 3);
      }
      
    }
    
  } // end of main for loop
  
  gn = -1;
  // now check every single data block in sequence
  for (ext2_block_group_descriptor &group : block_group_tables) {
    gn++;
    int startBlock = group.inode_table + (super.inodes_per_group*super.inode_size)/block_size;
    if ((super.inodes_per_group*super.inode_size)%block_size)
      startBlock++;
    
    int numberOfDataBlocks;
    // last group
    if ((gn + 1) == num_of_groups) {
      numberOfDataBlocks = gn*blocks_per_group + zeroth_group_starting_block + last_group_blocks - startBlock;
    }
    else {
    numberOfDataBlocks = (gn + 1)*blocks_per_group + zeroth_group_starting_block - startBlock;
    }
    
    uint8_t byte;
    
    int s = (gn)*blocks_per_group + zeroth_group_starting_block;
    while (s < startBlock) {
      int pointer = s - gn*blocks_per_group;
      
      lseek(fd, group.block_bitmap*block_size + (pointer/8), SEEK_SET);
      ret = read(fd, &byte, 1);
      byte = byte | (0b00000001 << (pointer%8));
      lseek(fd, group.block_bitmap*block_size + (pointer/8), SEEK_SET);
      ret = write(fd, &byte, 1);
      
      s++;
    }
    
    s--;
    for (int i = 0; i < numberOfDataBlocks; i++) {
      s++;
      
      int pointer = s - gn*blocks_per_group;
      
      if (isDataBlockEmpty(s)) continue;
      
      lseek(fd, group.block_bitmap*block_size + (pointer/8), SEEK_SET);
      ret = read(fd, &byte, 1);
      byte = byte | (0b00000001 << (pointer%8));
      lseek(fd, group.block_bitmap*block_size + (pointer/8), SEEK_SET);
      ret = write(fd, &byte, 1);
    }
  } // end of 2nd main for loop
  
}

void bitmapRecovery() {

  inodeRecovery();
  dataRecovery();
  
}


void printDirectory(int depth, ext2_inode root);


void printIndirect(uint32_t p, int level, int depth) {
  
  uint32_t dpointer;
  int numPointers = block_size/4;
  if (level == 1) {
    int length;
    ext2_dir_entry curr;
    ext2_inode temp;
  
    for (int i = 0; i < numPointers; i++) {
      lseek(fd, p*block_size + i*4, SEEK_SET);
      ret = read(fd, &dpointer, 4);
      
      if (dpointer == 0) continue;
      length = 0;
      while (length < block_size) {
        lseek(fd, dpointer*block_size + length, SEEK_SET);
        ret = read(fd, &curr, 8);
        
        ret = read(fd, curr.name, curr.name_length);
        curr.name[curr.name_length] = '\0';
        length += curr.length;
        
        if (curr.length == block_size) continue;
        
        if (strcmp(curr.name, ".") != 0 && strcmp(curr.name, "..") != 0) {
          cout << string(depth, '-') << " " << curr.name;
          temp = findiNode(curr.inode);
          
          if (S_ISDIR(temp.mode)) {
            cout << "/" << endl;
            printDirectory(depth + 1, temp);
          }
          else {
            cout << endl;
          }
        }
      }
    
    } //end of for loop
    
  return;
  }
  
  //double-triple
  for (int i = 0; i < numPointers; i++) {
    lseek(fd, p*block_size + i*4, SEEK_SET);
    ret = read(fd, &dpointer, 4);
    if (dpointer == 0) continue;
    printIndirect(dpointer, level - 1, depth);
  }
}



ext2_inode findiNode(int inode) {
  inode--;
  ext2_inode root;
  int group = inode/super.inodes_per_group;
  lseek(fd, block_group_tables[group].inode_table*block_size + (inode%super.inodes_per_group)*super.inode_size, SEEK_SET);
  ret = read(fd, &root, sizeof(root));
  return root;
}





void printDirectory(int depth, ext2_inode root) {
  
  ext2_dir_entry curr;
  ext2_inode temp;
  int length = 0;
  
  // direct blocks
  for (int i = 0; i < EXT2_NUM_DIRECT_BLOCKS; i++) {
    if (root.direct_blocks[i] == 0) continue;
    length = 0;
    while (length < block_size) {
      lseek(fd, root.direct_blocks[i]*block_size + length, SEEK_SET);
      ret = read(fd, &curr, 8);
      
      ret = read(fd, curr.name, curr.name_length);
      curr.name[curr.name_length] = '\0';
      length += curr.length;
      
      if (curr.length == block_size) continue;
      
      if (strcmp(curr.name, ".") != 0 && strcmp(curr.name, "..") != 0) {
        cout << string(depth, '-') << " " << curr.name;
        temp = findiNode(curr.inode);
        
        if (S_ISDIR(temp.mode)) {
          cout << "/" << endl;
          printDirectory(depth + 1, temp);
        }
        else {
          cout << endl;
        }
      }
    } // end of data block
    
  } // end of direct blocks
  
  // indirect blocks

  if (root.single_indirect != 0) {
    printIndirect(root.single_indirect, 1, depth);
  }
  
  if (root.double_indirect != 0) {
    printIndirect(root.double_indirect, 2, depth);
  }
  
  if (root.triple_indirect != 0) {
    printIndirect(root.triple_indirect, 3, depth);
  }
  

}



void printAfterFixing() {
  
  cout << "- root/" << endl;
  
  ext2_inode root;
  lseek(fd, block_group_tables[0].inode_table*block_size + super.inode_size, SEEK_SET);
  ret = read(fd, &root, sizeof(root));
  printDirectory(2, root);
  
}


//
void parseInput() {
  
  // reading the super block
  lseek(fd, 1024, SEEK_SET);
  
  ret = read(fd, &super, sizeof(super));
  block_size = pow(2, 10 + super.log_block_size);
  blocks_per_group = super.blocks_per_group;
  num_of_groups = super.block_count/blocks_per_group;
  zeroth_group_starting_block = 1024/block_size;
  last_group_blocks = super.block_count - (num_of_groups - 1)*blocks_per_group - zeroth_group_starting_block;
  
  // reading the block group descriptor tables
  lseek(fd, ((1024 + sizeof(super))/block_size + 1)*block_size, SEEK_SET);
  
  for (int i = 0; i < num_of_groups; i++) {
  ext2_block_group_descriptor temp;
  ret = read(fd, &temp, sizeof(temp));
  block_group_tables.push_back(temp);
  }
  
}
