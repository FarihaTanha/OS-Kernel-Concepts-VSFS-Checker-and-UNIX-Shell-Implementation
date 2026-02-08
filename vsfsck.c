/**
 * vsfsck - Very Simple File System Consistency Checker
 * 
 * This program checks the consistency of a VSFS file system image,
 * identifies errors, and corrects them when possible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define INODE_SIZE 256
#define INODE_COUNT 80  // 5 blocks * 4096 bytes per block / 256 bytes per inode
#define MAGIC_NUMBER 0xD34D

// Superblock structure
typedef struct {
    uint16_t magic;              // Magic number (0xD34D)
    uint32_t block_size;         // Block size (4096)
    uint32_t total_blocks;       // Total number of blocks (64)
    uint32_t inode_bitmap_block; // Inode bitmap block number (1)
    uint32_t data_bitmap_block;  // Data bitmap block number (2)
    uint32_t inode_table_start;  // Inode table start block number (3)
    uint32_t data_block_start;   // First data block number (8)
    uint32_t inode_size;         // Size of each inode (256)
    uint32_t inode_count;        // Number of inodes
    uint8_t reserved[4058];      // Reserved space
} superblock_t;

// Inode structure
typedef struct {
    uint32_t mode;               // File mode
    uint32_t uid;                // User ID
    uint32_t gid;                // Group ID
    uint32_t size;               // File size in bytes
    uint32_t atime;              // Last access time
    uint32_t ctime;              // Creation time
    uint32_t mtime;              // Last modification time
    uint32_t dtime;              // Deletion time
    uint32_t nlink;              // Number of hard links
    uint32_t blocks;             // Number of data blocks
    uint32_t direct_blocks[12];  // Direct block pointers
    uint32_t indirect_block;     // Single indirect block pointer
    uint32_t double_indirect;    // Double indirect block pointer
    uint32_t triple_indirect;    // Triple indirect block pointer
    uint8_t reserved[156];       // Reserved space
} inode_t;

// Global variables
FILE *fs_image;
superblock_t superblock;
uint8_t inode_bitmap[BLOCK_SIZE];
uint8_t data_bitmap[BLOCK_SIZE];
inode_t inodes[INODE_COUNT];

// Block reference tracking
bool block_referenced[TOTAL_BLOCKS];
int block_referenced_by[TOTAL_BLOCKS];  // -1 means not referenced, otherwise stores inode number
int errors_found = 0;
int errors_fixed = 0;

// Function prototypes
void read_superblock();
void read_bitmaps();
void read_inodes();
bool check_superblock();
bool check_inode_bitmap_consistency();
bool check_data_bitmap_consistency();
bool check_duplicate_blocks();
bool check_bad_blocks();
bool fix_errors();
bool is_valid_inode(int inode_index);
void mark_block_referenced(int block_num, int inode_num);
int get_bit(uint8_t *bitmap, int bit_index);
void set_bit(uint8_t *bitmap, int bit_index);
void clear_bit(uint8_t *bitmap, int bit_index);
void write_superblock();
void write_bitmaps();
void write_inodes();

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <fs_image>\n", argv[0]);
        return 1;
    }

    // Open the file system image
    fs_image = fopen(argv[1], "r+");
    if (fs_image == NULL) {
        perror("Error opening file system image");
        return 1;
    }

    // Initialize the block reference tracking array
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        block_referenced[i] = false;
        block_referenced_by[i] = -1;
    }

    // Read the superblock, bitmaps, and inodes
    read_superblock();
    read_bitmaps();
    read_inodes();

    // Check for inconsistencies
    printf("Checking VSFS file system consistency...\n");
    bool sb_consistent = check_superblock();
    bool inode_bitmap_consistent = check_inode_bitmap_consistency();
    bool data_bitmap_consistent = check_data_bitmap_consistency();
    bool no_duplicate_blocks = check_duplicate_blocks();
    bool no_bad_blocks = check_bad_blocks();

    // Report results
    printf("\nFile system check summary:\n");
    printf("Superblock: %s\n", sb_consistent ? "OK" : "ERRORS FOUND");
    printf("Inode bitmap: %s\n", inode_bitmap_consistent ? "OK" : "ERRORS FOUND");
    printf("Data bitmap: %s\n", data_bitmap_consistent ? "OK" : "ERRORS FOUND");
    printf("Duplicate blocks: %s\n", no_duplicate_blocks ? "NONE FOUND" : "ERRORS FOUND");
    printf("Bad blocks: %s\n", no_bad_blocks ? "NONE FOUND" : "ERRORS FOUND");
    
    printf("\nTotal errors found: %d\n", errors_found);
    
    // Fix errors if any were found
    if (errors_found > 0) {
        printf("\nAttempting to fix errors...\n");
        fix_errors();
        printf("Errors fixed: %d\n", errors_fixed);
        
        // Reset error counters
        int original_errors = errors_found;
        errors_found = 0;
        errors_fixed = 0;
        
        // Re-check the file system
        printf("\nRe-checking file system for remaining errors...\n");
        bool sb_consistent = check_superblock();
        bool inode_bitmap_consistent = check_inode_bitmap_consistency();
        bool data_bitmap_consistent = check_data_bitmap_consistency();
        bool no_duplicate_blocks = check_duplicate_blocks();
        bool no_bad_blocks = check_bad_blocks();
        
        // Report re-check results
        printf("\nFile system re-check summary:\n");
        printf("Superblock: %s\n", sb_consistent ? "OK" : "ERRORS REMAIN");
        printf("Inode bitmap: %s\n", inode_bitmap_consistent ? "OK" : "ERRORS REMAIN");
        printf("Data bitmap: %s\n", data_bitmap_consistent ? "OK" : "ERRORS REMAIN");
        printf("Duplicate blocks: %s\n", no_duplicate_blocks ? "NONE FOUND" : "ERRORS REMAIN");
        printf("Bad blocks: %s\n", no_bad_blocks ? "NONE FOUND" : "ERRORS REMAIN");
        
        printf("\nOriginal errors: %d\n", original_errors);
        printf("Remaining errors: %d\n", errors_found);
        
        if (errors_found == 0) {
            printf("\nAll errors successfully fixed! File system is now consistent.\n");
        } else {
            printf("\nSome errors could not be fixed automatically. Manual intervention may be required.\n");
        }
    } else {
        printf("\nNo errors found. File system is consistent.\n");
    }

    //  Close the file system image
    fclose(fs_image);
    
    return 0;
}

void read_superblock() {
    // Seek to the beginning of the file
    fseek(fs_image, 0, SEEK_SET);
    
    // Read the superblock
    fread(&superblock, sizeof(superblock_t), 1, fs_image);
}

void read_bitmaps() {
    // Read the inode bitmap
    fseek(fs_image, superblock.inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, BLOCK_SIZE, 1, fs_image);
    
    // Read the data bitmap
    fseek(fs_image, superblock.data_bitmap_block * BLOCK_SIZE, SEEK_SET);
    fread(data_bitmap, BLOCK_SIZE, 1, fs_image);
}

void read_inodes() {
    // Read each inode from the inode table
    for (int i = 0; i < INODE_COUNT; i++) {
        fseek(fs_image, superblock.inode_table_start * BLOCK_SIZE + i * INODE_SIZE, SEEK_SET);
        fread(&inodes[i], sizeof(inode_t), 1, fs_image);
    }
}

bool check_superblock() {
    bool consistent = true;
    
    // Check magic number
    if (superblock.magic != MAGIC_NUMBER) {
        printf("Error: Invalid magic number (0x%x), should be 0x%x\n", superblock.magic, MAGIC_NUMBER);
        consistent = false;
        errors_found++;
    }
    
    // Check block size
    if (superblock.block_size != BLOCK_SIZE) {
        printf("Error: Invalid block size (%u), should be %u\n", superblock.block_size, BLOCK_SIZE);
        consistent = false;
        errors_found++;
    }
    
    // Check total blocks
    if (superblock.total_blocks != TOTAL_BLOCKS) {
        printf("Error: Invalid total blocks (%u), should be %u\n", superblock.total_blocks, TOTAL_BLOCKS);
        consistent = false;
        errors_found++;
    }
    
    // Check inode bitmap block
    if (superblock.inode_bitmap_block != 1) {
        printf("Error: Invalid inode bitmap block (%u), should be 1\n", superblock.inode_bitmap_block);
        consistent = false;
        errors_found++;
    }
    
    // Check data bitmap block
    if (superblock.data_bitmap_block != 2) {
        printf("Error: Invalid data bitmap block (%u), should be 2\n", superblock.data_bitmap_block);
        consistent = false;
        errors_found++;
    }
    
    // Check inode table start
    if (superblock.inode_table_start != 3) {
        printf("Error: Invalid inode table start (%u), should be 3\n", superblock.inode_table_start);
        consistent = false;
        errors_found++;
    }
    
    // Check data block start
    if (superblock.data_block_start != 8) {
        printf("Error: Invalid data block start (%u), should be 8\n", superblock.data_block_start);
        consistent = false;
        errors_found++;
    }
    
    // Check inode size
    if (superblock.inode_size != INODE_SIZE) {
        printf("Error: Invalid inode size (%u), should be %u\n", superblock.inode_size, INODE_SIZE);
        consistent = false;
        errors_found++;
    }
    
    // Check inode count
    if (superblock.inode_count != INODE_COUNT && superblock.inode_count != 0) {
        printf("Error: Invalid inode count (%u), should be %u\n", superblock.inode_count, INODE_COUNT);
        consistent = false;
        errors_found++;
    }
    
    return consistent;
}

bool is_valid_inode(int inode_index) {
    // An inode is valid if:
    // 1. Its number of links is greater than 0
    // 2. Its deletion time is 0
    return inodes[inode_index].nlink > 0 && inodes[inode_index].dtime == 0;
}

bool check_inode_bitmap_consistency() {
    bool consistent = true;
    
    // Check if each bit set in the inode bitmap corresponds to a valid inode
    for (int i = 0; i < INODE_COUNT; i++) {
        int bit_value = get_bit(inode_bitmap, i);
        bool valid = is_valid_inode(i);
        
        if (bit_value && !valid) {
            printf("Error: Inode %d is marked as used in bitmap but is not valid\n", i);
            consistent = false;
            errors_found++;
        } else if (!bit_value && valid) {
            printf("Error: Inode %d is valid but not marked as used in bitmap\n", i);
            consistent = false;
            errors_found++;
        }
    }
    
    return consistent;
}

bool check_data_bitmap_consistency() {
    bool consistent = true;
    
    // Reset block reference tracking
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        block_referenced[i] = false;
        block_referenced_by[i] = -1;
    }
    
    // First, mark blocks referenced by inodes
    for (int i = 0; i < INODE_COUNT; i++) {
        if (is_valid_inode(i)) {
            // Mark direct blocks
            for (int j = 0; j < 12; j++) {
                if (inodes[i].direct_blocks[j] != 0) {
                    mark_block_referenced(inodes[i].direct_blocks[j], i);
                }
            }
            
            // Mark single indirect blocks
            if (inodes[i].indirect_block != 0) {
                mark_block_referenced(inodes[i].indirect_block, i);
                
                // Read the indirect block
                uint32_t indirect_entries[BLOCK_SIZE / sizeof(uint32_t)];
                fseek(fs_image, inodes[i].indirect_block * BLOCK_SIZE, SEEK_SET);
                fread(indirect_entries, BLOCK_SIZE, 1, fs_image);
                
                // Mark each referenced block
                for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                    if (indirect_entries[j] != 0) {
                        mark_block_referenced(indirect_entries[j], i);
                    }
                }
            }
            
            // For simplicity, we're not checking double and triple indirect blocks in this implementation
            // but the same principle would apply
        }
    }
    
    // Check if every block marked as used in the data bitmap is referenced by an inode
    for (int i = superblock.data_block_start; i < TOTAL_BLOCKS; i++) {
        int bit_value = get_bit(data_bitmap, i - superblock.data_block_start);
        
        if (bit_value && !block_referenced[i]) {
            printf("Error: Block %d is marked as used in data bitmap but not referenced by any inode\n", i);
            consistent = false;
            errors_found++;
        } else if (!bit_value && block_referenced[i]) {
            printf("Error: Block %d is referenced by inode %d but not marked as used in data bitmap\n", 
                   i, block_referenced_by[i]);
            consistent = false;
            errors_found++;
        }
    }
    
    return consistent;
}

void mark_block_referenced(int block_num, int inode_num) {
    // Skip invalid block numbers
    if (block_num < superblock.data_block_start || block_num >= TOTAL_BLOCKS) {
        return;
    }
    
    if (block_referenced[block_num]) {
        // This block is already referenced by another inode, which is a problem
        // We'll handle this in check_duplicate_blocks()
    } else {
        block_referenced[block_num] = true;
        block_referenced_by[block_num] = inode_num;
    }
}

bool check_duplicate_blocks() {
    bool no_duplicates = true;
    
    // Create an array to track the inodes referencing each block
    int block_references[TOTAL_BLOCKS][INODE_COUNT];
    int reference_counts[TOTAL_BLOCKS];
    
    memset(reference_counts, 0, sizeof(reference_counts));
    
    // Count references to each block
    for (int i = 0; i < INODE_COUNT; i++) {
        if (is_valid_inode(i)) {
            // Check direct blocks
            for (int j = 0; j < 12; j++) {
                uint32_t block_num = inodes[i].direct_blocks[j];
                if (block_num != 0 && block_num < TOTAL_BLOCKS) {
                    block_references[block_num][reference_counts[block_num]] = i;
                    reference_counts[block_num]++;
                }
            }
            
            // Check indirect block
            if (inodes[i].indirect_block != 0 && inodes[i].indirect_block < TOTAL_BLOCKS) {
                block_references[inodes[i].indirect_block][reference_counts[inodes[i].indirect_block]] = i;
                reference_counts[inodes[i].indirect_block]++;
                
                // Read the indirect block
                uint32_t indirect_entries[BLOCK_SIZE / sizeof(uint32_t)];
                fseek(fs_image, inodes[i].indirect_block * BLOCK_SIZE, SEEK_SET);
                fread(indirect_entries, BLOCK_SIZE, 1, fs_image);
                
                // Check each referenced block
                for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                    uint32_t block_num = indirect_entries[j];
                    if (block_num != 0 && block_num < TOTAL_BLOCKS) {
                        block_references[block_num][reference_counts[block_num]] = i;
                        reference_counts[block_num]++;
                    }
                }
            }
            
            // For simplicity, we're not checking double and triple indirect blocks
        }
    }
    
    // Check for blocks with multiple references
    for (int i = superblock.data_block_start; i < TOTAL_BLOCKS; i++) {
        if (reference_counts[i] > 1) {
            printf("Error: Block %d is referenced by multiple inodes: ", i);
            for (int j = 0; j < reference_counts[i]; j++) {
                printf("%d ", block_references[i][j]);
            }
            printf("\n");
            no_duplicates = false;
            errors_found++;
        }
    }
    
    return no_duplicates;
}

bool check_bad_blocks() {
    bool no_bad_blocks = true;
    
    // Check for blocks with indices outside valid range
    for (int i = 0; i < INODE_COUNT; i++) {
        if (is_valid_inode(i)) {
            // Check direct blocks
            for (int j = 0; j < 12; j++) {
                uint32_t block_num = inodes[i].direct_blocks[j];
                if (block_num != 0 && (block_num < superblock.data_block_start || block_num >= TOTAL_BLOCKS)) {
                    printf("Error: Inode %d has direct block %d with invalid block number %u\n", 
                           i, j, block_num);
                    no_bad_blocks = false;
                    errors_found++;
                }
            }
            
            // Check indirect block
            if (inodes[i].indirect_block != 0) {
                if (inodes[i].indirect_block < superblock.data_block_start || 
                    inodes[i].indirect_block >= TOTAL_BLOCKS) {
                    printf("Error: Inode %d has invalid indirect block number %u\n", 
                           i, inodes[i].indirect_block);
                    no_bad_blocks = false;
                    errors_found++;
                } else {
                    // Read the indirect block
                    uint32_t indirect_entries[BLOCK_SIZE / sizeof(uint32_t)];
                    fseek(fs_image, inodes[i].indirect_block * BLOCK_SIZE, SEEK_SET);
                    fread(indirect_entries, BLOCK_SIZE, 1, fs_image);
                    
                    // Check each referenced block
                    for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                        uint32_t block_num = indirect_entries[j];
                        if (block_num != 0 && (block_num < superblock.data_block_start || block_num >= TOTAL_BLOCKS)) {
                            printf("Error: Inode %d has indirect entry %d with invalid block number %u\n", 
                                   i, j, block_num);
                            no_bad_blocks = false;
                            errors_found++;
                        }
                    }
                }
            }
            
            // For simplicity, we're not checking double and triple indirect blocks
        }
    }
    
    return no_bad_blocks;
}

bool fix_errors() {
    // Fix superblock errors
    if (superblock.magic != MAGIC_NUMBER) {
        superblock.magic = MAGIC_NUMBER;
        errors_fixed++;
    }
    
    if (superblock.block_size != BLOCK_SIZE) {
        superblock.block_size = BLOCK_SIZE;
        errors_fixed++;
    }
    
    if (superblock.total_blocks != TOTAL_BLOCKS) {
        superblock.total_blocks = TOTAL_BLOCKS;
        errors_fixed++;
    }
    
    if (superblock.inode_bitmap_block != 1) {
        superblock.inode_bitmap_block = 1;
        errors_fixed++;
    }
    
    if (superblock.data_bitmap_block != 2) {
        superblock.data_bitmap_block = 2;
        errors_fixed++;
    }
    
    if (superblock.inode_table_start != 3) {
        superblock.inode_table_start = 3;
        errors_fixed++;
    }
    
    if (superblock.data_block_start != 8) {
        superblock.data_block_start = 8;
        errors_fixed++;
    }
    
    if (superblock.inode_size != INODE_SIZE) {
        superblock.inode_size = INODE_SIZE;
        errors_fixed++;
    }
    
    if (superblock.inode_count != INODE_COUNT) {
        superblock.inode_count = INODE_COUNT;
        errors_fixed++;
    }
    
    // Fix inode bitmap inconsistencies
    for (int i = 0; i < INODE_COUNT; i++) {
        bool valid = is_valid_inode(i);
        int bit_value = get_bit(inode_bitmap, i);
        
        if (bit_value && !valid) {
            // Inode is marked as used but is not valid - clear the bit
            clear_bit(inode_bitmap, i);
            errors_fixed++;
        } else if (!bit_value && valid) {
            // Inode is valid but not marked as used - set the bit
            set_bit(inode_bitmap, i);
            errors_fixed++;
        }
    }
    
    // Fix data bitmap inconsistencies
    for (int i = superblock.data_block_start; i < TOTAL_BLOCKS; i++) {
        int bit_index = i - superblock.data_block_start;
        int bit_value = get_bit(data_bitmap, bit_index);
        
        if (bit_value && !block_referenced[i]) {
            // Block is marked as used but not referenced - clear the bit
            clear_bit(data_bitmap, bit_index);
            errors_fixed++;
        } else if (!bit_value && block_referenced[i]) {
            // Block is referenced but not marked as used - set the bit
            set_bit(data_bitmap, bit_index);
            errors_fixed++;
        }
    }
    
    // Fix bad block references
    for (int i = 0; i < INODE_COUNT; i++) {
        if (is_valid_inode(i)) {
            // Fix direct blocks
            for (int j = 0; j < 12; j++) {
                uint32_t block_num = inodes[i].direct_blocks[j];
                if (block_num != 0 && (block_num < superblock.data_block_start || block_num >= TOTAL_BLOCKS)) {
                    // Invalid block reference - set to 0
                    inodes[i].direct_blocks[j] = 0;
                    errors_fixed++;
                }
            }
            
            // Fix indirect block
            if (inodes[i].indirect_block != 0) {
                if (inodes[i].indirect_block < superblock.data_block_start || 
                    inodes[i].indirect_block >= TOTAL_BLOCKS) {
                    // Invalid indirect block reference - set to 0
                    inodes[i].indirect_block = 0;
                    errors_fixed++;
                } else {
                    // Read the indirect block
                    uint32_t indirect_entries[BLOCK_SIZE / sizeof(uint32_t)];
                    fseek(fs_image, inodes[i].indirect_block * BLOCK_SIZE, SEEK_SET);
                    fread(indirect_entries, BLOCK_SIZE, 1, fs_image);
                    
                    bool indirect_modified = false;
                    
                    // Fix invalid references in the indirect block
                    for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                        uint32_t block_num = indirect_entries[j];
                        if (block_num != 0 && (block_num < superblock.data_block_start || block_num >= TOTAL_BLOCKS)) {
                            // Invalid block reference - set to 0
                            indirect_entries[j] = 0;
                            indirect_modified = true;
                            errors_fixed++;
                        }
                    }
                    
                    // Write back the indirect block if modified
                    if (indirect_modified) {
                        fseek(fs_image, inodes[i].indirect_block * BLOCK_SIZE, SEEK_SET);
                        fwrite(indirect_entries, BLOCK_SIZE, 1, fs_image);
                    }
                }
            }
        }
    }
    
    // Write back the fixed superblock, bitmaps, and inodes
    write_superblock();
    write_bitmaps();
    write_inodes();
    
    return true;
}

void write_superblock() {
    fseek(fs_image, 0, SEEK_SET);
    fwrite(&superblock, sizeof(superblock_t), 1, fs_image);
}

void write_bitmaps() {
    // Write the inode bitmap
    fseek(fs_image, superblock.inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, BLOCK_SIZE, 1, fs_image);
    
    // Write the data bitmap
    fseek(fs_image, superblock.data_bitmap_block * BLOCK_SIZE, SEEK_SET);
    fwrite(data_bitmap, BLOCK_SIZE, 1, fs_image);
}

void write_inodes() {
    // Write each inode back to the inode table
    for (int i = 0; i < INODE_COUNT; i++) {
        fseek(fs_image, superblock.inode_table_start * BLOCK_SIZE + i * INODE_SIZE, SEEK_SET);
        fwrite(&inodes[i], sizeof(inode_t), 1, fs_image);
    }
}

int get_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    return (bitmap[byte_index] >> bit_offset) & 1;
}

void set_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}

void clear_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    bitmap[byte_index] &= ~(1 << bit_offset);
}