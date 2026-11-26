/*
 * MorseOS files: what the band knows and lets you configure, seen from the PC as small text
 * files (./lt716 fs ls|cat|write, ./lt716 fs mount <dir>). The Linux /proc and /sys feeling,
 * with every shortcut that keeps the band side small:
 *   - a fixed table of files, built with the firmware: no create, delete, rename, permissions;
 *     directories exist only as the "/" in the names
 *   - a file is a function that prints its text when it is read (nothing is stored), and
 *     optionally one that parses a value written to it (one line, up to 39 bytes)
 *   - one value per file, plain text, like sysfs; series are CSV files (/log/...)
 *   - RAM only: what is written is lost on reboot (flash comes in a later version)
 *
 * Protocol, on the dumper characteristics (DUMP_CMD writes, DUMP_DATA notifications):
 *   40 <id> <off u16 LE>          read file id from off. File 0 is the list of files:
 *                                 one line "<rw> <path>" per file, file id = line number
 *   41 <id> <off> <bytes...>      write: bytes at off in the write buffer (up to 17 per packet)
 *   42 <id> <len>                 write: the first len bytes are the new value
 *   reply to 40: <off u16 LE> <id> FC <up to 16 bytes>, until a packet with less than 16
 *                (possibly none) ends the file
 *   reply to 42: FF FF <id> FC <status>   0 ok, 1 no such file, 2 read only, 3 bad value
 * (dump packets start with a flash address < 0x80000, so byte 3 is 0 there: FC is free)
 */
#ifndef FILES_H
#define FILES_H

#include "tl_common.h"

int  files_cmd(const u8 *d, int len);           /* from dump_onWrite: 1 = was a files command */
void files_poll(void);                          /* main loop: the data logs */
void files_pump(int (*tx)(const u8 *p, int n)); /* main loop, connected: send replies;
                                                   tx returns 0 when the packet is queued */

#endif
