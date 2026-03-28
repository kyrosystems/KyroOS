# POSIX Commands in KyroOS

KyroOS includes a comprehensive set of POSIX-like commands for system management and file operations.

## File Operations

### cp - Copy Files

```
cp <source> <destination>
```

Copy files from source to destination.

**Example:**
```
> cp /bin/hello /root/hello_backup
```

### mv - Move/Rename Files

```
mv <source> <destination>
```

Move or rename files.

**Example:**
```
> mv /root/oldname.txt /root/newname.txt
```

### rm - Remove Files

```
rm <file>
```

Delete files.

**Example:**
```
> rm /root/unwanted.txt
```

### cat - Concatenate and Display Files

```
cat <file>
```

Display file contents.

**Example:**
```
> cat /docs/readme.txt
```

### ls - List Directory Contents

```
ls [directory]
```

List files in a directory.

**Example:**
```
> ls /bin
> ls /
```

### mkdir - Create Directory

```
mkdir <directory>
```

Create a new directory.

**Example:**
```
> mkdir /root/myfiles
```

### rmdir - Remove Directory

```
rmdir <directory>
```

Remove an empty directory.

**Example:**
```
> rmdir /root/emptydir
```

### touch - Create Empty File

```
touch <file>
```

Create an empty file or update timestamp.

**Example:**
```
> touch /root/newfile.txt
```

### cd - Change Directory

```
cd [directory]
```

Change current directory.

**Example:**
```
> cd /bin
> cd ..
> cd /
```

### pwd - Print Working Directory

```
pwd
```

Show current directory path.

**Example:**
```
> pwd
/
```

## Text Processing

### grep - Search Text Pattern

```
grep <pattern> <file>
```

Search for a pattern in a file.

**Example:**
```
> grep "error" /var/log/system.log
```

### head - Display First Lines

```
head [-n N] <file>
```

Display first N lines of a file (default: 10).

**Example:**
```
> head -n 5 /var/log/system.log
```

### tail - Display Last Lines

```
tail [-n N] <file>
```

Display last N lines of a file (default: 10).

**Example:**
```
> tail -n 20 /var/log/system.log
```

### wc - Word Count

```
wc <file>
```

Count lines, words, and bytes.

**Example:**
```
> wc /docs/readme.txt
```

### sort - Sort Lines

```
sort <file>
```

Sort lines alphabetically.

**Example:**
```
> sort /etc/hosts
```

### uniq - Filter Duplicate Lines

```
uniq
```

Filter adjacent duplicate lines (reads from stdin).

**Example:**
```
> sort /var/log/system.log | uniq
```

### find - Search for Files

```
find <path> -name <pattern>
```

Search for files by name.

**Example:**
```
> find / -name "*.txt"
```

## System Information

### uname - System Information

```
uname [-a|-s|-n|-r|-v|-m|-p]
```

Display system information.

**Options:**
- `-a` - All information
- `-s` - Kernel name
- `-n` - Node name (hostname)
- `-r` - Release version
- `-v` - Kernel version
- `-m` - Machine hardware
- `-p` - Processor type

**Example:**
```
> uname -a
KyroOS localhost 26.03.12 Titanium x86_64 x86_64
```

### hostname - System Hostname

```
hostname [new_hostname]
```

Get or set the system hostname.

**Example:**
```
> hostname
localhost
> hostname mykyroos
```

### whoami - Current User

```
whoami
```

Display current username.

**Example:**
```
> whoami
root
```

### id - User Identity

```
id
```

Display user and group IDs.

**Example:**
```
> id
uid=0(root) gid=0(root) groups=0(root)
```

### groups - Group Memberships

```
groups
```

Display user groups.

**Example:**
```
> groups
root
```

### users - Logged In Users

```
users
```

List logged in users.

**Example:**
```
> users
root
```

### uptime - System Uptime

```
uptime
```

Display system uptime.

**Example:**
```
> uptime
up 0 hours, 15 minutes, 32 seconds
```

### date - Current Date/Time

```
date
```

Display current date and time.

**Example:**
```
> date
Wed Mar 18 14:30:45 2026
```

## Process Management

### ps - Process Status

```
ps
```

Display running processes.

**Example:**
```
> ps
PID     State
---     -----
0       RUNNING
1       READY
```

### kill - Send Signal to Process

```
kill [-signal] <pid>
```

Send signal to a process.

**Signals:**
- `-1` - HUP (Hangup)
- `-9` - KILL (Force kill)
- `-15` - TERM (Terminate)

**Example:**
```
> kill -9 1234
```

### killall - Kill Process by Name

