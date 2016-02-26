
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

static D212Card *firstCard = NULL;
static int dmaCount = 0;
static int intHasConnect[MAX_INT_SUP] = {0, 0, 0, 0};

/******* no int ***********
static int fpgaIntCount = 0;
static int fpgaRunMode = MODE_TEST;
static unsigned int dmaMoment[INTERRUPT_NUM];
static unsigned int swapMoment[INTERRUPT_NUM];
static unsigned int checkMoment[INTERRUPT_NUM];
static unsigned int procMoment[INTERRUPT_NUM];
static unsigned int intMoment[INTERRUPT_NUM];
unsigned int dmaWriteMoment[2];
static float dmaTime[INTERRUPT_NUM];
static float swapTime[INTERRUPT_NUM];
static float checkTime[INTERRUPT_NUM];
static float procTime[INTERRUPT_NUM];
static float intTime[INTERRUPT_NUM];
static float dmaWriteTime;
******************************/

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
   D212Card *pCard;
   for (pCard = firstCard; pCard; pCard = pCard->next) 
   {
      /* print a short report */
      printf("Card %d with BDF (%d,%d,%d)\n", pCard->cardNum, pCard->bus, pCard->device, pCard->function);

      /* print additional card information */
      if(level >= 1)
      {
         printf("Bridge PCI Address: 0x%08x\n",  pCard->bridgeAddr);
         printf("FPGA PCI Address: 0x%08x\n", pCard->fpgaAddr);
         printf("Interrupt Line: %d\n", pCard->intLine);
      }

      if(level >= 2)
      {
         /* print more card information */
         printf("Index: %d\n", pCard->index);
         printf("FPGA Version: 0x%08x\n", pCard->fpgaVersion);
         printf("Buffer Address: 0x%08x\n", (unsigned int) pCard->buffer);
         printf("Float Buffer Address: 0x%08x\n", (unsigned int) pCard->floatBuffer);
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
   float *initData;
   int i;

   D212Card *pCard;
   D212Card *pCardIndex;

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

   /* Find end of card list and check for duplicates */
   for (pCardIndex = firstCard; pCardIndex; pCardIndex = pCardIndex->next)
   {
       if (pCardIndex->cardNum == cardNum) 
       {
           fprintf (stderr, "D212Configure: cardNum %d already in use\n", 
                   cardNum);
           return ERROR;
       }
       if (pCardIndex->index == index)
       {
           fprintf (stderr, "D212Configure: index %d already in use\n",
                   index);
           return ERROR;
       }
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
   pCard = (D212Card*) malloc (sizeof (D212Card));
   if (!pCard) 
   {
       fprintf (stderr, "D212Config: fail to alloc pCard\n");
       return ERROR;
   }

   /* add card struct to link list */
   if (pCardIndex == NULL && firstCard == NULL)
       firstCard = pCard;
   else
   {
       for(pCardIndex=firstCard; pCardIndex->next!=NULL;
                 pCardIndex=pCardIndex->next);
       pCardIndex->next = pCard;
   }

   pCard->next = NULL;
   pCard->cardNum = cardNum;

   /*BAR0 corresponds to 9656 register*/
   pciConfigInLong (bus, device, function,
                    PCI_CFG_BASE_ADDRESS_0, &busAddr);
   busAddr &= PCI_MEMBASE_MASK;
   pCard->bridgeAddr = busAddr;
 
   /*BAR2 corresponds to FPGA register*/
   pciConfigInLong (bus, device, function,
                    PCI_CFG_BASE_ADDRESS_1, &busAddr);
   busAddr &= PCI_MEMBASE_MASK;
   pCard->fpgaAddr = busAddr;

   /* store BDF and index to card structure */
   pCard->bus = bus;
   pCard->device = device;
   pCard->function = function;
   pCard->index = index;

   /* get interrupt vector */
   pciConfigInByte (bus, device, function,
                    PCI_CFG_DEV_INT_LINE, &intLine);
   pCard->intLine = intLine;

   /* create DMA0 semphore */
   pCard->semDMA0 = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
   if( pCard->semDMA0 == NULL)
   {
       fprintf(stderr,"create semDMA0 error\n");
       return ERROR;
   }

   /* create DMA1 semphore */
   pCard->semDMA1 = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
   if( pCard->semDMA1 == NULL)
   {
       fprintf(stderr,"create semDMA1 error\n");
       return ERROR;
   }

   /* allocate data buffer */
   pCard->buffer = (int *) calloc (DMA_TRANSFER_NUM, sizeof(int));
   if (!pCard->buffer)
   {
       fprintf (stderr, "D212Config: fail to alloc buffer\n");
       return ERROR;
   }

   /* allocate processed float data buffer */
   pCard->floatBuffer = (float *) calloc (DMA_TRANSFER_NUM+0x800+1, sizeof(float));
   if (!pCard->floatBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc float buffer\n");
       return ERROR;
   }

   /*allocate error all buffer */
   pCard->errorAllBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->errorAllBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc error all buffer\n");
       return ERROR;
   }
   
   initData = pCard->errorAllBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /*allocate error phase buffer */
   pCard->errorPhaseBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->errorPhaseBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc error all buffer\n");
       return ERROR;
   }
   initData = pCard->errorPhaseBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /*allocate error front buffer */
   pCard->errorFrontBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->errorFrontBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc error all buffer\n");
       return ERROR;
   }
   initData = pCard->errorFrontBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /*allocate error total buffer */
   pCard->errorTotalBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->errorTotalBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc error all buffer\n");
       return ERROR;
   }
   initData = pCard->errorTotalBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /*allocate grid buffer */
   pCard->gridBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->gridBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc grid buffer\n");
       return ERROR;
   }
   initData = pCard->gridBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /*allocate front buffer */
   pCard->frontBuffer = (float *) calloc (WAVEFOMR_NUM+1, sizeof(float));
   if (!pCard->frontBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc front buffer\n");
       return ERROR;
   }
   initData = pCard->frontBuffer;
   for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           initData[i] =  0.0;
      }

   /* allocate writing data buffer */
   pCard->writeBuffer = (int *) calloc (DMA_TRANSFER_NUM, sizeof(int));
   if (!pCard->writeBuffer)
   {
       fprintf (stderr, "D212Config: fail to alloc writing buffer\n");
       return ERROR;
   }
   
   pCard->errorFlag = 0;

   /******* no int ***********
   pCard->interruptNum = 0;
   *********************************/


   scanIoInit(&pCard->ioScanPvt); 

   /* get FPGA version */
   pCard->fpgaVersion = FPGA_REG_READ32(pCard->fpgaAddr, REG_Identifier); 

   /* ensure that each interrupt line of four be connected only once */

   if(! intHasConnect[pCard->intLine - PCIE0_INT0_VEC])
   {
      /* connect ISR to interrupt, use intLine as interrupt vector */
      if(intConnect(INUM_TO_IVEC(pCard->intLine), cpciIntISR, pCard->intLine) == ERROR)
      {
         printf("intConnect error: Card %d\tintLine %d\n", pCard->cardNum, pCard->intLine);
         return ERROR;
      }

      /*enable interrupt*/
      if(intEnable(pCard->intLine) == ERROR)
      {
         printf("intEnable error: Card %d\tintLine %d\n", pCard->cardNum, pCard->intLine);
         return ERROR;
      }

      intHasConnect[pCard->intLine - PCIE0_INT0_VEC] = 1;

      printf("Card %d, intLine %d: now intConnect\n\n", pCard->cardNum, pCard->intLine);
   }
   else
   {
      printf("Card %d, intLine %d: intLine has been connected already\n\n", pCard->cardNum, pCard->intLine);
   }

   /* initialize plx9656 bridge chip */
   plx9656Init(pCard);

   /* start data process task */
   if( ERROR == taskSpawn("dataProcessTask", 20, VX_FP_TASK, 10000, (FUNCPTR) dataProcess, (int) pCard, 0, 0, 0, 0, 0, 0, 0, 0, 0))
   {
      printf("Fail to spawn data process task!\n");
   }

   /* start DMA write task */
   if( ERROR == taskSpawn("dmaWriteTask", 20, VX_FP_TASK, 10000, (FUNCPTR) dmaWrite, (int) pCard, 0, 0, 0, 0, 0, 0, 0, 0, 0)) 
   {
      printf("Fail to spawn dma write task!\n");
   }

   /* print card configuration information */
   printf("Card %d successfully initialized:\n", pCard->cardNum);
   printf("BDF: %d %d %d\n", pCard->bus, pCard->device, pCard->function);
   printf("Index: %d\n", pCard->index);
   printf("Bridge PCI Address: 0x%08x\n", pCard->bridgeAddr);
   printf("FPGA PCI Address: 0x%08x\n", pCard->fpgaAddr);
   printf("Interrupt Line: %d\n", pCard->intLine);
   printf("FPGA Version: 0x%08x\n", pCard->fpgaVersion);
   printf("Buffer Address: 0x%08x\n", (unsigned int) pCard->buffer);
   printf("Float Buffer Address: 0x%08x\n", (unsigned int) pCard->floatBuffer);
   printf("Start IOC!!!\n");

   return 0;
}

