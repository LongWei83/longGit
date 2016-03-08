
#include <vxWorks.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysLib.h>
#include <intLib.h>
#include <taskLib.h>
#include <string.h>
#include <logLib.h>
#include <drv/pci/pciConfigLib.h>
#include <arch/ppc/ivPpc.h>
#include "drvD212.h"
#include "plx9656.h"
#include "llrfCommonIO.h"
#include "parameter.h"
#include "drvSup.h"
#include "epicsExport.h"
#include "registryFunction.h"
#include <semLib.h>
#include <dbScan.h>


/* D212 Register Map */
#define    REG_Identifier			0x000

#define    REG_Int_Enable			0x004

#define    REG_RF_Reset				0x00c

#define    REG_Int_Clear			0x008

#define    REG_Alarm				0x010

#define    REG_Drv_Reset			0x014

#define    REG_SG_Mode				0x018

#define    REG_ARC_COUNT			0x01C

#define    REG_Point_Sweep			0x100
#define    REG_Sweep_Option			0x104
#define    REG_AMP_Option			0x108
#define    REG_AMP_FF_Option			0x10C
#define    REG_AMP_Modify_Option		0x110
#define    REG_Tune_Option			0x114
#define    REG_Front_Tune_Option		0x118
#define    REG_Tune_FF_Option			0x11C
#define    REG_Tune_Modify_Option		0x120
#define	   REG_Phase_Option			0x124
#define	   REG_Phase_FF_Option			0x128
#define	   REG_Phase_Modify_Option		0x12C

#define    REG_Fix_Frequency_Set		0x200
#define    REG_Work_Period_Set			0x204
#define    REG_AMP_Set				0x208
#define    REG_AMP_Coefficient			0x20C
#define    REG_AMP_P_Set			0x210
#define    REG_AMP_I_Set			0x214
#define    REG_AMP_I_Set1			0x218
#define    REG_AMP_I_Set2			0x21C
#define    REG_AMP_I_Set3			0x220
#define    REG_Bias_Set				0x224
#define    REG_Fix_Tuning_Angle			0x228
#define    REG_Tuning_Angle_Offset		0x22C
#define    REG_Tune_P_Set			0x230
#define    REG_Tune_I_Set			0x234
#define    REG_Tune_I_Set1			0x238
#define    REG_Tune_I_Set2			0x23C
#define    REG_Tune_I_Set3			0x240
#define    REG_Front_Bias_Set			0x244
#define    REG_Front_Tune_P_Set			0x248
#define    REG_Front_Tune_I_Set			0x24C
#define    REG_Front_Fix_Tuning_Angle		0x250
#define    REG_Phase_P				0x254
#define    REG_Phase_I				0x258
#define    REG_Initial_Phase			0x25C
#define    REG_FF_Delay				0x260

#define    REG_AMP_Upload			0x300
#define    REG_AMP_Set_Upload			0x304
#define    REG_Tuning_Phase_Upload		0x308
#define    REG_Front_Tuning_Phase_Upload	0x30C
#define    REG_Bias_Upload			0x310
#define    REG_Front_Bias_Upload		0x314
#define    REG_Reserved_1			0x318
#define    REG_Reserved_2			0x31C

static long D212Report(int level);

