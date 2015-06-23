/* Copyright 2015, Daniel Cohen
 * Copyright 2015, Esteban Volentini
 * Copyright 2015, Matias Giori
 * Copyright 2015, Franco Salinas
 *
 * This file is part of CIAA Firmware.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** \brief Update Test source file
 **
 ** This test creates a thread for the master task and a thread for the slave.
 ** A loopback transport layer is used to communicate both threads. A certain
 ** amount of data segments are generated by the master. These data segments
 ** are parsed and sent to the slave. In a real application the data segments
 ** would be read from an ELF or S19 file and a suitable parser would be used.
 ** The slave writes the received data on the flash device.
 **/

/** \addtogroup CIAA_Firmware CIAA Firmware
 ** @{ */
/** \addtogroup MTests CIAA Firmware Module Tests
 ** @{ */
/** \addtogroup Update Update Module Tests
 ** @{ */

/*
 * Initials     Name
 * ---------------------------
 * DC           Daniel Cohen
 * EV           Esteban Volentini
 * MG           Matias Giori
 * FS           Franco Salinas
 */

/*
 * modification history (new versions first)
 * -----------------------------------------------------------
 * 20150408 v0.0.1   FS   first initial version
 */

/*==================[inclusions]=============================================*/
#include "os.h"               /* <= operating system header */
#include "ciaaLibs_CircBuf.h"
#include "ciaaPOSIX_assert.h" /* <= assert header */
#include "ciaaPOSIX_stdio.h"  /* <= device handler header */
#include "ciaak.h"            /* <= ciaa kernel header */
#include "UPDT_master.h"
#include "UPDT_packer.h"
#include "UPDT_services.h"
#include "test_protocol_loopback.h"
#include "test_protocol_parser.h"

/*==================[macros and definitions]=================================*/
#define DATA_SIZE 1024
/*==================[internal data declaration]==============================*/

/*==================[internal functions declaration]=========================*/

/*==================[internal data definition]===============================*/
/* binary image to transmit */
static uint8_t input[DATA_SIZE];

/* segments to transmit */
static test_update_parserSegmentType segments[] =
{
   {0x0000, 0x00F8, NULL},
   {0x00F8, 0x0050, NULL},
   {0x0150, 0x0068, NULL},
   {0x0210, 0x0008, NULL},
   {0x0218, 0x0010, NULL},
   {0x0228, 0x0100, NULL},
   {0x0360, 0x00A0, NULL}
};

/* master side */
static uint8_t master_buffer[UPDT_PROTOCOL_PAYLOAD_MAX_SIZE];
static UPDT_packerType packer;
static UPDT_masterType master;
static test_update_loopbackType master_transport;
static test_update_parserType parser;

/* slave side */
static test_update_loopbackType slave_transport;
static int32_t slave_fd = -1;

/*==================[external data definition]===============================*/

/*==================[internal functions definition]==========================*/

static uint32_t generateSegments()
{
   uint32_t i, j;

   /* generate sizes */
   for(i = 0; i < sizeof(segments) / sizeof(segments[0]); ++i)
   {
      ciaaPOSIX_assert(segments[i].address < sizeof(input));

      segments[i].data = &input[segments[i].address];

      /* assign each byte its byte number from the beginning of the
       * segment */
      for(j = 0; j < segments[i].size; ++j)
      {
         segments[i].data[j] = j;
      }
   }
   return sizeof(segments) / sizeof(segments[0]);
}
/*==================[external functions definition]==========================*/
/** \brief Main function
 *
 * This is the main entry point of the software.
 *
 * \return 0
 *
 * \remarks This function never returns. Return value is only to avoid compiler
 *          warnings or errors.
 */
int main(void)
{
   /* Starts the operating system in the Application Mode 1 */
   /* This example has only one Application Mode */
   StartOS(AppMode1);

   /* StartOs shall never returns, but to avoid compiler warnings or errors
    * 0 is returned */
   return 0;
}

