/****************************************************************************
 * examplex/mtdrwb/mtdrwb_main.c
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/mtd/mtd.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>

#ifdef CONFIG_EXAMPLES_MTDRWB

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Configuration ************************************************************/

#if !defined(CONFIG_MTD_WRBUFFER) && !defined(CONFIG_MTD_READAHEAD)
#  error CONFIG_MTD_WRBUFFER or CONFIG_MTD_READAHEAD must be selected
#endif

/* The default is to use the RAM MTD device at drivers/mtd/rammtd.c.  But
 * an architecture-specific MTD driver can be used instead by defining
 * CONFIG_EXAMPLES_MTDRWB_ARCHINIT.  In this case, the initialization logic
 * will call mtdrwb_archinitialize() to obtain the MTD driver instance.
 */

#ifndef CONFIG_EXAMPLES_MTDRWB_ARCHINIT

/* Make sure that the RAM MTD driver is enabled */

#  ifndef CONFIG_RAMMTD
#    error "CONFIG_RAMMTD is required without CONFIG_EXAMPLES_MTDRWB_ARCHINIT"
#  endif

/* This must exactly match the default configuration in drivers/mtd/rammtd.c */

#  ifndef CONFIG_RAMMTD_ERASESIZE
#    define CONFIG_RAMMTD_ERASESIZE 4096
#  endif

/* Given the ERASESIZE, CONFIG_EXAMPLES_MTDRWB_NEBLOCKS will determine the
 * size of the RAM allocation needed.
 */

#  ifndef CONFIG_EXAMPLES_MTDRWB_NEBLOCKS
#    define CONFIG_EXAMPLES_MTDRWB_NEBLOCKS (32)
#  endif

#  undef MTDRWB_BUFSIZE
#  define MTDRWB_BUFSIZE \
    (CONFIG_RAMMTD_ERASESIZE * CONFIG_EXAMPLES_MTDRWB_NEBLOCKS)

#endif