static D212Card *pCard[8];
static int intTest[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static int intHasConnect[MAX_INT_SUP] = {0, 0, 0, 0};

static int dmaCount = 0;

struct {
        long    number;
        DRVSUPFUN       report;
        DRVSUPFUN       init;
}drvD212 = {
    2,
    D212Report,
    NULL
};
epicsExportAddress (drvet, drvD212);

long D212Report (int level)
{
   int i = 0;
   for (i=0;i<8;i++) 
   {
      /* print a short report */
      printf("Card %d with BDF (%d,%d,%d)\n", pCard[i]->cardNum, pCard[i]->bus, pCard[i]->device, pCard[i]->function);

      /* print additional card information */
      if(level >= 1)
      {
         printf("Bridge PCI Address: 0x%08x\n",  pCard[i]->bridgeAddr);
         printf("FPGA PCI Address: 0x%08x\n", pCard[i]->fpgaAddr);
         printf("Interrupt Line: %d\n", pCard[i]->intLine);
      }

      if(level >= 2)
      {
         /* print more card information */
         printf("Index: %d\n", pCard[i]->index);
         printf("FPGA Version: 0x%08x\n", pCard[i]->fpgaVersion);
         printf("Buffer Address: 0x%08x\n", (unsigned int) pCard[i]->buffer);
         printf("Float Buffer Address: 0x%08x\n", (unsigned int) pCard[i]->floatBuffer);
      }
   }

   return 0;
}

/* The configure function is called from the startup script */
int D212Config (int cardNum, int index)
{
   int bus;
   int device;
   int function;
   unsigned char intLine;
   unsigned int busAddr;

   /* Check card number for sanity */
   if (cardNum < 0)
   {
       fprintf (stderr, "D212Configure: cardNum %d must be >= 0\n",
                cardNum);
       return ERROR;
   } 

   /* Check index for sanity */
   if (index < 0) 
   {
       fprintf (stderr, "D212Configure: index %d must be >= 0\n",index);
       return ERROR;
   }

   /* find D212 card, the actual PCI target is PLX9656 bridge chip */
   if(pciFindDevice(PLX9656_VENDOR_ID, PLX9656_DEVICE_ID, index,   
                 &bus, &device, &function) == ERROR)
   {
       fprintf (stderr, "D212Configure: fail to find D212 index %d\n",
                index);
       return ERROR;
   }
 
   /* Create new card structure */
   pCard[index] = (D212Card*) malloc (sizeof (D212Card));
   if (!(pCard[index])) 
   {
       fprintf (stderr, "D212Config: fail to alloc pCard\n");
       return ERROR;
   }

   pCard[index]->cardNum = cardNum;

   /*BAR0 corresponds to 9656 register*/
   pciConfigInLong (bus, device, function,
                    PCI_CFG_BASE_ADDRESS_0, &busAddr);
   busAddr &= PCI_MEMBASE_MASK;
   pCard[index]->bridgeAddr = busAddr;
 
   /*BAR2 corresponds to FPGA register*/
   pciConfigInLong (bus, device, function,
                    PCI_CFG_BASE_ADDRESS_1, &busAddr);
   busAddr &= PCI_MEMBASE_MASK;
   pCard[index]->fpgaAddr = busAddr;

   /* store BDF and index to card structure */
   pCard[index]->bus = bus;
   pCard[index]->device = device;
   pCard[index]->function = function;
   pCard[index]->index = index;

   /* get interrupt vector */
   pciConfigInByte (bus, device, function,
                    PCI_CFG_DEV_INT_LINE, &intLine);
   pCard[index]->intLine = intLine;

   pCard[index]->fpgaVersion = PCI_IN_LONG(pCard[index]->fpgaAddr + REG_Identifier); 

   /* create DMA0 semphore */
   pCard[index]->semDMA0 = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
   if( pCard[index]->semDMA0 == NULL)
   {
       fprintf(stderr,"create semDMA0 error\n");
       return ERROR;
   }

   /* create DMA1 semphore */
   pCard[index]->semDMA1 = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
   if( pCard[index]->semDMA1 == NULL)
   {
       fprintf(stderr,"create semDMA1 error\n");
       return ERROR;
   }

   /* allocate data buffer */
   pCard[index]->buffer = (int *) calloc (DMA_TRANSFER_NUM, sizeof(int));
   if (!pCard[index]->buffer)
   {
       fprintf (stderr, "D212Config: fail to alloc buffer\n");
       return ERROR;
   }

   /* allocate processed float data buffer */
   pCard[index]->floatBuffer = (float *) calloc (DMA_TRANSFER_NUM+0x800+1, sizeof(float));
   if (!pCard[index]->floatBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc float buffer\n");
       return ERROR;
   }

   if(! intHasConnect[pCard[index]->intLine - PCIE0_INT0_VEC])
   {
      /* connect ISR to interrupt, use intLine as interrupt vector */
      if(intConnect(INUM_TO_IVEC(pCard[index]->intLine), cpciIntISR, pCard[index]->intLine) == ERROR)
      {
         printf("intConnect error: Card %d\tintLine %d\n", pCard[index]->cardNum, pCard[index]->intLine);
         return ERROR;
      }

      /*enable interrupt*/
      if(intEnable(pCard[index]->intLine) == ERROR)
      {
         printf("intEnable error: Card %d\tintLine %d\n", pCard[index]->cardNum, pCard[index]->intLine);
         return ERROR;
      }

      intHasConnect[pCard[index]->intLine - PCIE0_INT0_VEC] = 1;

      printf("Card %d, intLine %d: now intConnect\n\n", pCard[index]->cardNum, pCard[index]->intLine);
   }
   else
   {
      printf("Card %d, intLine %d: intLine has been connected already\n\n", pCard[index]->cardNum, pCard[index]->intLine);
   }

   plx9656Init(pCard[index]);

/*   if( ERROR == taskSpawn("dataProcessTask"+index, 20, VX_FP_TASK, 10000, (FUNCPTR) dataProcess, (int) pCard[index], index, 0, 0, 0, 0, 0, 0, 0, 0))
   {
      printf("Fail to spawn data process task!\n");
   }
*/
   /* print card configuration information */
   printf("Card %d successfully initialized:\n", pCard[index]->cardNum);
   printf("BDF: %d %d %d\n", pCard[index]->bus, pCard[index]->device, pCard[index]->function);
   printf("Index: %d\n", pCard[index]->index);
   printf("Bridge PCI Address: 0x%08x\n", pCard[index]->bridgeAddr);
   printf("FPGA PCI Address: 0x%08x\n", pCard[index]->fpgaAddr);
   printf("Interrupt Line: %d\n", pCard[index]->intLine);
   printf("FPGA Version: 0x%08x\n", pCard[index]->fpgaVersion);
   printf("Buffer Address: 0x%08x\n", (unsigned int) pCard[index]->buffer);
   printf("Float Buffer Address: 0x%08x\n", (unsigned int) pCard[index]->floatBuffer);
   printf("Start IOC!!!\n");

   return 0;
}

void cpciIntISR(int intLine)
{
   int lock = intLock();
   int i = 0;

   /* check which card generate interrupt */
   for (i = 0; i < 8; i++)
   {
      if(intLine == pCard[i]->intLine)
      {
         /* FPGA interrupt */
         if(BRIDGE_REG_READ32(pCard[i]->bridgeAddr, REG_9656_INTCSR) & PLX9656_INTCSR_LINTi_ACTIVE) 
         {
            int_Clear(pCard[i]);
            BRIDGE_REG_WRITE32(pCard[i]->bridgeAddr, REG_9656_DMA0_PCI_ADR, (unsigned int) (pCard[i]->buffer));
            BRIDGE_REG_WRITE32(pCard[i]->bridgeAddr, REG_9656_DMA0_LOCAL_ADR, REG_AMP_Upload);
            BRIDGE_REG_WRITE32(pCard[i]->bridgeAddr, REG_9656_DMA0_SIZE, (WAVEFORM_SIZE + 4)*8);
            BRIDGE_REG_WRITE32(pCard[i]->bridgeAddr, REG_9656_DMA0_DPR, 0x00000008);
            BRIDGE_REG_WRITE32(pCard[i]->bridgeAddr, REG_9656_DMA0_CSR, 0x03);
            intTest[i]++;
         }
         else if(BRIDGE_REG_READ32(pCard[i]->bridgeAddr, REG_9656_INTCSR) & PLX9656_INTCSR_DMA0_INT_ACTIVE)
         {
            BRIDGE_REG_WRITE8(pCard[i]->bridgeAddr, REG_9656_DMA0_CSR, 0x09);
         }
      }
   }
   intUnlock(lock);
}

void dataProcess(D212Card *pCard, int i)
{
   while(1)
   {
      semTake(pCard->semDMA0, WAIT_FOREVER);
      intTest[i]++;
   }
}

void plx9656Init(D212Card *pCard)
{
   int bridgeAddr = pCard->bridgeAddr;

   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_INTCSR, 0x0f0C0900); 
/*   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_INTCSR, 0x0f040900); */

   BRIDGE_REG_WRITE8(bridgeAddr, REG_9656_DMA0_CSR, 0x01); 
/*   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, 0x00020d43); */ 
   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, 0x00020dC3); 
}

void int_Clear (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Int_Clear, OPTION_SET);
}

void int_Enable (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Int_Enable, OPTION_SET);
}

void int_Disable (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Int_Enable, OPTION_CLEAR);
}

int int_Enable_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Int_Enable) == OPTION_SET)
      return 1;
   else
      return 0;
}

void set_Sweep_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Sweep_Option, OPTION_SET);
}

void clear_Sweep_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Sweep_Option, OPTION_CLEAR);
}

int SweepOption_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Sweep_Option) == OPTION_SET)
      return 1;
   else 
      return 0;
}

void set_Work_Period (D212Card* pCard, float period)
{
   unsigned int value;
   value = (unsigned int)(period * CALC_Work_Period_Set_MUL + CALC_Work_Period_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Work_Period_Set, value);
}

float get_Work_Period (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Work_Period_Set) - CALC_Work_Period_Set_ADD) / CALC_Work_Period_Set_MUL;
}

D212Card* getCardStruct (int cardNum)
{
   return pCard[cardNum];
}

void intTestPr(int i){
	printf("%d\n",intTest[i]);
}

void intTestCl(int i){
	intTest[i] = 0;
}


epicsRegisterFunction(intTestPr);
