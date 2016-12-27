/*
*********************************************************************************************************
*                                            EXAMPLE CODE
*
*               This file is provided as an example on how to use Micrium products.
*
*               Please feel free to use any application code labeled as 'EXAMPLE CODE' in
*               your application products.  Example code may be used as is, in whole or in
*               part, or may be used as a reference only. This file can be modified as
*               required to meet the end-product requirements.
*
*               Please help us continue to provide the Embedded community with the finest
*               software available.  Your honesty is greatly appreciated.
*
*               You can find our product's user manual, API reference, release notes and
*               more information at https://doc.micrium.com.
*               You can contact us at www.micrium.com.
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                      APPLICATION CONFIGURATION
*
*                                        Freescale Kinetis K64
*                                               on the
*
*                                         Freescale FRDM-K64F
*                                          Evaluation Board
*
* Filename      : app_cfg.h
* Version       : V1.00
* Programmer(s) : FF
*********************************************************************************************************
*/

#ifndef  APP_CFG_MODULE_PRESENT
#define  APP_CFG_MODULE_PRESENT


/*
*********************************************************************************************************
*                                       ADDITIONAL uC/MODULE ENABLES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            TASK PRIORITIES
*********************************************************************************************************
*/

#define  APP_CFG_TASK_START_PRIO                      2u
#define  APP_CFG_TASK_OBJ_PRIO                        3u
#define  APP_CFG_TASK_EQ_PRIO                         4u


/*
*********************************************************************************************************
*                                            TASK STACK SIZES
*********************************************************************************************************
*/

#define  APP_CFG_TASK_START_STK_SIZE                512u
#define  APP_CFG_TASK_EQ_STK_SIZE                   512u
#define  APP_CFG_TASK_OBJ_STK_SIZE                  256u

/*
*********************************************************************************************************
*                                          TASK STACK SIZES LIMIT
*********************************************************************************************************
*/

#define  APP_CFG_TASK_START_STK_SIZE_PCT_FULL             90u
#define  APP_CFG_TASK_EQ_STK_SIZE_PCT_FULL                90u


#define  APP_CFG_TASK_START_STK_SIZE_LIMIT       (APP_CFG_TASK_START_STK_SIZE * (100u - APP_CFG_TASK_START_STK_SIZE_PCT_FULL)) / 100u
#define  APP_CFG_TASK_EQ_STK_SIZE_LIMIT          (APP_CFG_TASK_EQ_STK_SIZE    * (100u - APP_CFG_TASK_EQ_STK_SIZE_PCT_FULL   )) / 100u
#define  APP_CFG_TASK_OBJ_STK_SIZE_LIMIT         (APP_CFG_TASK_OBJ_STK_SIZE   * (100u - APP_CFG_TASK_EQ_STK_SIZE_PCT_FULL   )) / 100u


/*
*********************************************************************************************************
*                                          SERIAL CONFIGURATION
*********************************************************************************************************
*/

#define  BSP_CFG_SER_COMM_SEL                BSP_SER_COMM_UART_00


/*
*********************************************************************************************************
*                                       TRACE / DEBUG CONFIGURATION
*********************************************************************************************************
*/

#ifndef  TRACE_LEVEL_OFF
#define  TRACE_LEVEL_OFF                            0u
#endif

#ifndef  TRACE_LEVEL_INFO
#define  TRACE_LEVEL_INFO                           1u
#endif

#ifndef  TRACE_LEVEL_DBG
#define  TRACE_LEVEL_DBG                            2u
#endif

#include  <stdio.h>
void  BSP_Ser_Printf (CPU_CHAR *p_fmt,
                      ...);
#define  APP_TRACE_LEVEL                            TRACE_LEVEL_DBG
#define  APP_CFG_TRACE                              BSP_Ser_Printf

#define  APP_TRACE_INFO(x)               ((APP_TRACE_LEVEL >= TRACE_LEVEL_INFO)  ? (void)(APP_CFG_TRACE x) : (void)0u)
#define  APP_TRACE_DBG(x)                ((APP_TRACE_LEVEL >= TRACE_LEVEL_DBG)   ? (void)(APP_CFG_TRACE x) : (void)0u)

typedef enum blink_rate {
	BLINK_SHORT,
	BLINK_LONG,
	BLINK_SHORTEST,
	BLINK_NONE
} blink_mode;

#define BLINK_LONG_MOD		0x5B8D	// 10Hz
#define BLINK_SHORT_MOD 	0x2DC6	// 20Hz
#define BLINK_SHORTEST_MOD	0x3210	// 36Hz

#define THRE_FACT_LEFT		0		// threshold factor from left
#define THRE_FACT_RIGHT		1.1		// threshold factor from right

// proportionally computed (65535 = 3.3V)
//#define VOLT_00				     0	// 0.0 V
//#define VOLT_05				  9930	// 0.5 V
//#define VOLT_10				 19859	// 1.0 V
//#define VOLT_15				 29789	// 1.5 V
//#define VOLT_20				 39718	// 2.0 V
//#define VOLT_25				 49648	// 2.5 V
//#define VOLT_30				 59577	// 3.0 V
//#define THRE_05				     0
//#define THRE_10				     0
//#define THRE_15				     0
//#define THRE_20					 0
//#define THRE_25				     0
//#define THRE_30				     0

// measured
#define VOLT_00				     0	// 0.0 V
#define VOLT_05				  9744	// 0.5 V
#define VOLT_10				 19910	// 1.0 V
#define VOLT_15				 30234	// 1.5 V
#define VOLT_20				 39870	// 2.0 V
#define VOLT_25				 49905	// 2.5 V
#define VOLT_30				 60349	// 3.0 V
#define THRE_05				   128
#define THRE_10				   628
#define THRE_15				     8
#define THRE_20					 8
#define THRE_25				   128
#define THRE_30				  1001

#endif