/*---------------------Comment for hardware register access--------------------
 * a) CPCI_WRITE8(pCard->bridgeAddr, REG_9656_DMA0_CSR, 0x09); 
 *
 *    Clear DMA0 Interrupt
 *    Following is the equivalent:
 *    regRead8 = CPCI_READ8(pCard->bridgeAddr, REG_9656_DMA0_CSR);
 *    regRead8 |= PLX9656_DMA0_INTERRUPT_CLEAR;
 *    CPCI_WRITE8(pCard->bridgeAddr, REG_9656_DMA0_CSR, regRead8);
 *
 * b) CPCI_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, 0x0f0C0900);
 *
 *    Enable LINTi#
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(pCard->bridgeAddr, REG_9656_INTCSR);
 *    regRead32 |= PLX9656_INTCSR_LINTi_ENABLE;
 *    CPCI_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, regRead32);       
 *
 * c) CPCI_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, 0x0f0C0100);
 *
 *    Disable LINTi#, i.e. Clear Local Interrupt
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(pCard->bridgeAddr, REG_9656_INTCSR);
 *    regRead32 &= ~PLX9656_INTCSR_LINTi_ENABLE;
 *    CPCI_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, regRead32);
 *
 * d) CPCI_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_DPR, 0x00000008);
 *
 *    Select transfers from the Local Bus to the PCI Bus
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(pCard->bridgeAddr, REG_9656_DMA0_DPR);
 *    regRead32 |= PLX9656_DMA0_DIRECT_LOC_TO_PCI;
 *    CPCI_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_DPR, regRead32);
 *
 * e) CPCI_WRITE8(pCard->bridgeAddr, REG_9656_DMA0_CSR, 0x03);
 *
 *    DMA Channel 0 Start
 *    Following is the equivalent:
 *    regRead8 = CPCI_READ8(pCard->bridgeAddr, REG_9656_DMA0_CSR);
 *    regRead8 |= PLX9656_DMA0_START;
 *    CPCI_WRITE8(pCard->bridgeAddr, REG_9656_DMA0_CSR, regRead8);
 *
 * f) CPCI_WRITE32(pCard->fpgaAddr, REG_CONTROL, 0x00000002);
 *
 *    Disable FIFO and Interrupt
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(pCard->fpgaAddr, REG_CONTROL);
 *    regRead32 &= ~ D212_CR_FIFO_ENABLE;
 *    regRead32 &= ~ D212_CR_INT_ENABLE;
 *    CPCI_WRITE32(pCard->fpgaAddr, REG_CONTROL, regRead32);
 *
 *----------------end of Comment for hardware register access------------------
 */

