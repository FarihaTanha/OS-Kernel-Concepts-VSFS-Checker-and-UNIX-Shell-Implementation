# OS-Kernel-Concepts-VSFS-Checker-and-UNIX-Shell-Implementation
CSE321 Operating Systems Projects This repository contains two core projects developed for the CSE321 course, focusing on operating systems principles, system calls and file system integrity.

PROJECT-1: Simple UNIX Shell A custom UNIX shell implemented in C that mimics basic terminal functionality and manages process lifecycles.

Key features:

1.Core Functionality: Supports standard Linux commands such as pwd using fork and execvp system calls.

2.Built-in Commands: Includes support for cd to change directories, history to track previous commands and exit to close the shell.

3.Redirection: Handles input and output redirection using <, > and >> with the dup2 system call.

4.Piping: Supports complex command pipelines with multiple pipes to connect processes.

5.Logical Operators: Implements inline command separation via semicolons and logical sequence execution with AND operators.

6.Signal Handling: Uses sigaction to intercept CTRL+C signals, ensuring currently running commands terminate without crashing the shell.

PROJECT-2: VSFS Consistency Checker A file system consistency checker (vsfsck) designed to verify and repair a Very Simple File System image. 

Key features:

1.Superblock Validation: Verifies the integrity of the superblock, checking the magic number 0xD34D, block size 4096 and total blocks 64.

2.Inode Consistency: Checks the inode bitmap to ensure each bit corresponds to a valid inode with links greater than 0 and a deletion time of 0.

3.Data Bitmap Checker: Ensures every block marked as used in the bitmap is referenced by an inode and vice versa.

4.Duplicate Reference Detection: Identifies blocks that are erroneously referenced by multiple inodes.

5.Bad Block Detection: Scans for block indices pointing outside the valid range of 8 to 63.

6.Automated Repair: Includes a fix function that automatically corrects structural errors and writes the updated metadata back to the disk image
