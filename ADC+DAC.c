#include <stdio.h>
#include "diag/Trace.h"
#include "cmsis/cmsis_device.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"

#define MIN_FREQ_HZ 1u
#define myTIM2_PRESCALER ((uint16_t)0x0000)

volatile uint8_t firstEdge = 1;
volatile uint32_t count = 0;
volatile uint8_t freqAvailable = 0;
volatile uint8_t measuring555 = 1;

void SystemClock48MHz(void)
{
    RCC->CR &= ~(RCC_CR_PLLON);
    while ((RCC->CR & RCC_CR_PLLRDY) != 0) { }
    RCC->CFGR = 0x00280000;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != RCC_CR_PLLRDY) { }
    RCC->CFGR = (RCC->CFGR & (~RCC_CFGR_SW_Msk)) | RCC_CFGR_SW_PLL;
    SystemCoreClockUpdate();
}

static void myGPIOA_Init(void)
{
    /* Enable clock for GPIOA peripheral */
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    /* Configure PA0 as input */
    // bits [1:0] = 00 for input mode
    GPIOA->MODER &= ~(GPIO_MODER_MODER0);

    /* Configure PA1 and PA4 as analog */
    // bits [3:2] and [9:8] = 11 for analog mode
    GPIOA->MODER |= (0x3u << (1 * 2) | 0x3u << (4 * 2));

    /* Ensure no pull-up/pull-down for ports */
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR0 | GPIO_PUPDR_PUPDR1 | GPIO_PUPDR_PUPDR4);
}

static void myGPIOB_Init(void)
{
    /* Enable clock for GPIOB peripheral */
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

    /* Configure PB2 and PB3 as inputs */
    GPIOB->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);

    /* Configure PB8, PB9, PB11 as outputs */
    GPIOB->MODER |= ((GPIO_MODER_MODER8_0 | GPIO_MODER_MODER9_0 | GPIO_MODER_MODER11_0));

    /* Configure PB13, PB15 as alternate function (AF0) */
    GPIOB->MODER |= (0x2u << (13 * 2) | (0x2u << (15 * 2)));

    /* Set alternate function to AF0 for PB13, PB15 */
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFSEL13 | GPIO_AFRH_AFSEL15);

    /* Set high-speed mode for PB8, PB9, PB11, PB13, PB15 */
    GPIOB->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR8 | GPIO_OSPEEDER_OSPEEDR9 | GPIO_OSPEEDER_OSPEEDR11 | GPIO_OSPEEDER_OSPEEDR13 | GPIO_OSPEEDER_OSPEEDR15);

    /* Ensure no pull-up/pull-down for ports */
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR2 | GPIO_PUPDR_PUPDR3 | GPIO_PUPDR_PUPDR8 | GPIO_PUPDR_PUPDR9 | GPIO_PUPDR_PUPDR11 | GPIO_PUPDR_PUPDR13 | GPIO_PUPDR_PUPDR15);
}

static void myTIM2_Init(void)
{
    /* Enable clock for TIM2 peripheral */
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    /* Configure TIM2: buffer auto-reload, count up, stop on overflow,
	 * enable update events, interrupt on overflow only */
    TIM2->CR1 = TIM_CR1_URS | TIM_CR1_ARPE;

    /* Set clock prescaler value */
    TIM2->PSC = myTIM2_PRESCALER;

    uint32_t arr;
    if (MIN_FREQ_HZ == 0) {
        arr = 0xFFFFFFFFu;
    } else {
        uint64_t ticks = (uint64_t)SystemCoreClock / (uint64_t)MIN_FREQ_HZ;
        if (ticks == 0) ticks = 1;
        if (ticks > 0xFFFFFFFFull) ticks = 0xFFFFFFFFull;
        arr = (uint32_t)(ticks - 1u);
    }
    TIM2->ARR = arr;

    /* Update timer registers */
    TIM2->EGR = TIM_EGR_UG;

    /* Enable update interrupt generation */
    TIM2->DIER = TIM_DIER_UIE;

    /* Assign TIM2 interrupt priority = 0 in NVIC */
    NVIC_SetPriority(TIM2_IRQn, 0);

    /* Enable TIM2 interrupts in NVIC */
    NVIC_EnableIRQ(TIM2_IRQn);
}