/** \brief Error Hook function
 *
 * This function is called from the OS if an OS interface (API) returns an
 * error. Is for debugging proposes. If called this function triggers a
 * ShutdownOs which ends in a while(1).
 *
 * The values:
 *    OSErrorGetServiceId
 *    OSErrorGetParam1
 *    OSErrorGetParam2
 *    OSErrorGetParam3
 *    OSErrorGetRet
 *
 * will provide you the interface, the input parameters and the returned value.
 * For more details see the OSEK specification:
 * http://portal.osek-vdx.org/files/pdf/specs/os223.pdf
 *
 */
void ErrorHook(void)
{
   ciaaPOSIX_printf("ErrorHook was called\n");
   ciaaPOSIX_printf("Service: %d, P1: %d, P2: %d, P3: %d, RET: %d\n", OSErrorGetServiceId(), OSErrorGetParam1(), OSErrorGetParam2(), OSErrorGetParam3(), OSErrorGetRet());
   ShutdownOS(0);
}

/** \brief Initial task
 *
 * This task is started automatically in the application mode 1.
 */
TASK(InitTask)
{
   int32_t ret;
   /* init CIAA kernel and devices */
   ciaak_start();

   ciaaPOSIX_printf("Init Task\n");

   /* initialize loopback transport layers */
   ret = test_update_loopbackInit(&master_transport, MasterTask, MASTER_RECV_EVENT);
   ciaaPOSIX_assert(0 == ret);

   ret = test_update_loopbackInit(&slave_transport, SlaveTask, SLAVE_RECV_EVENT);
   ciaaPOSIX_assert(0 == ret);

   /* connect master and slave */
   test_update_loopbackConnect(&master_transport, &slave_transport);

   /* activate the MasterTask task */
   ActivateTask(MasterTask);

   /* activate the SlaveTask task */
   ActivateTask(SlaveTask);

   /* end InitTask */
   TerminateTask();
}

/** \brief Master Task */
TASK(MasterTask)
{
   int32_t ret;
   ssize_t bytes_packed;
   ssize_t bytes_sent;
   uint32_t total_segments = 0;

   ciaaPOSIX_printf("Master Task\n");

   /* initialize data to send */
   total_segments = generateSegments();

   /* initialize the data parser */
   test_update_parserInit(&parser, segments, total_segments);

   /* initialize the master */
   ret = UPDT_masterInit(&master, (UPDT_ITransportType *) &master_transport);
   ciaaPOSIX_assert(0 == ret);

   /* initialize the packer */
   ret = UPDT_packerInit(&packer, (UPDT_parserType *) &parser, master_buffer, UPDT_PROTOCOL_PAYLOAD_MAX_SIZE);
   ciaaPOSIX_assert(0 == ret);

   /** \todo Handshake */

   do
   {
      /* request a block of packed data */
      bytes_packed = UPDT_packerGet(&packer);
      ciaaPOSIX_assert(bytes_packed >= 0);

      /** \todo encrypt */
      /** \todo calculate signature */

      /* send the packet data */
      bytes_sent =  UPDT_masterSendData(&master, master_buffer, bytes_packed);

   /* it finishes when we send a block of data smaller than the others */
   } while(bytes_sent == UPDT_PROTOCOL_PAYLOAD_MAX_SIZE);

   if(bytes_sent == -1)
   {
      ciaaPOSIX_printf("transmission error\n");
   }
   else
   {
      ciaaPOSIX_printf("transmission successful\n");
   }

   /** \todo send signature */

   /* clear the structure packer */
   UPDT_packerClear(&packer);

   /* clear the master structure */
   UPDT_masterClear(&master);

   TerminateTask();
}

/** \brief Slave Task */
TASK(SlaveTask)
{
   ciaaPOSIX_printf("Slave Task\n");

   /* open flash device */
   slave_fd = ciaaPOSIX_open("/dev/block/fd/0", O_RDWR);
   ciaaPOSIX_assert(0 >= slave_fd);

   /* start the update service */
   UPDT_servicesStart((UPDT_ITransportType *) &slave_transport, slave_fd);

   /* close the device */
   ciaaPOSIX_close(slave_fd);
   TerminateTask();
}

/** @} doxygen end group definition */
/** @} doxygen end group definition */
/** @} doxygen end group definition */
/*==================[end of file]============================================*/

