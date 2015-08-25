#include "ledtorus.h"
#include "nrf24l01p.h"


/* Communications protocol. */
#define POV_CMD_DEBUG 254
#define POV_SUBCMD_RESET_TO_BOOTLOADER 255
#define POV_SUBCMD_ENTER_BOOTLOADER 254
#define POV_SUBCMD_RESET_TO_APP 253
#define POV_SUBCMD_FLASH_BUFFER 252
#define POV_SUBCMD_EXIT_DEBUG   251
#define POV_SUBCMD_STATUS_REPLY 240


/*
  Setup SPI communication for nRF42L01+ on USART1 in synchronous mode.

    PA8   clk
    PB6   mosi
    PB7   miso
    PC1   cs
    PC2   ce
    PC3   irq
*/
static void
setup_nrf_spi(void)
{
  union {
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    USART_ClockInitTypeDef USART_ClockInitStruct;
    DMA_InitTypeDef DMA_InitStructure;
    EXTI_InitTypeDef EXTI_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
  } u;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

  USART_Cmd(USART1, DISABLE);

  /*
    Clock on PA8.
    Polarity is idle low, active high.
    Phase is sample on rising, setup on falling edge.
  */
  u.GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
  u.GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  u.GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
  u.GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  u.GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_Init(GPIOA, &u.GPIO_InitStructure);
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_USART1);

  /* MOSI and MISO on PB6/PB7. */
  u.GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7;
  u.GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  u.GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
  u.GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  u.GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOB, &u.GPIO_InitStructure);
  GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_USART1);
  GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_USART1);

  /*
    CS on PC1, CE on PC2.
    CS is high initially (active low).
    CE is low initially (active high).
  */
  GPIO_SetBits(GPIOC, GPIO_Pin_1);
  GPIO_ResetBits(GPIOC, GPIO_Pin_2);
  u.GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1|GPIO_Pin_2;
  u.GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  u.GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
  u.GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  u.GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOC, &u.GPIO_InitStructure);

  u.USART_InitStructure.USART_BaudRate = 5250000;
  u.USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  u.USART_InitStructure.USART_StopBits = USART_StopBits_1;
  u.USART_InitStructure.USART_Parity = USART_Parity_No;
  u.USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  u.USART_InitStructure.USART_Mode = USART_Mode_Tx|USART_Mode_Rx;
  USART_Init(USART1, &u.USART_InitStructure);
  u.USART_ClockInitStruct.USART_Clock = USART_Clock_Enable;
  u.USART_ClockInitStruct.USART_CPOL = USART_CPOL_Low;
  u.USART_ClockInitStruct.USART_CPHA = USART_CPHA_1Edge;
  u.USART_ClockInitStruct.USART_LastBit = USART_LastBit_Enable;
  USART_ClockInit(USART1, &u.USART_ClockInitStruct);

  USART_Cmd(USART1, ENABLE);

  /* Setup DMA. USART1 on DMA2 channel 4, streams 2 (Rx) and 7 (Tx). */
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
  DMA_DeInit(DMA2_Stream2);
  DMA_DeInit(DMA2_Stream7);

  u.DMA_InitStructure.DMA_BufferSize = 1;
  u.DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
  u.DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_1QuarterFull;
  u.DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
  u.DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  u.DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
  u.DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
  u.DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
  u.DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  u.DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  u.DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
  /* Configure USART1 TX DMA */
  u.DMA_InitStructure.DMA_PeripheralBaseAddr =(uint32_t) (&(USART1->DR));
  u.DMA_InitStructure.DMA_Channel = DMA_Channel_4;
  u.DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral ;
  u.DMA_InitStructure.DMA_Memory0BaseAddr = 0;
  DMA_Init(DMA2_Stream7, &u.DMA_InitStructure);
  /* Configure USART1 RX DMA */
  u.DMA_InitStructure.DMA_Channel = DMA_Channel_4;
  u.DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory ;
  u.DMA_InitStructure.DMA_Memory0BaseAddr = 0;
  DMA_Init(DMA2_Stream2, &u.DMA_InitStructure);

  /* Configure a USART1 DMA Rx transfer complete interrupt. */
  DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, DISABLE);
  u.NVIC_InitStruct.NVIC_IRQChannel = DMA2_Stream2_IRQn;
  u.NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 10;
  u.NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
  u.NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&u.NVIC_InitStruct);
  DMA_ITConfig(DMA2_Stream2, DMA_IT_TC, ENABLE);

  /* IRQ on PC3. */
  u.GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
  u.GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  u.GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
  u.GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  u.GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOC, &u.GPIO_InitStructure);

  /* Take an interrupt on falling edge (IRQ is active low). */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOC, EXTI_PinSource3);
  u.EXTI_InitStruct.EXTI_Line = EXTI_Line3;
  u.EXTI_InitStruct.EXTI_LineCmd = ENABLE;
  u.EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
  u.EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
  EXTI_Init(&u.EXTI_InitStruct);

  /* Clear any pending interrupt before enabling. */
  EXTI->PR = EXTI_Line3;
  u.NVIC_InitStruct.NVIC_IRQChannel = EXTI3_IRQn;
  u.NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 10;
  u.NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
  u.NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&u.NVIC_InitStruct);
}