/* interrupt service routine, one for 8 cards, with 4 different parameters */
void cpciIntISR(int intLine)
{
   D212Card *pCard;
   unsigned int tmp32;
   unsigned char tmp8;  
   

   /* check which card generate interrupt */
   for (pCard = firstCard; pCard; pCard = pCard->next)
   {
      if(intLine == pCard->intLine)
      {
         /* FPGA interrupt */
         if(BRIDGE_REG_READ32(pCard->bridgeAddr, REG_9656_INTCSR) & PLX9656_INTCSR_LINTi_ACTIVE) 
         {
            /******* no int ***********
            intMoment[fpgaIntCount] = sysTimeBaseLGet();
            ****************************/

            /* clear FPGA interrupt, i.e. de-assert LINTi line */
            int_Clear(pCard);
            /* read back, avoid spurious interrupt */
  /*          tmp32 = FPGA_REG_READ32(pCard->fpgaAddr, Identifier);  */

            /* disable FPGA interupt, also clear the FPGA interrupt */
  /*          BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, 0x0f0C0100); */
            /* read back, avoid spurious interrupt */
     /*       tmp32 = BRIDGE_REG_READ32(pCard->bridgeAddr, REG_9656_INTCSR);*/  

            /* clear the data buffer */
       /*     memset(pCard->buffer, 0, DMA_TRANSFER_NUM*sizeof(int));
            memset(pCard->floatBuffer, 0, DMA_TRANSFER_NUM*sizeof(float));*/

            /* start DMA Channel 0 transfer, 8k bytes data at once */
            BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_PCI_ADR, (unsigned int) (pCard->buffer));
            BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_LOCAL_ADR, REG_AMP_Upload);
            BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_SIZE, WAVEFORM_SIZE + 4);
            BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_DPR, 0x00000008);
            BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_CSR, 0x03);
            dmaCount++;

            /* read back, avoid spurious interrupt */
            tmp32 = FPGA_REG_READ32(pCard->fpgaAddr, REG_Identifier); 

         }     
         /* DMA Channel 0 interrupt, Transfer Data from hardware to CPU board */
         else if(BRIDGE_REG_READ32(pCard->bridgeAddr, REG_9656_INTCSR) & PLX9656_INTCSR_DMA0_INT_ACTIVE)
         {
            /* clear DMA Channel 0 interrupt */
            BRIDGE_REG_WRITE8(pCard->bridgeAddr, REG_9656_DMA0_CSR, 0x09);
            /* read back, avoid spurious interrupt */
   /*         tmp8 = BRIDGE_REG_READ8(pCard->bridgeAddr, REG_9656_DMA0_CSR);*/

            /* re-enable FPGA interrupt */
     /*       BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_INTCSR, 0x0f0C0900); */

            if(dmaCount < 8)
            {
               /* start DMA Channel 0 transfer, 8k bytes data at once */
               BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_PCI_ADR, (unsigned int) (pCard->buffer + 2049 * dmaCount));
               BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_LOCAL_ADR, REG_AMP_Upload + 4 * dmaCount);
               BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_SIZE, WAVEFORM_SIZE + 4);
               BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_DPR, 0x00000008);
               BRIDGE_REG_WRITE32(pCard->bridgeAddr, REG_9656_DMA0_CSR, 0x03);
               dmaCount++; 
            }
            else
            {
               dmaCount = 0;
               /* synchronize data process task */
               semGive(pCard->semDMA0);
            }
            /* read back, avoid spurious interrupt */
            tmp8 = BRIDGE_REG_READ8(pCard->bridgeAddr, REG_9656_DMA0_CSR);

         }
         /* DMA Channel 1 interrupt, Transfer Data from CPU board to hardware */
         else if(BRIDGE_REG_READ32(pCard->bridgeAddr, REG_9656_INTCSR) & PLX9656_INTCSR_DMA1_INT_ACTIVE)
         {
            /* clear DMA Channel 1 interrupt */
            BRIDGE_REG_WRITE8(pCard->bridgeAddr, REG_9656_DMA1_CSR, 0x09);
            /* read back, avoid spurious interrupt */
            tmp8 = BRIDGE_REG_READ8(pCard->bridgeAddr, REG_9656_DMA1_CSR);

            /* synchronize DMA write task */
            semGive(pCard->semDMA1);
         }
      }
   }
}

/*---------------------Comment for hardware register access--------------------
 * a) CPCI_WRITE32(pCard->fpgaAddr, REG_CONTROL, 0x00000007);
 *
 *    Enable FIFO, Period Generation, and Interrupt
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(fpgaAddr, REG_CONTROL);
 *    regRead32 |= ( D212_CR_FIFO_ENABLE |
 *                   D212_CR_PERIOD_GEN |
 *                   D212_CR_INT_ENABLE );
 *    CPCI_WRITE32(fpgaAddr, REG_CONTROL, regRead32);
 *
 *----------------end of Comment for hardware register access------------------
 */