```
killall <process_name>
```

Kill all processes with given name.

**Example:**
```
> killall idle
```

### nice - Run with Priority

```
nice [-n priority] <command>
```

Run command with specified priority.

**Example:**
```
> nice -n 5 /bin/heavy_task
```

### renice - Change Process Priority

```
renice <priority> <pid>
```

Change priority of running process.

**Example:**
```
> renice 10 1234
```

## Disk Management

### df - Disk Free Space

```
df
```

Display filesystem disk space usage.

**Example:**
```
> df
Filesystem      Size    Used   Available  Use%  Mounted on
kyrofs         100M     25M       75M   25%   /
```

### du - Disk Usage

```
du
```

Display directory space usage.

**Example:**
```
> du
```

### mount - Mount Filesystem

```
mount <device> <mountpoint> [fstype]
```

Mount a filesystem.

**Example:**
```
> mount /dev/sda1 /mnt/data kyrofs
```

### umount - Unmount Filesystem

```
umount <mountpoint>
```

Unmount a filesystem.

**Example:**
```
> umount /mnt/data
```

### fdisk - Partition Editor

```
fdisk <device>
```

Display partition information.

**Example:**
```
> fdisk /dev/sda
```

### mkfs - Create Filesystem

```
mkfs [-t fstype] <device>
```

Create a filesystem on a device.

**Example:**
```
> mkfs -t kyrofs /dev/sda1
```

## Shell Utilities

### echo - Print Text

```
echo <text>
```

Print text to stdout.

**Example:**
```
> echo "Hello, KyroOS!"
```

### env - Environment Variables

```
env
```

Display environment variables.

**Example:**
```
> env
PATH=/bin:/usr/bin
HOME=/root
USER=root
```

### export - Set Environment Variable

```
export <variable=value>
```

Set an environment variable.

**Example:**
```
> export MYVAR=hello
```

### unset - Unset Variable

```
unset <variable>
```

Remove a variable.

**Example:**
```
> unset MYVAR
```

### alias - Command Alias

```
alias [name=value]
```

Create or display command aliases.

**Example:**
```
> alias ll=ls
```

### unalias - Remove Alias

```
unalias <name>
```

Remove a command alias.

**Example:**
```
> unalias ll
```

### history - Command History

```
history
```

Display command history.

**Example:**
```
> history
```

### clear - Clear Screen

```
clear
```

Clear the terminal screen.

**Example:**
```
> clear
```

### reset - Reset Terminal

```
reset
```

Reset the terminal.

**Example:**
```
> reset
```

### sleep - Delay Execution

```
sleep <seconds>
```

Pause for specified seconds.

**Example:**
```
> sleep 5
```

### time - Measure Execution Time

```
time <command>
```

Measure command execution time.

**Example:**
```
> time /bin/benchmark
```

### yes - Repeated Output

```
yes [string]
```

Repeatedly output a string.

**Example:**
```
> yes
y
y
y
...
```

### true - Always Succeed

```
true
```

Do nothing, return success.

**Example:**
```
> true
```

### false - Always Fail

```
false
```

Do nothing, return failure.

**Example:**
```
> false
```

### test - Test Expression

```
test <expression>
```

Evaluate expression.

**Example:**
```
> test -f /bin/shell
```

### sync - Sync Filesystems

```
sync
```

Flush filesystem buffers.

**Example:**
```
> sync
```

## System Control

### reboot - Reboot System

```
reboot
```

Reboot the system.

**Example:**
```
> reboot
```

### halt - Halt System

```
halt
```

Halt the system.

**Example:**
```
> halt
```

### poweroff - Power Off System

```
poweroff
```

Power off the system.

**Example:**
```
> poweroff
```

## File Permissions

### chmod - Change Permissions

```
chmod <mode> <file>
```

Change file permissions.

**Example:**
```
> chmod 755 /bin/script
```

### ln - Create Link

```
ln [-s] <target> <link_name>
```

Create a file link.

**Options:**
- `-s` - Create symbolic link

**Example:**
```
> ln -s /bin/shell /bin/sh
```

## Driver Management

### lsmod - List Modules

```
lsmod
```

List loaded kernel modules.

**Example:**
```
> lsmod
Loaded modules:
----------------
  hello_module       @ 0xFFFF80001000  [1 refs]
```

### insmod - Insert Module

```
insmod <module.kmod>
```

Load a kernel module.

**Example:**
```
> insmod /bin/network.kmod
```

### rmmod - Remove Module

```
rmmod <module_name>
```

Unload a kernel module.

**Example:**
```
> rmmod hello_module
```