static inline void
csn_low(void)
{
  GPIO_ResetBits(GPIOC, GPIO_Pin_1);
}


static inline void
csn_high(void)
{
  GPIO_SetBits(GPIOC, GPIO_Pin_1);
}


static inline void
ce_low(void)
{
  GPIO_ResetBits(GPIOC, GPIO_Pin_2);
}


static inline void
ce_high(void)
{
  GPIO_SetBits(GPIOC, GPIO_Pin_2);
}


static inline uint8_t
bitswap_byte(uint8_t in)
{
  return (uint8_t)(__RBIT((uint32_t)in) >> 24);
}


/*
  This function starts DMA to issue an nRF24L01+ command.
  The DMA transfer will run in the background. Interrupts should be used to
  track completion.
  When DMA Rx completion interrupt runs, call ssi_cmd_transfer_done() to
  complete the command.
*/
static void
ssi_cmd_start(volatile uint8_t *recvbuf, volatile uint8_t *sendbuf, uint32_t len)
{
  uint32_t i;

  /* Take CSN low to initiate transfer. */
  csn_low();

  /*
    Note that nRF SPI uses most-significant-bit first, while USART works
    with least-significant-bit first. So we need to bit-swap all the
    bytes sent and received.
  */
  for (i = 0; i < len; ++i)
    sendbuf[i] = bitswap_byte(sendbuf[i]);

  DMA2_Stream2->M0AR = (uint32_t)recvbuf;
  DMA2_Stream2->NDTR = len;
  DMA2_Stream7->M0AR = (uint32_t)sendbuf;
  DMA2_Stream7->NDTR = len;
  /* Clear DMA transfer complete flags. */
  DMA2->LIFCR = DMA_FLAG_TCIF2 & 0x0F7D0F7D;
  DMA2->HIFCR = DMA_FLAG_TCIF7 & 0x0F7D0F7D;
  /* Clear the  USART TC (transfer complete) flag. */
  USART1->SR &= ~USART_FLAG_TC;
  /* Enable the Rx and Tx DMA channels. */
  DMA2_Stream2->CR |= DMA_SxCR_EN;
  DMA2_Stream7->CR |= DMA_SxCR_EN;
  /* Enable the USART1 to generate Rx/Tx DMA requests. */
  USART1->CR3 |= (USART_DMAReq_Tx|USART_DMAReq_Rx);
}


static void
ssi_cmd_transfer_done()
{
  /* Take CSN high to complete transfer. */
  csn_high();

  /* Disable DMA requests and channels. */
  DMA2_Stream2->CR &= ~DMA_SxCR_EN;
  DMA2_Stream7->CR &= ~DMA_SxCR_EN;
  USART1->CR3 &= ~(USART_DMAReq_Tx|USART_DMAReq_Rx);
}


void
EXTI3_IRQHandler(void)
{
  if (EXTI->PR & EXTI_Line3) {
    // ToDo ... handle nrf IRQ.

    /* Clear the pending interrupt event. */
    EXTI->PR = EXTI_Line3;
  }
}


static volatile uint8_t nrf_send_buffer[32];
static volatile uint8_t nrf_recv_buffer[32];
static volatile uint8_t nrf_cmd_done = 0;

void
DMA2_Stream2_IRQHandler(void)
{
  if (DMA2->LISR & 0x00200000)
  {
    ssi_cmd_transfer_done();
    nrf_cmd_done = 1;
    DMA2->LIFCR = 0x00200000;
  }
}


static void
ssi_cmd_blocking(uint8_t *recvbuf, uint8_t *sendbuf, uint32_t len)
{
  uint32_t i;

  nrf_cmd_done = 0;
  ssi_cmd_start(nrf_recv_buffer, sendbuf, len);
  while (!nrf_cmd_done)
    ;
  /*
    Note that nRF SPI uses most-significant-bit first, while USART works
    with least-significant-bit first. So we need to bit-swap all the
    bytes sent and received.
  */
  for (i = 0; i < len; ++i)
    recvbuf[i] = bitswap_byte(nrf_recv_buffer[i]);
}


static void
nrf_read_reg_n_blocking(uint8_t reg, uint8_t *out, uint32_t len)
{
  uint8_t sendbuf[6];
  if (len > 5)
    len = 5;
  sendbuf[0] = nRF_R_REGISTER | reg;
  memset(&sendbuf[1], 0, len);
  ssi_cmd_blocking(out, sendbuf, len+1);
}


static uint8_t
nrf_read_reg_blocking(uint8_t reg, uint8_t *status_ptr)
{
  uint8_t recvbuf[2];
  nrf_read_reg_n_blocking(reg, recvbuf, 1);
  if (status_ptr)
    *status_ptr = recvbuf[0];
  return recvbuf[1];
}


void
setup_nrf24l01p(void)
{
  setup_nrf_spi();

  /* As a test, let's try read a register. */
  {
    uint8_t val, status;
    serial_puts("Starting nRF read...\r\n");
    val = nrf_read_reg_blocking(nRF_CONFIG, &status);
    serial_puts("nRF read: val=0x");
    serial_output_hexbyte(val);
    serial_puts(" status=0x");
    serial_output_hexbyte(status);
    serial_puts("\r\n");
    for (;;)
      ;
  }
}