/* data process task, one for each of 8 cards */
void dataProcess(D212Card *pCard)
{
   int i;
   float *pDest;
   int *pSrc;
   float temp;
   float *pTemp1, *pTemp2, *pTemp3;
   UINT originIntHigh;
   UINT originIntLow;

   /* infinite loop, used for data process */
   while(1)
   {
      /* synchronize with ISR */
      semTake(pCard->semDMA0, WAIT_FOREVER); 

      /* process waveform 1 data */
      pDest = pCard->floatBuffer + WF1_FADDR;
      pSrc = pCard->buffer + WF1_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           pDest[i] =  (pSrc[i]>>12) * CALC_WF1_MUL + CALC_WF1_ADD;
      }

      /* process waveform 2 data */
      pDest = pCard->floatBuffer + WF2_FADDR;
      pSrc = pCard->buffer + WF2_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           pDest[i] =  pSrc[i] * CALC_WF2_MUL + CALC_WF2_ADD;
      }

      /* calculate amplitude skew with wf1 & wf2, i.e. (wf2 - wf1) / wf1 */
      pSrc = pCard->buffer + WF1_ADDR;
      pTemp2 = pCard->floatBuffer + WF2_FADDR;
      pTemp3 = pCard->errorAllBuffer;
      pDest = pCard->ampSkewBuffer;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           pDest[i] = (pSrc[i]*1.0 - pTemp2[i]) / pTemp2[i] * 100.0;
           if(pCard->errorFlag)
           {
           if(pDest[i] > 0)
           {
               if(pTemp3[i]>0)
                   pTemp3[i] = (pDest[i] > pTemp3[i]) ? pDest[i] : pTemp3[i];
               else
                   pTemp3[i] = (pDest[i] > -1*pTemp3[i]) ? pDest[i] : pTemp3[i];
           }
           else
           {
               if(pTemp3[i]<0)
                   pTemp3[i] = (pDest[i] < pTemp3[i]) ? pDest[i] : pTemp3[i];
               else
                   pTemp3[i] = (-1*pDest[i] > pTemp3[i]) ? pDest[i] : pTemp3[i];
           }
           }
           else
               pTemp3[i] = 0.0;
      }

      /* process waveform 3 data */
      pTemp1 = pCard->errorPhaseBuffer;
      pDest = pCard->floatBuffer + WF3_FADDR;
      pSrc = pCard->buffer + WF3_ADDR;
      for(i=0; i<WAVEFOMR_NUM+1; i++)
      {
           temp =  pSrc[i] * CALC_WF3_MUL + CALC_WF3_ADD;
           
           pDest[i] =  temp;
           if(pCard->errorFlag)
           {
           if(pDest[i] > 0)
           {
               if(pTemp1[i]>0)
                   pTemp1[i] = (pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (pDest[i] > -1*pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           else
           {
               if(pTemp1[i]<0)
                   pTemp1[i] = (pDest[i] < pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (-1*pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           }
           else
               pTemp1[i] = 0.0;
      }

      /* process waveform 4 data */
      pTemp1 = pCard->errorFrontBuffer;
      pDest = pCard->floatBuffer + WF4_FADDR;
      pSrc = pCard->buffer + WF4_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           temp =  pSrc[i] * CALC_WF4_MUL + CALC_WF4_ADD;
           pDest[i] =  temp;
           if(pCard->errorFlag)
           {
           if(pDest[i] > 0)
           {
               if(pTemp1[i]>0)
                   pTemp1[i] = (pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (pDest[i] > -1*pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           else
           {
               if(pTemp1[i]<0)
                   pTemp1[i] = (pDest[i] < pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (-1*pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           }
           else
               pTemp1[i] = 0.0;
      }

      /* process waveform 5 data */
      pTemp1 = pCard->errorTotalBuffer;
      pDest = pCard->floatBuffer + WF5_FADDR;
      pSrc = pCard->buffer + WF5_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           temp =  pSrc[i] * CALC_WF5_MUL + CALC_WF5_ADD;
           pDest[i] =  temp;
           if(pCard->errorFlag)
           {
           if(pDest[i] > 0)
           {
               if(pTemp1[i]>0)
                   pTemp1[i] = (pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (pDest[i] > -1*pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           else
           {
               if(pTemp1[i]<0)
                   pTemp1[i] = (pDest[i] < pTemp1[i]) ? pDest[i] : pTemp1[i];
               else
                   pTemp1[i] = (-1*pDest[i] > pTemp1[i]) ? pDest[i] : pTemp1[i];
           }
           }
           else
               pTemp1[i] = 0.0;
      }

      /* process waveform 6 data */
      pDest = pCard->floatBuffer + WF6_FADDR_A;
      pSrc = pCard->buffer + WF6_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           originIntHigh = ((UINT) (pSrc[i]))>>16;
	   pDest[i] =  originIntHigh * CALC_WF6A_MUL + CALC_WF6A_ADD;
      }

      /* process waveform 6 data */
      pDest = pCard->floatBuffer + WF6_FADDR_B;
      pSrc = pCard->buffer + WF6_ADDR;
      /*pSrc = pCard->buffer + WF6_ADDR;*/
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           originIntLow=((UINT) (pSrc[i]))&0x0000FFFF;
           pDest[i] =  originIntLow * CALC_WF6B_MUL + CALC_WF6B_ADD;
      }

      /* process waveform 7 data */
      
      pDest = pCard->floatBuffer + WF7_FADDR;
      pSrc = pCard->buffer + WF7_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           pDest[i] =  pSrc[i] * CALC_WF7_MUL + CALC_WF7_ADD;
      }

      /* process waveform 8 data */
      pDest = pCard->floatBuffer + WF8_FADDR;
      pSrc = pCard->buffer + WF8_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           pDest[i] =  pSrc[i] * CALC_WF8_MUL + CALC_WF8_ADD;
      }

      /* calculate gridBuffer with wf8 */
      pDest = pCard->gridBuffer;
      pSrc = pCard->buffer + WF8_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           originIntHigh = ((UINT) (pSrc[i]))>>16;
	   pDest[i] =  originIntHigh * CALC_WF8_MUL + CALC_WF8_ADD;
      }

      /* calculate frontBuffer with wf8 */
      pDest = pCard->frontBuffer;
      pSrc = pCard->buffer + WF8_ADDR;
      for(i=1; i<WAVEFOMR_NUM+1; i++)
      {
           originIntLow=((UINT) (pSrc[i]))&0x0000FFFF;
	   pDest[i] =  originIntLow * CALC_WF8_MUL + CALC_WF8_ADD;
      }

      /******* no int ***********
      procMoment[fpgaIntCount] = sysTimeBaseLGet();
      ***************************/

      /* Inform records to retrieve the data */
      scanIoRequest(pCard->ioScanPvt);
/**********************************no int ********************
      FPGA_REG_WRITE32(pCard->fpgaAddr, REG_CONTROL, 0x00000007);
************************************************************/
      /* if no-stop mode, enable fifo and interrupt, go back to next loop */
      /******* no int ***********
      if(fpgaRunMode == MODE_NORMAL)
      {
         FPGA_REG_WRITE32(pCard->fpgaAddr, REG_CONTROL, 0x00000007);
         continue;
      }
      ********************************/

      /*it's time to stop interrupt*/
      /******* no int ***********
      if(fpgaIntCount == intNum - 1)
      {
         fpgaIntCount = 0;
         int_Disable(pCard);

         for(i=0; i<intNum; i++)
         {
            intTime[i] = (i==0) ? 0 : (TIME_DIFF(intMoment[i], intMoment[i-1]))/sysTimeBaseFreqGet();
            dmaTime[i] = (TIME_DIFF(dmaMoment[i], intMoment[i]))/sysTimeBaseFreqGet();
            swapTime[i] = (TIME_DIFF(swapMoment[i], dmaMoment[i]))/sysTimeBaseFreqGet();
            checkTime[i] = (TIME_DIFF(checkMoment[i], swapMoment[i]))/sysTimeBaseFreqGet();
            procTime[i] = (TIME_DIFF(procMoment[i], checkMoment[i]))/sysTimeBaseFreqGet();
         }  
         for(i=0; i<intNum; i++)
         {
            printf("int %3d interval %6.3fms dmaTime %.3fms swapTime %.3fms checkTime %.3fms procTime %.3fms\n", i+1, intTime[i], dmaTime[i], swapTime[i], checkTime[i], procTime[i]);
         }
      }
      else
      {
         fpgaIntCount++;
      }
      ***************************/
   }
}

/* DMA write task, one for each of 8 cards */
void dmaWrite(D212Card *pCard)
{

   /* infinite loop, used for DMA write return */
   while(1)
   {
      /* synchronize with ISR */
      semTake(pCard->semDMA1, WAIT_FOREVER);
      /************* no int **************
      dmaWriteMoment[1] = sysTimeBaseLGet();
      ***********************************/
      /***********************************
      dmaWriteTime = (TIME_DIFF(dmaWriteMoment[1], dmaWriteMoment[0]))/sysTimeBaseFreqGet(); 
       ********************************/

      printf("Data has been written to FPGA\n");
/********************************
      printf("Transfer time is %.3fms\n", dmaWriteTime);
**********************************/
   }
}

/*---------------------Comment for hardware register access--------------------
 * a) CPCI_WRITE32(bridgeAddr, REG_9656_INTCSR, 0x0f0C0900);
 *
 *    Enable PCI Interrupt, LINTi# and DMA0 Interrupt
 *    Disable LINTo#
 *    Following is the equivalent:
 *
 *    regRead32 = CPCI_READ32(bridgeAddr, REG_9656_INTCSR);
 *    regRead32 &= ~PLX9656_INTCSR_LINTo_ENABLE;
 *    regRead32 |= ( PLX9656_INTCSR_PCI_INT_ENABLE |
 *                   PLX9656_INTCSR_LINTi_ENABLE   |
 *                   PLX9656_INTCSR_DMA0_INT_ENABLE|
 *                   PLX9656_INTCSR_DMA1_INT_ENABLE );
 *    CPCI_WRITE32(bridgeAddr, REG_9656_INTCSR, regRead32);
 *
 * b) CPCI_WRITE8(bridgeAddr, REG_9656_DMA0_CSR, 0x01);
 *
 *    Enable DMA Channel 0
 *    Following is the equivalent:
 *    regRead8 = CPCI_READ8(bridgeAddr, REG_9656_DMA0_CSR);
 *    regRead8 |= PLX9656_DMA0_ENABLE;
 *    CPCI_WRITE8(bridgeAddr, REG_9656_DMA0_CSR, regRead8);
 *
 * c) CPCI_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, 0x00020d43);
 *
 *    Enable Local Burst, DMA0 Done Interrupt
 *    Select 32 Data Witth, Constant Local Address, DMA0 Interrupt to INTA#
 *    Following is the equivalent:
 *    regRead32 = CPCI_READ32(bridgeAddr, REG_9656_DMA0_MODE);
 *    regRead32 |= ( PLX9656_DMA0_DATA_WIDTH_32 |
 *                   PLX9656_DMA0_LOC_BURST_ENABLE |
 *                   PLX9656_DMA0_DONE_INT_ENABLE |
 *                   PLX9656_DMA0_LOCAL_ADDR_CONST |
 *                   PLX9656_DMA0_INT_SELECT_INTA );
 *    CPCI_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, regRead32);
 *
 * d) The initialization of DMA Channel 1 is equivalent to Channel 0
 *
 *----------------end of Comment for hardware register access------------------
 */

/* initialize PLX9656 bridge chip */
void plx9656Init(D212Card *pCard)
{
   int bridgeAddr = pCard->bridgeAddr;

   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_INTCSR, 0x0f0C0900); 
/*   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_INTCSR, 0x0f040900); */

   BRIDGE_REG_WRITE8(bridgeAddr, REG_9656_DMA0_CSR, 0x01); 
/*   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, 0x00020d43); */ 
   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_DMA0_MODE, 0x00020dC3); 
        

   BRIDGE_REG_WRITE8(bridgeAddr, REG_9656_DMA1_CSR, 0x01);
   BRIDGE_REG_WRITE32(bridgeAddr, REG_9656_DMA1_MODE, 0x00020d43);
}



/* get card structure by card number */
D212Card* getCardStruct (int cardNum)
{
   D212Card* pCard;
   for (pCard = firstCard; pCard; pCard = pCard->next)
      if (pCard->cardNum == cardNum)
         return pCard;
   return NULL;
}

/* set interrupt number */
/******* no int ***********
void setInterruptNum (D212Card* pCard, int interruptNum)
{
   pCard->interruptNum = interruptNum;
}
**************************/


/* get interrupt number */
/******* no int ***********
int getInterruptNum (D212Card* pCard)
{
   return pCard->interruptNum;
}
*****************************/


/* set run mode of D212*/
/******* no int ***********
void setRunMode (int mode)
{
   fpgaRunMode = mode;
}
***************************/

/* get run mode of D212*/
/******* no int ***********
int getRunMode ()
{
   return fpgaRunMode;
}
**************************/

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
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Int_Enable) == 0xAAAAAAAA)
      return 1;
   else 
      return 0;
}

void point_Sweep_S (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Point_Sweep, OPTION_SET);
}

