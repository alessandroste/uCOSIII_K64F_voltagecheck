/*
*********************************************************************************************************
* STUDENT: Alessandro Stefanini
* This software implements an embedded battery alarm. The system checks continuously the voltage provided
* at the pin PTB2 (w.r.t. GND) and controls the blinking of the on-board LEDs.
* It is not possible to control on-board LEDs directly from the FlexTimer module because there is no physical
* connection between the two inside the board, a possibility is to use an interrupt which toggles the
* associated LED pin.
* FTM0 used for triggering ADC0
* ADC0 used for analog input reading
* FTM1 used for output wave generation
* eDMA used for fast and deterministic transfer of data between ADC0 and SRAM
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                             INCLUDE FILES
*********************************************************************************************************
*/

#include  <math.h>
#include  <lib_math.h>
#include  <cpu_core.h>

#include  <app_cfg.h>
#include  <os.h>

#include  <fsl_os_abstraction.h>
#include  <system_MK64F12.h>
#include  <board.h>

#include  <bsp_ser.h>


/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/

#include "fsl_interrupt_manager.h"
#include "fsl_gpio_common.h"

/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

static  OS_TCB       AppStartupTaskTCB;
static  CPU_STK      AppStartupTaskStk[APP_CFG_TASK_START_STK_SIZE];

static  OS_TCB       AppTaskTCB;
static  CPU_STK      AppTaskStk[APP_CFG_TASK_START_STK_SIZE];

static  OS_SEM       semaphore_main;
static  OS_SEM       semaphore_starttask;

volatile uint32_t	 adc_in;

volatile blink_mode  led_rate;
volatile uint32_t	 current_led;

/*
*********************************************************************************************************
*                                      LOCAL FUNCTION PROTOTYPES
*********************************************************************************************************
*/

static void AppStartupTask (void  *p_arg);
static void AppTask (void  *p_arg);

// setup of FTM0, ADC0 and DMA handling
static void ftm0_adc0_trigger_setup(void);
static void dma_int_handler(void);

// this timer is for blink wave generation
static void ftm1_setup(void);
static void ftm1_int_handler(void);
static void ftm1_change_pulse(blink_mode rate);

// ADC0 for input signal
static void adc0_setup(void);
static void adc0_start_reading(void);

// procedure of voltage range checking
static void range_check(void);

// procedure of range extension check
static uint8_t extend_range_color(uint32_t required_led);
static uint8_t extend_range_rate(blink_mode required_rate);

/*
*********************************************************************************************************
*                                                main()
*********************************************************************************************************
*/

int  main (void){
    OS_ERR   err;

#if (CPU_CFG_NAME_EN == DEF_ENABLED)
    CPU_ERR  cpu_err;
#endif

    hardware_init();

    // to output the wave of LEDs blinking
    PORT_HAL_SetMuxMode(PORTC_BASE,3u, kPortMuxAsGpio);

    GPIO_DRV_Init(switchPins, outPins);

    // setup of all the hardware modules
    ftm0_adc0_trigger_setup();
    ftm1_setup();


#if (CPU_CFG_NAME_EN == DEF_ENABLED)
    CPU_NameSet((CPU_CHAR *)"MK64FN1M0VMD12",
                (CPU_ERR  *)&cpu_err);
#endif

    BSP_Ser_Init(115200u);
    OSA_Init();                                                 /* Init uC/OS-III.                                      */

    OSTaskCreate( &AppStartupTaskTCB,                           /* Create the start task                                */
                  "App Startup Task",
                  AppStartupTask,
                  0u,
                  APP_CFG_TASK_START_PRIO,
                  &AppStartupTaskStk[0u],
                  (APP_CFG_TASK_START_STK_SIZE / 10u),
                  APP_CFG_TASK_START_STK_SIZE,
                  0u,
                  0u,
                  0u,
                  (OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR | OS_OPT_TASK_SAVE_FP),
                  &err);

    OSA_Start();                                                /* Start multitasking (i.e. give control to uC/OS-III). */

    while (DEF_ON) {                                            /* Should Never Get Here                                */
        ;
    }
}


