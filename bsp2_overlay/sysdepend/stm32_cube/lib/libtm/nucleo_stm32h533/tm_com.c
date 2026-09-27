/*
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *    tm_com.c
 *    T-Monitor Communication low-level device driver (NUCLEO-H533RE)
 *
 *    USART2 (PA2=TX, PA3=RX) — ST-Link VCP 経由
 *    STM32H533 USART2 base: 0x40004400
 *    ピンとクロックは stm32h5_hal_stub.c の Board_Init() で設定済み
 */

#include <tk/tkernel.h>

#if USE_TMONITOR
#include <mtkernel/lib/libtm/libtm.h>
#include <sysdepend/stm32_cube/halif.h>

#ifdef MTKBSP_NUCLEO_STM32H533
#if TM_COM_SERIAL_DEV

/* USART2 register definition */
#define	UART_BASE	(0x40004400UL)

#define UART_CR1	(*(_UW*)(UART_BASE+0x0000))
#define UART_CR2	(*(_UW*)(UART_BASE+0x0004))
#define UART_CR3	(*(_UW*)(UART_BASE+0x0008))
#define UART_BRR	(*(_UW*)(UART_BASE+0x000C))
#define UART_ISR	(*(_UW*)(UART_BASE+0x001C))
#define UART_ICR	(*(_UW*)(UART_BASE+0x0020))
#define UART_RDR	(*(_UW*)(UART_BASE+0x0024))
#define UART_TDR	(*(_UW*)(UART_BASE+0x0028))

#define CR1_UE		(0x00000001)
#define CR1_RE		(0x00000004)
#define CR1_TE		(0x00000008)

#define ISR_TXE		(0x00000080)
#define ISR_TC		(0x00000040)
#define ISR_RXNE	(0x00000020)

#define UART_BAUD	(115200)

EXPORT	void	tm_snd_dat( const UB* buf, INT size )
{
	UB	*b;
	for( b = (UB *)buf; size > 0; size--, b++ ){
		while ((UART_ISR & ISR_TXE) == 0 );
		UART_TDR = *b;
		while ((UART_ISR & ISR_TC) == 0 );
	}
}

EXPORT	void	tm_rcv_dat( UB* buf, INT size )
{
	for( ; size > 0; size--, buf++ ){
		while ( (UART_ISR & ISR_RXNE) == 0 );
		*buf = UART_RDR & 0xff;
	}
}

EXPORT	void	tm_com_init(void)
{
	UW	pclk;

	UART_CR1 = 0;
	UART_CR2 = 0;
	UART_CR3 = 0;

	/* STM32H533: USART2 clock from APB1 */
	pclk = halif_get_pclk1();
	UART_BRR = (pclk + UART_BAUD/2)/UART_BAUD;

	UART_CR1 = CR1_UE | CR1_RE | CR1_TE;
}

#endif /* TM_COM_SERIAL_DEV */
#endif /* MTKBSP_NUCLEO_STM32H533 */
#endif /* USE_TMONITOR */