void point_Sweep_P (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Point_Sweep, OPTION_CLEAR);
}

int point_Sweep_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Point_Sweep) == 0xAAAAAAAA)
      return 1;
   else 
      return 0;
}

int alarm0_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm)  & 0x0F000000) == 0x0E000000)
      return 1;
   else 
      return 0;
}
int alarm1_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0xF0000000) == 0xE0000000)
      return 1;
   else 
      return 0;
}
int alarm2_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x000F0000) == 0x000E0000)
      return 1;
   else 
      return 0;
}
int alarm3_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x00F00000) == 0x00E00000)
      return 1;
   else 
      return 0;
}
int alarm4_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x00000F00) == 0x00000E00)
      return 1;
   else 
      return 0;
}
int alarm5_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x0000F000) == 0x0000E000)
      return 1;
   else 
      return 0;
}
int alarm6_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x0000000F) == 0x0000000E)
      return 1;
   else 
      return 0;
}
int alarm7_get (D212Card* pCard)
{
   if ((FPGA_REG_READ32(pCard->fpgaAddr, REG_Alarm) & 0x000000F0) == 0x000000E0)
      return 1;
   else 
      return 0;
}

void set_Drv_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Drv_Reset, OPTION_SET);
}

void clear_Drv_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Drv_Reset, OPTION_CLEAR);
}