/* Debug ********************************************************************/
#if defined(CONFIG_DEBUG) && defined(CONFIG_DEBUG_FS)
#  define message    syslog
#  define msgflush()
#else
#  define message    printf
#  define msgflush() fflush(stdout);
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mtdweb_filedesc_s
{
  FAR char *name;
  bool deleted;
  size_t len;
  uint32_t crc;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/
/* Pre-allocated simulated flash */

#ifndef CONFIG_EXAMPLES_MTDRWB_ARCHINIT
static uint8_t g_simflash[MTDRWB_BUFSIZE];
#endif

/****************************************************************************
 * External Functions
 ****************************************************************************/

#ifdef CONFIG_EXAMPLES_MTDRWB_ARCHINIT
extern FAR struct mtd_dev_s *mtdrwb_archinitialize(void);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtdrwb_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int mtdrwb_main(int argc, char *argv[])
#endif
{
  FAR struct mtd_dev_s *mtdraw;
  FAR struct mtd_dev_s *mtdrwb;
  FAR struct mtd_geometry_s geo;
  FAR uint32_t *buffer;
  ssize_t nbytes;
  off_t nblocks;
  off_t offset;
  off_t check;
  off_t sectoff;
  off_t seekpos;
  unsigned int blkpererase;
  int fd;
  int i;
  int j;
  int k;
  int ret;

  /* Create and initialize a RAM MTD FLASH driver instance */

#ifdef CONFIG_EXAMPLES_MTDRWB_ARCHINIT
  mtdraw = mtdrwb_archinitialize();
#else
  mtdraw = rammtd_initialize(g_simflash, MTDRWB_BUFSIZE);
#endif
  if (!mtdraw)
    {
      message("ERROR: Failed to create RAM MTD instance\n");
      msgflush();
      exit(1);
    }

  /* Perform the IOCTL to erase the entire FLASH part */

  ret = mtdraw->ioctl(mtdraw, MTDIOC_BULKERASE, 0);
  if (ret < 0)
    {
      message("ERROR: MTDIOC_BULKERASE ioctl failed: %d\n", ret);
    }

  /* Initialize to support buffering on the MTD device */

  mtdrwb = mtd_rwb_initialize(mtdraw);
  if (!mtdrwb)
    {
      message("ERROR: Failed to create RAM MTD R/W buffering\n");
      msgflush();
      exit(2);
    }

  /* Initialize to provide an FTL block driver on the MTD FLASH interface.
   *
   * NOTE:  We could just skip all of this FTL and BCH stuff.  We could
   * instead just use the MTD drivers bwrite and bread to perform this
   * test.  Creating the character drivers, however, makes this test more
   * interesting.
   */

  ret = ftl_initialize(0, mtdrwb);
  if (ret < 0)
    {
      message("ERROR: ftl_initialize /dev/mtdblock0 failed: %d\n", ret);
      msgflush();
      exit(3);
    }

  /* Now create a character device on the block device */

  ret = bchdev_register("/dev/mtdblock0", "/dev/mtd0", false);
  if (ret < 0)
    {
      message("ERROR: bchdev_register /dev/mtd0 failed: %d\n", ret);
      msgflush();
      exit(4);
    }

  /* Get the geometry of the FLASH device */

  ret = mtdrwb->ioctl(mtdrwb, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
    {
      fdbg("ERROR: mtdrwb->ioctl failed: %d\n", ret);
      exit(5);
    }

  message("Flash Geometry:\n");
  message("  blocksize:      %lu\n", (unsigned long)geo.blocksize);
  message("  erasesize:      %lu\n", (unsigned long)geo.erasesize);
  message("  neraseblocks:   %lu\n", (unsigned long)geo.neraseblocks);

  blkpererase = geo.erasesize / geo.blocksize;
  message("  blkpererase:    %u\n", blkpererase);

  nblocks = geo.neraseblocks * blkpererase;
  message("  nblocks:        %lu\n", (unsigned long)nblocks);

  /* Allocate a buffer */

  buffer = (FAR uint32_t *)malloc(geo.blocksize);
  if (!buffer)
    {
      message("ERROR: failed to allocate a sector buffer\n");
      msgflush();
      exit(6);
    }

  /* Open the MTD FLASH character driver for writing */

  fd = open("/dev/mtd0", O_WRONLY);
  if (fd < 0)
    {
      message("ERROR: open /dev/mtd0 failed: %d\n", errno);
      msgflush();
      exit(7);
    }

  /* Now write the offset into every block */

  message("Initializing media:\n");

  offset = 0;
  for (i = 0; i < geo.neraseblocks; i++)
    {
      for (j = 0; j < blkpererase; j++)
        {
          /* Fill the block with the offset */

          for (k = 0; k < geo.blocksize / sizeof(uint32_t); k++)
            {
              buffer[k] = offset;
              offset += 4;
            }

          /* And write it using the character driver */

          nbytes = write(fd, buffer, geo.blocksize);
          if (nbytes < 0)
            {
              message("ERROR: write to /dev/mtd0 failed: %d\n", errno);
              msgflush();
              exit(8);
            }
        }
    }

  close(fd);

  /* Open the MTD character driver for writing */

  fd = open("/dev/mtd0", O_RDWR);
  if (fd < 0)
    {
      message("ERROR: open /dev/mtd0 failed: %d\n", errno);
      msgflush();
      exit(9);
    }

  /* Now verify the offset in every block */

  check = offset;
  sectoff = 0;

  for (j = 0; j < nblocks; j++)
    {
#if 0 /* Too much */
      message("  block=%u offset=%lu\n", j, (unsigned long) check);
#endif
      /* Seek to the next read position */

      seekpos = lseek(fd, sectoff, SEEK_SET);
      if (seekpos != sectoff)
        {
          message("ERROR: lseek to offset %ld failed: %d\n",
                   (unsigned long)sectoff, errno);
          msgflush();
          exit(10);
        }

      /* Read the next block into memory */

      nbytes = read(fd, buffer, geo.blocksize);
      if (nbytes < 0)
        {
          message("ERROR: read from /dev/mtd0 failed: %d\n", errno);
          msgflush();
          exit(11);
        }
      else if (nbytes == 0)
        {
          message("ERROR: Unexpected end-of file in /dev/mtd0\n");
          msgflush();
          exit(12);
        }
      else if (nbytes != geo.blocksize)
        {
          message("ERROR: Unexpected read size from /dev/mtd0 : %ld\n",
                  (unsigned long)nbytes);
          msgflush();
          exit(13);
        }

      /* Since we forced the size of the partition to be an even number
       * of erase blocks, we do not expect to encounter the end of file
       * indication.
       */

     else if (nbytes == 0)
        {
          message("ERROR: Unexpected end of file on /dev/mtd0\n");
          msgflush();
          exit(14);
        }

     /* This is not expected at all */

     else if (nbytes != geo.blocksize)
       {
          message("ERROR: Short read from /dev/mtd0 failed: %lu\n",
                  (unsigned long)nbytes);
          msgflush();
          exit(15);
        }

      /* Verify the offsets in the block */

      for (k = 0; k < geo.blocksize / sizeof(uint32_t); k++)
        {
          if (buffer[k] != check)
            {
              message("ERROR: Bad offset %lu, expected %lu\n",
                      (long)buffer[k], (long)check);
              msgflush();
              exit(16);
            }

          /* Invert the value to indicate that we have verified
           * this value.
           */

          buffer[k] = ~check;
          check += sizeof(uint32_t);
        }

      /* Seek to the next write position */

      seekpos = lseek(fd, sectoff, SEEK_SET);
      if (seekpos != sectoff)
        {
          message("ERROR: lseek to offset %ld failed: %d\n",
                  (unsigned long)sectoff, errno);
          msgflush();
          exit(17);
        }

      /* Now write the block back to FLASH with the modified value */

      nbytes = write(fd, buffer, geo.blocksize);
      if (nbytes < 0)
        {
          message("ERROR: write to /dev/mtd0 failed: %d\n", errno);
          msgflush();
          exit(18);
        }
      else if (nbytes != geo.blocksize)
        {
          message("ERROR: Unexpected write size to /dev/mtd0 : %ld\n",
                  (unsigned long)nbytes);
          msgflush();
          exit(19);
        }

      /* Get the offset to the next block */

      sectoff += geo.blocksize;
    }

  /* Try reading one more time.  We should get the end of file */

  nbytes = read(fd, buffer, geo.blocksize);
  if (nbytes != 0)
    {
      message("ERROR: Expected end-of-file from /dev/mtd0 failed: %d %d\n",
              nbytes, errno);
      msgflush();
      exit(20);
    }

  close(fd);

  /* And exit without bothering to clean up */

  message("PASS: Everything looks good\n");
  msgflush();
  return 0;
}

#endif /* CONFIG_EXAMPLES_MTDRWB */