static  void  AppStartupTask (void *p_arg){
    OS_ERR    os_err;

    (void)p_arg;

    CPU_Init();                                                 /* Initialize the uC/CPU Services.                      */
    Mem_Init();                                                 /* Initialize the Memory Management Module              */
    Math_Init();                                                /* Initialize the Mathematical Module                   */

#if OS_CFG_STAT_TASK_EN > 0u
    OSStatTaskCPUUsageInit(&os_err);                            /* Compute CPU capacity with no task running            */
#endif

#ifdef CPU_CFG_INT_DIS_MEAS_EN
    CPU_IntDisMeasMaxCurReset();
#endif

    // this semaphore will put this task in wait state so that it will not be scheduled anymore
    OSSemCreate(&semaphore_starttask,
    				"Start Task Lock",
    				0u,
    				&os_err);

    OSTaskCreate(&AppTaskTCB,                              		/* Create the start task                                */
                 "App Task",
                 AppTask,
                 0u,
                 APP_CFG_TASK_START_PRIO,
                 &AppTaskStk[0u],
                 (APP_CFG_TASK_START_STK_SIZE / 10u),
                 APP_CFG_TASK_START_STK_SIZE,
                 0u,
                 0u,
                 0u,
                 (OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR | OS_OPT_TASK_SAVE_FP),
                 &os_err);

    // do not schedule this task anymore
    OSSemPend(&semaphore_starttask,
    				  0u,
    				  OS_OPT_PEND_BLOCKING,
    				  0u,
    				  &os_err);
    while (DEF_TRUE) {}
}

static  void  AppTask (void *p_arg){
	CPU_ERR     cpu_err;
	OS_ERR      os_err;

    (void)p_arg;

    // this semaphore is for ADC0 reading
    OSSemCreate(&semaphore_main,
				"Main Task Lock",
				0u,
				&os_err);

    // all the LEDs are switched off initially
    GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_RED);
    GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_GREEN);
    GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_BLUE);

    // the output of the wave is initially low
    GPIO_DRV_ClearPinOutput(kGpioWave1Out);

    // initial settings
    ftm1_change_pulse(BLINK_SHORT);
    current_led = BOARD_GPIO_LED_RED;
    led_rate = BLINK_NONE;

    // main cycle
    while (DEF_TRUE) {
    	// wait for ADC0 reading
    	OSSemPend(&semaphore_main,
				  0u,
				  OS_OPT_PEND_BLOCKING,
				  0u,
				  &os_err);
    	// do the check and eventually change FTM1 settings and LED
    	range_check();
    }
}

static void ftm0_adc0_trigger_setup(void){
	INT_SYS_EnableIRQ(DMA0_IRQn);							// enable interrupts of eDMA module
	INT_SYS_InstallHandler(DMA0_IRQn, dma_int_handler);		// install routine for interrupt

	SIM_SCGC6 |= SIM_SCGC6_DMAMUX_MASK;						// enable DMAMUX module
	SIM_SCGC7 |= SIM_SCGC7_DMA_MASK;						// enable eDMA module
	DMAMUX_CHCFG0 = (DMAMUX_CHCFG_ENBL_MASK|DMAMUX_CHCFG_SOURCE(40));
															// route ADC0 DMA request to channel 0 of eDMA
	DMA_CR = (DMA_CR_EDBG_MASK);							// enable debug
															// word is 32bit NBYTES
	DMA_ERQ |= (DMA_ERQ_ERQ0_MASK);							// enable DMA requests on channel 0
	DMA_DCHPRI0 = ((uint32_t) (0));							// static priority
	DMA_TCD0_CSR = (DMA_CSR_INTMAJOR_MASK);					// generate interrupt after major cycle
	DMA_TCD0_SADDR = DMA_SADDR_SADDR(&ADC0_RA);				// set source address i.e. ADC0 register A,
															// where the reading is stored
	DMA_TCD0_SOFF = DMA_SOFF_SOFF(0);						// no offset
	DMA_TCD0_DADDR = DMA_DADDR_DADDR(&adc_in);				// set destination address in SRAM:
															// address of the variable
	DMA_TCD0_DOFF = DMA_DOFF_DOFF(0);						// no offset
	DMA_TCD0_ATTR = (DMA_ATTR_SSIZE(1)|DMA_ATTR_DSIZE(1)|
					 DMA_ATTR_SMOD(0)|DMA_ATTR_DMOD(0));	// 16b read and write, disable modulo function
	DMA_TCD0_NBYTES_MLNO = 2;								// 16bits minor cycle
	DMA_TCD0_SLAST = 0;										// do not correct source address after major cycle
	DMA_TCD0_CITER_ELINKNO = 1;								// number of major cycles
	DMA_TCD0_BITER_ELINKNO = 1;								// initial count of major cycles
	DMA_TCD0_DLASTSGA = 0;									// do not correct destination address after major cycle

	SIM_SCGC6 |= SIM_SCGC6_ADC0_MASK;						// enable ADC0
	SIM_SOPT7 |= (SIM_SOPT7_ADC0TRGSEL(8)|SIM_SOPT7_ADC0ALTTRGEN_MASK);
															// ADC0 trigger source: FTM0
	ADC0_SC2 = (ADC_SC2_ADTRG_MASK|ADC_SC2_DMAEN_MASK);		// ADC0 enable hardware triggers and DMA requests after
															// conversion
	ADC0_SC1A = ADC_SC1_ADCH(0xC);							// enable conversion on selected channel
	ADC0_CFG1 = ADC_CFG1_MODE(3);							// single ended 16B

	SIM_SCGC6 |= SIM_SCGC6_FTM0_MASK;						// enable FTM0
	FTM0_CONF = 0xC0; 										// set the timer in Debug mode, with BDM mode = 0xC0
	FTM0_FMS =  0x0;										// enable modifications to the FTM0 configuration
	FTM0_MODE |= (FTM_MODE_WPDIS_MASK|FTM_MODE_FTMEN_MASK);	// allowing writing in the registers
	FTM0_CNTIN = FTM_CNTIN_INIT(0);							// initial value of 16 bit counter
	FTM0_MOD = FTM_MOD_MOD(0xFFFF);							// module of the count over 16 bits
	FTM0_EXTTRIG |= FTM_EXTTRIG_INITTRIGEN_MASK;			// enable hw trigger on init count
	FTM0_SC = (FTM_SC_PS(7)|FTM_SC_CLKS(0x1));				// enable FTM0 with prescaler set to FTM_C_PS(7)=128
}