int Drv_Reset_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Drv_Reset) == 0xAAAAAAAA)
      return 1;
   else 
      return 0;
}

void set_SG_Mode (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_SG_Mode, OPTION_SET);
}

void clear_SG_Mode (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_SG_Mode, OPTION_CLEAR);
}

int SG_Mode_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_SG_Mode) == 0xAAAAAAAA)
      return 1;
   else 
      return 0;
}

void int_Clear (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Int_Clear, OPTION_SET);
}

void set_RFReset_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_RF_Reset, OPTION_SET);
}

void clear_RFReset_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_RF_Reset, OPTION_CLEAR);
}

int RFReset_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_RF_Reset) == 0xAAAAAAAA)
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
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Sweep_Option) == 0xAAAAAAAA)
      return 1;
   else 
      return 0;
}

void set_AMP_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Option, OPTION_SET);
}

void clear_AMP_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Option, OPTION_CLEAR);
}

int AMP_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_AMP_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_FF_Option, OPTION_SET);
}

void clear_AMP_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_FF_Option, OPTION_CLEAR);
}

int AMP_FF_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_FF_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_AMP_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Modify_Option, OPTION_SET);
}

void clear_AMP_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Modify_Option, OPTION_CLEAR);
}

int AMP_Modify_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_Modify_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Tune_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_Option, OPTION_SET);
}

void clear_Tune_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_Option, OPTION_CLEAR);
}

int Tune_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Front_Tune_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Tune_Option, OPTION_SET);
}

void clear_Front_Tune_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Tune_Option, OPTION_CLEAR);
}

int Front_Tune_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Front_Tune_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Tune_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_FF_Option, OPTION_SET);
}

void clear_Tune_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_FF_Option, OPTION_CLEAR);
}

int Tune_FF_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_FF_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Tune_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_Modify_Option, OPTION_SET);
}

void clear_Tune_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_Modify_Option, OPTION_CLEAR);
}

int Tune_Modify_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_Modify_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Phase_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_Option, OPTION_SET);
}

void clear_Phase_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_Option, OPTION_CLEAR);
}