static void myEXTI_Init(void)
{
    /* Enable clock for SYSCFG peripheral */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;

    /* Map EXTI0 line to PA0 (USER button) */
    SYSCFG->EXTICR[0] &= ~(0xFu << (4 * 0));
    SYSCFG->EXTICR[0] |= (0x0u << (4 * 0));

    /* Map EXTI2 line to PB2 (Function Generator) */
    SYSCFG->EXTICR[0] &= ~(0xFu << (4 * 2));
    SYSCFG->EXTICR[0] |= (0x1u << (4 * 2));

    /* Map EXTI3 line to PB3 (555 Timer) */
    SYSCFG->EXTICR[0] &= ~(0xFu << (4 * 3));
    SYSCFG->EXTICR[0] |= (0x1u << (4 * 3));

    /* EXTI lines: set rising-edge trigger */
    EXTI->RTSR |= (EXTI_RTSR_TR0 |EXTI_RTSR_TR2 | EXTI_RTSR_TR3);
    EXTI->FTSR &= ~(EXTI_FTSR_TR0 | EXTI_FTSR_TR2 | EXTI_FTSR_TR3);

    /* Enables interrupts on EXTI0, EXTI2 and EXTI3 */
    EXTI->IMR |= (EXTI_IMR_MR0 | EXTI_IMR_MR2 | EXTI_IMR_MR3);

    /* Assign EXTI0_1 interrupt priority = 0 in NVIC */
    NVIC_SetPriority(EXTI0_1_IRQn, 0);

    /* Assign EXTI2_3 interrupt priority = 0 in NVIC */
    NVIC_SetPriority(EXTI2_3_IRQn, 0);

    /* Enable EXTI0_1 interrupts in NVIC */
    NVIC_EnableIRQ(EXTI0_1_IRQn);

    /* Enable EXTI2_3 interrupts in NVIC */
    NVIC_EnableIRQ(EXTI2_3_IRQn);
}

/* when ADC1->ISR[0] = 1, then ADC is ready to start */
static void myADC_init(void)
{
	/* enable ADC clock */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

	/* ADC sampling time set to 239.5 clock cycles, set bits [2:0] to 1*/
	ADC1->SMPR |= (0x7u << 0);

	/* Channel selection register bit [1] set to 1 */
	ADC1->CHSELR |= (0x1u << 1);

	/* Set data resolution bits [4:3] to 00 */
	ADC1->CFGR1 &= ~(ADC_CFGR1_RES);

	/* Align set to 0 for right aligned data */
	ADC1->CFGR1 &= ~(ADC_CFGR1_ALIGN);

	/* ADC Data Register bits can be overwritten, so OVRMOD set to 1 */
	ADC1->CFGR1 |= ADC_CFGR1_OVRMOD;

	/* continuous conversion mode enabled */
	ADC1->CFGR1 |= ADC_CFGR1_CONT;

    /* Enable ADC */
    ADC1->CR |= ADC_CR_ADEN;

    /* Wait until ADC is ready to start */
    while((ADC1->ISR & ADC_ISR_ADRDY) == 0) {}

    /* start ADC process */
    ADC1->CR |= ADC_CR_ADSTART;

}

static uint32_t myADC_convert(void)
{
    /* wait until end of ADC process */
    /* wait for ADC1->ISR[2] = 1 */
    while((ADC1->ISR & ADC_ISR_EOC) == 0) {}

    /*ADC value set to what is contained in ADC1->DR register */
    uint32_t result = ADC1->DR;

    /* clear EOC flag */
    ADC1->ISR |= ADC_ISR_EOC;

    return result;
}

uint32_t find_resistance(uint32_t adc_value) {
	uint32_t res;
    if(adc_value >= 4095) {
    	return 5000;
    } else {
    	res = (unsigned int)((adc_value/4095.0f) * 5000.0f);
    }
    return res;
}

static void myDAC_init(void)
{
    /* Enable clock for DAC peripheral */
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;

    /* Disable DAC channel 1 trigger */
    DAC->CR &= ~DAC_CR_TEN1;

    /* Enable DAC channel 1 buffer */
    DAC->CR &= ~DAC_CR_BOFF1;

    /* Enable DAC channel 1 */
    DAC->CR |= DAC_CR_EN1;
}

static void myDAC_convert(uint16_t adcValue)
{
	//max value of DAC should be 4095
    //16 bit register, lower 12 bits used for DAC value
	uint32_t value = adcValue;
	if (value > 4095) {
		value = 4095;
		DAC->DHR12R1 = 4095;
	} else {
		DAC->DHR12R1 = value;
	}
	trace_printf("DAC input: %u\n", value);
}