static void dma_int_handler(void){
	OS_ERR      os_err;

	// disable interrupts
	CPU_CRITICAL_ENTER();
	OSIntEnter();

	// enable main task
	OSSemPost(&semaphore_main,
				  OS_OPT_POST_1,
				  &os_err);

	// allow other DMA requests
	DMA_CINT = DMA_CINT_CINT(0);

	// re-enable interrupts
	CPU_CRITICAL_EXIT();
	OSIntExit();
}

static void ftm1_setup(void){
	INT_SYS_EnableIRQ(FTM1_IRQn);
	INT_SYS_InstallHandler(FTM1_IRQn, ftm1_int_handler);
    SIM_SCGC6 |= SIM_SCGC6_FTM1_MASK;
    FTM1_CONF = 0xC0;
    FTM1_FMS =  0x0;
    FTM1_MODE |= (FTM_MODE_WPDIS_MASK|FTM_MODE_FTMEN_MASK);
    FTM1_CNTIN = FTM_CNTIN_INIT(0);
    FTM1_SYNCONF |= (FTM_SYNCONF_SWWRBUF_MASK|FTM_SYNCONF_SWRSTCNT_MASK);
}

static void ftm1_change_pulse(blink_mode rate){
	CPU_CRITICAL_ENTER();
	OSIntEnter();
	switch(rate){
		case BLINK_LONG:
			FTM1_MOD = FTM_MOD_MOD(0x5B8D);
			break;
		case BLINK_SHORT:
			FTM1_MOD = FTM_MOD_MOD(0x2DC6);
			break;
		case BLINK_SHORTEST:
			FTM1_MOD = FTM_MOD_MOD(0x3210);
			break;
		case BLINK_NONE:
			GPIO_DRV_ClearPinOutput(current_led);
			FTM1_SC  &= FTM_SC_CLKS(0x0);
			CPU_CRITICAL_EXIT();
			OSIntExit();
			return;
	}
	FTM1_SYNC |= (FTM_SYNC_SWSYNC_MASK|FTM_SYNC_REINIT_MASK);
	FTM1_SC  = (FTM_SC_PS(7)|FTM_SC_CLKS(0x1)|FTM_SC_TOIE_MASK);
	CPU_CRITICAL_EXIT();
	OSIntExit();
}

static void ftm1_int_handler(void){
	OS_ERR      err;
	CPU_ERR     cpu_err;

	CPU_CRITICAL_ENTER();
	OSIntEnter();
	// assuming that FMT1 has overflown, unique source of interrupts
	FTM1_SC &= 0x7F;

	GPIO_DRV_TogglePinOutput(current_led);
	GPIO_DRV_TogglePinOutput(kGpioWave1Out);

	CPU_CRITICAL_EXIT();
	OSIntExit();
}