int Phase_OPTION_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Phase_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Phase_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_FF_Option, OPTION_SET);
}

void clear_Phase_FF_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_FF_Option, OPTION_CLEAR);
}

int Phase_FF_Option_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Phase_FF_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Phase_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_Modify_Option, OPTION_SET);
}

void clear_Phase_Modify_Option (D212Card* pCard)
{
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_Modify_Option, OPTION_CLEAR);
}

int Phase_Modify_Option_get (D212Card* pCard)
{
   if (FPGA_REG_READ32(pCard->fpgaAddr, REG_Phase_Modify_Option) == 0xAAAAAAAA)
      return 1;
   else
      return 0;
}

void set_Fix_Frequency (D212Card* pCard, float frequency)
{
   unsigned int value;
   value = (unsigned int)(frequency * CALC_Fix_Frequency_Set_MUL + CALC_Fix_Frequency_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Fix_Frequency_Set, value);
}

float get_Fix_Frequency (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Fix_Frequency_Set) - CALC_Fix_Frequency_Set_ADD) / CALC_Fix_Frequency_Set_MUL;
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

void set_AMP (D212Card* pCard, float ampSet)
{
   unsigned int value;
   value = (unsigned int)(ampSet * CALC_AMP_Set_MUL + CALC_AMP_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Set, value);
}

float get_AMP (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_Set) - CALC_AMP_Set_ADD) / CALC_AMP_Set_MUL;
}

void set_AMP_Coefficient (D212Card* pCard, float ampCoefficient)
{
   unsigned int value;
   value = (unsigned int)(ampCoefficient * CALC_AMP_Coefficient_MUL + CALC_AMP_Coefficient_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_Coefficient, value);
}

float get_AMP_Coefficient (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_Coefficient) - CALC_AMP_Coefficient_ADD) / CALC_AMP_Coefficient_MUL;
}

void set_AMP_P (D212Card* pCard, float ampP)
{
   unsigned int value;
   value = (unsigned int)(ampP * CALC_AMP_P_Set_MUL + CALC_AMP_P_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_P_Set, value);
}

float get_AMP_P (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_P_Set) - CALC_AMP_P_Set_ADD) / CALC_AMP_P_Set_MUL;
}

void set_AMP_I (D212Card* pCard, float ampI)
{
   unsigned int value;
   value = (unsigned int)(ampI * CALC_AMP_I_Set_MUL + CALC_AMP_I_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_I_Set, value);
}

float get_AMP_I (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_I_Set) - CALC_AMP_I_Set_ADD) / CALC_AMP_I_Set_MUL;
}

void set_AMP_I_1 (D212Card* pCard, float ampI_1)
{
   unsigned int value;
   value = (unsigned int)(ampI_1 * CALC_AMP_I_Set1_MUL + CALC_AMP_I_Set1_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_I_Set1, value);
}

float get_AMP_I_1 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_I_Set1) - CALC_AMP_I_Set1_ADD)/ CALC_AMP_I_Set1_MUL;
} 

void set_AMP_I_2 (D212Card* pCard, float ampI_2)
{
   unsigned int value;
   value = (unsigned int)(ampI_2 * CALC_AMP_I_Set2_MUL + CALC_AMP_I_Set2_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_I_Set2, value);
}

float get_AMP_I_2 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_I_Set2) - CALC_AMP_I_Set2_ADD) / CALC_AMP_I_Set2_MUL;
} 

void set_AMP_I_3 (D212Card* pCard, float ampI_3)
{
   unsigned int value;
   value = (unsigned int)(ampI_3 * CALC_AMP_I_Set3_MUL + CALC_AMP_I_Set3_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_AMP_I_Set3, value);
}

float get_AMP_I_3 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_AMP_I_Set3) - CALC_AMP_I_Set3_ADD) / CALC_AMP_I_Set3_MUL;
} 


void set_Bias (D212Card* pCard, float bias)
{
   unsigned int value;
   value = (unsigned int)(bias * CALC_Bias_Set_MUL + CALC_Bias_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Bias_Set, value);
}

float get_Bias (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Bias_Set) - CALC_Bias_Set_ADD) / CALC_Bias_Set_MUL;
}

/* Angle value, add 360 if negative value */
void set_Fix_Tuning_Angle (D212Card* pCard, float angle)
{
   unsigned int value;
   angle = angle >= 0 ? angle : angle + 360;
   value = (unsigned int)(angle * CALC_Fix_Tuning_Angle_MUL + CALC_Fix_Tuning_Angle_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Fix_Tuning_Angle, value);
}

float get_Fix_Tuning_Angle (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Fix_Tuning_Angle) - CALC_Fix_Tuning_Angle_ADD) / CALC_Fix_Tuning_Angle_MUL;
}

/* Angle value, add 360 if negative value */
void set_Tuning_Angle_Offset (D212Card* pCard, float offset)
{
   unsigned int value;
   offset = offset >= 0 ? offset : offset + 360;
   value = (unsigned int)(offset * CALC_Tuning_Angle_Offset_MUL + CALC_Tuning_Angle_Offset_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tuning_Angle_Offset, value);
}

float get_Tuning_Angle_Offset (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tuning_Angle_Offset) - CALC_Tuning_Angle_Offset_ADD) / CALC_Tuning_Angle_Offset_MUL;
}

void set_Tune_P (D212Card* pCard, float tuneP)
{
   unsigned int value;
   value = (unsigned int)(tuneP * CALC_Tune_P_Set_MUL + CALC_Tune_P_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_P_Set, value);
}