static void mySPI2_init(void)
{
    /* Enable clock for SPI2 peripheral */
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    SPI2->CR1 = 0; // Reset control register
    /* Configure SPI2 in Master mode  */
    SPI2->CR1 |= SPI_CR1_MSTR;

    /* Software control of NSS enabled */
    SPI2->CR1 |= SPI_CR1_SSM;

    /* NSS pin forced to 1 */
    SPI2->CR1 |= SPI_CR1_SSI;

    /* baud rate prescale */
    SPI2->CR1 |= SPI_CR1_BR_1;

    /* BIDIO enabled */
    SPI2->CR1 |= SPI_CR1_BIDIOE;

    /* BIDIMODE enabled */
    SPI2->CR1 |= SPI_CR1_BIDIMODE;

    /* Enable SPI2 peripheral */
    SPI2->CR1 |= SPI_CR1_SPE;

}

int main(int argc, char* argv[])
{
    SystemClock48MHz();

    trace_printf("Project start\n");
    trace_printf("System clock: %u Hz, MIN_FREQ_HZ=%u\n", SystemCoreClock,
                 (unsigned)MIN_FREQ_HZ);

    myGPIOA_Init();
    myGPIOB_Init();
    myTIM2_Init();
    mySPI2_init();
    myEXTI_Init();
    myADC_init();
    myDAC_init();

    while(1) {
    	//if (freqAvailable) {

            //freqAvailable = 0;

            //double freq = (double)SystemCoreClock / (double)count;

            //trace_printf("Frequency = %f Hz\n", freq);

            if (measuring555) {
                uint32_t adcValue = myADC_convert() & 0xFFF;
                uint32_t res = find_resistance(adcValue);
                trace_printf("ADC: Value = %u, Resistance = %u ohms\n", adcValue, res);
                myDAC_convert(adcValue);
            }
    	}
    //}

}

void TIM2_IRQHandler(void)
{
    /* Check if update interrupt flag is indeed set */
    if (TIM2->SR & TIM_SR_UIF) {
        /* Clear update interrupt flag */
        TIM2->SR &= ~TIM_SR_UIF;

        /* Stop timer */
        TIM2->CR1 &= ~TIM_CR1_CEN;

        TIM2->CNT = 0;
        firstEdge = 1;
        trace_printf("*** Too slow: < %u Hz (overflow) ***\n", (unsigned)MIN_FREQ_HZ);
    }
}

/* Interrupt handler for user button (PA0) */
void EXTI0_1_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR0) {

        /* Wait for button to be released (PA0 = 0) */
        while((GPIOA->IDR & GPIO_IDR_0) != 0) {
            __NOP();
        }

        /* Switch measuring555 flag */
        measuring555 = !measuring555;

        // Stop timer
        TIM2->CR1 &= ~TIM_CR1_CEN;

        // Clear counter
        TIM2->CNT = 0;

        // Clear overflow flag
        TIM2->SR &= ~TIM_SR_UIF;

        // reset measurement variables
        firstEdge = 1;
        freqAvailable = 0;
        count = 0;

        /* Print message based on which source is being measured */
        if (measuring555) {
            trace_printf("Measuring 555 Timer (PB3)\n");
        } else {
            trace_printf("Measuring Function Generator (PB2)\n");
        }

        /* Clear EXTI0 interrupt pending flag */
        EXTI->PR = EXTI_PR_PR0;
    }
}

void EXTI2_3_IRQHandler(void)
{
    // Function Generator (PB2)
    if ((EXTI->PR & EXTI_PR_PR2) && measuring555 == 0) {

        if (firstEdge) {
            TIM2->CNT = 0;
            TIM2->SR &= ~TIM_SR_UIF;
            TIM2->CR1 |= TIM_CR1_CEN;
            firstEdge = 0;
            trace_printf("FG measurement not ready");

        } else {
            TIM2->CR1 &= ~TIM_CR1_CEN;
            count = TIM2->CNT;
            /* frequency available to print in main function */
            freqAvailable = 1;
            firstEdge = 1;
            trace_printf("FG measurement ready");
        }

        /* Clear EXTI 2 (FG) */
        EXTI->PR = EXTI_PR_PR2;
    }

    // 555 Timer (PB3)
    if ((EXTI->PR & EXTI_PR_PR3) && measuring555 == 1) {

        if (firstEdge) {
            TIM2->CNT = 0;
            TIM2->SR &= ~TIM_SR_UIF;
            TIM2->CR1 |= TIM_CR1_CEN;
            firstEdge = 0;
            trace_printf("555 measurement not ready");

        } else {
            TIM2->CR1 &= ~TIM_CR1_CEN;
            count = TIM2->CNT;
            /* frequency available to print in main function */
            freqAvailable = 1;
            firstEdge = 1;
            trace_printf("555 measurement ready");
        }

        /* Clear EXTI3 (555) */
        EXTI->PR = EXTI_PR_PR3;
    }
}

#pragma GCC diagnostic pop