static void range_check(void){
	/* Rate of blink check */
	// 0.0 <= V < 0.5 | 1.0 <= V < 1.5 | 2.0 <= V < 2.5
	if (led_rate != BLINK_LONG &&(
			((adc_in >= VOLT_00)&&
					(adc_in < (VOLT_05 - THRE_FACT_RIGHT * THRE_05 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_GREEN))))||
			((adc_in >= (VOLT_10 + THRE_FACT_LEFT * THRE_10 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_GREEN)))&&
					(adc_in < (VOLT_15 - THRE_FACT_RIGHT * THRE_15 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_BLUE))))||
			((adc_in >= (VOLT_20 + THRE_FACT_LEFT * THRE_20 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_BLUE)))&&
					(adc_in < (VOLT_25 - THRE_FACT_RIGHT * THRE_25 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_RED)))))){
		ftm1_change_pulse(BLINK_LONG);
		led_rate = BLINK_LONG;
	}
	// 0.5 <= V < 1.0 | 1.5 <= V < 2.0 | 2.5 <= V < 3.0
	else if (led_rate != BLINK_SHORT &&(
			((adc_in >= (VOLT_05 + THRE_FACT_LEFT * THRE_05 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_GREEN)))&&
					(adc_in < (VOLT_10 - THRE_FACT_RIGHT * THRE_10 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_BLUE))))||
			((adc_in >= (VOLT_15 + THRE_FACT_LEFT * THRE_15 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_BLUE)))&&
					(adc_in < (VOLT_20 - THRE_FACT_RIGHT * THRE_20 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_RED))))||
			((adc_in >= (VOLT_25 + THRE_FACT_LEFT * THRE_25 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_RED)))&&
					(adc_in < (VOLT_30 - THRE_FACT_RIGHT * THRE_30 * extend_range_rate(BLINK_LONG) * extend_range_color(BOARD_GPIO_LED_RED)))))){
		ftm1_change_pulse(BLINK_SHORT);
		led_rate = BLINK_SHORT;
	}
	// 3.0 <= V
	else if (led_rate != BLINK_NONE &&
			(adc_in >= (VOLT_30 + THRE_FACT_LEFT * THRE_30 * extend_range_rate(BLINK_SHORT) * extend_range_color(BOARD_GPIO_LED_RED)))){
		ftm1_change_pulse(BLINK_NONE);
		led_rate = BLINK_NONE;
	}
	/* LED color check */
	// 0.0 <= V < 1.0 ---> GREEN
	if ((current_led != BOARD_GPIO_LED_GREEN) &&
			(adc_in >= VOLT_00) &&
			(adc_in < (VOLT_10 - THRE_FACT_RIGHT * THRE_10 * extend_range_color(BOARD_GPIO_LED_BLUE)))){
		current_led = BOARD_GPIO_LED_GREEN;
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_BLUE);
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_RED);
	}
	// 1.0 <= V < 2.0 ---> BLUE
	else if ((current_led != BOARD_GPIO_LED_BLUE) &&
			(adc_in >= (VOLT_10 + THRE_FACT_LEFT * THRE_10 * extend_range_color(BOARD_GPIO_LED_GREEN))) &&
			(adc_in <  (VOLT_20 - THRE_FACT_RIGHT * THRE_20 * extend_range_color(BOARD_GPIO_LED_RED)))){
		current_led = BOARD_GPIO_LED_BLUE;
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_GREEN);
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_RED);
	}
	// 2.0 <= V 	  ---> RED
	else if ((current_led != BOARD_GPIO_LED_RED) &&
			(adc_in >= (VOLT_20 + THRE_FACT_LEFT * THRE_20 * extend_range_color(BOARD_GPIO_LED_BLUE)))) {
		current_led = BOARD_GPIO_LED_RED;
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_BLUE);
		GPIO_DRV_SetPinOutput(BOARD_GPIO_LED_GREEN);
	}
}

static uint8_t extend_range_color(uint32_t required_led){
	if (current_led == required_led)
		return ((uint8_t)(1));
	return 0;
}

static uint8_t extend_range_rate(blink_mode required_rate){
	if (led_rate == required_rate)
		return ((uint8_t)(1));
	return 0;
}