float get_Tune_P (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_P_Set) - CALC_Tune_P_Set_ADD) / CALC_Tune_P_Set_MUL;
}

void set_Tune_I (D212Card* pCard, float tuneI)
{
   unsigned int value;
   value = (unsigned int)(tuneI * CALC_Tune_I_Set_MUL + CALC_Tune_I_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_I_Set, value);
}

float get_Tune_I (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_I_Set) - CALC_Tune_I_Set_ADD) / CALC_Tune_I_Set_MUL;
}

void set_Tune_I_1 (D212Card* pCard, float tuneI_1)
{
   unsigned int value;
   value = (unsigned int)(tuneI_1 * CALC_Tune_I_Set1_MUL + CALC_Tune_I_Set1_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_I_Set1, value);
}

float get_Tune_I_1 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_I_Set1) - CALC_Tune_I_Set1_ADD) / CALC_Tune_I_Set1_MUL;
}

void set_Tune_I_2 (D212Card* pCard, float tuneI_2)
{
   unsigned int value;
   value = (unsigned int)(tuneI_2 * CALC_Tune_I_Set2_MUL + CALC_Tune_I_Set2_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_I_Set2, value);
}

float get_Tune_I_2 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_I_Set2) - CALC_Tune_I_Set2_ADD) / CALC_Tune_I_Set2_MUL;
}

void set_Tune_I_3 (D212Card* pCard, float tuneI_3)
{
   unsigned int value;
   value = (unsigned int)(tuneI_3 * CALC_Tune_I_Set3_MUL + CALC_Tune_I_Set3_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Tune_I_Set3, value);
}

float get_Tune_I_3 (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Tune_I_Set3) - CALC_Tune_I_Set3_ADD) / CALC_Tune_I_Set3_MUL;
}

void set_Front_Bias (D212Card* pCard, float frontBias)
{
   unsigned int value;
   value = (unsigned int)(frontBias * CALC_Front_Bias_Set_MUL + CALC_Front_Bias_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Bias_Set, value);
}

float get_Front_Bias (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Front_Bias_Set) - CALC_Front_Bias_Set_ADD) / CALC_Front_Bias_Set_MUL;
}

void set_Front_Tune_P (D212Card* pCard, float frontTuneP)
{
   unsigned int value;
   value = (unsigned int)(frontTuneP * CALC_Front_Tune_P_Set_MUL + CALC_Front_Tune_P_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Tune_P_Set, value);
}

float get_Front_Tune_P (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Front_Tune_P_Set) - CALC_Front_Tune_P_Set_ADD) / CALC_Front_Tune_P_Set_MUL;
}

void set_Front_Tune_I (D212Card* pCard, float frontTuneI)
{
   unsigned int value;
   value = (unsigned int)(frontTuneI * CALC_Front_Tune_I_Set_MUL + CALC_Front_Tune_I_Set_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Tune_I_Set, value);
}

float get_Front_Tune_I (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Front_Tune_I_Set) - CALC_Front_Tune_I_Set_ADD) / CALC_Front_Tune_I_Set_MUL;
}

/* Angle value, add 360 if negative value */
void set_Front_Fix_Tuning_Angle (D212Card* pCard, float frontFixTunAng)
{
   unsigned int value;
   frontFixTunAng = frontFixTunAng >= 0 ? frontFixTunAng : frontFixTunAng + 360;
   value = (unsigned int)(frontFixTunAng * CALC_Front_Fix_Tuning_Angle_MUL + CALC_Front_Fix_Tuning_Angle_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Front_Fix_Tuning_Angle, value);
}

float get_Front_Fix_Tuning_Angle (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Front_Fix_Tuning_Angle) - CALC_Front_Fix_Tuning_Angle_ADD) / CALC_Front_Fix_Tuning_Angle_MUL;
}

/* Temp1 value */
void set_phase_i (D212Card* pCard, float phase_i)
{
   unsigned int value;
   value = (unsigned int)(phase_i * CALC_PHASE_I_MUL + CALC_PHASE_I_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_I, value);
}

float get_Phase_i (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Phase_I) - CALC_PHASE_I_ADD) / CALC_PHASE_I_MUL;
}

/* Temp2 value */
void set_phase_p (D212Card* pCard, float phase_p)
{
   unsigned int value;
   value = (unsigned int)(phase_p * CALC_PHASE_P_MUL + CALC_PHASE_P_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Phase_P, value);
}

float get_Phase_p (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Phase_P) - CALC_PHASE_P_ADD) / CALC_PHASE_P_MUL;
}

void set_Initial_Phase (D212Card* pCard, float initial_phase)
{
   unsigned int value;
   value = (unsigned int)(initial_phase * CALC_INITIAL_PHASE_MUL + CALC_INITIAL_PHASE_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_Initial_Phase, value);
}

float get_Initial_Phase (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_Initial_Phase) - CALC_INITIAL_PHASE_ADD) / CALC_INITIAL_PHASE_MUL;
}

void set_FF_Delay (D212Card* pCard, float ff_delay)
{
   unsigned int value;
   value = (unsigned int)(ff_delay * CALC_FF_DELAY_MUL + CALC_FF_DELAY_ADD);
   FPGA_REG_WRITE32(pCard->fpgaAddr, REG_FF_Delay, value);
}

float get_FF_Delay (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_FF_Delay) - CALC_FF_DELAY_ADD) / CALC_FF_DELAY_MUL;
}

float get_ARC_COUNT (D212Card* pCard)
{
   return (FPGA_REG_READ32(pCard->fpgaAddr, REG_ARC_COUNT) - CALC_ARC_COUNT_ADD) / CALC_ARC_COUNT_MUL;
}
