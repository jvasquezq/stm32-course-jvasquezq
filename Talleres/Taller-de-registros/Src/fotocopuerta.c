#include "stm32f411xe.h"
#include <stdint.h>

// Declaración de variables globales compartidas con las ISR
volatile uint8_t flag_sumar = 0;
volatile uint8_t flag_restar = 0;
volatile int16_t contador = 0; // Debe ser signed (con signo) para evaluar < 0

// Cabecera de funciones 
void init_GPIO(void);
void init_timer3(void);
void init_EXTI(void);
void Actualizar_Contador(void);

int main(void)
{
    init_GPIO();
    init_timer3();
    init_EXTI();

    /* Loop principal */
    while (1) {
        Actualizar_Contador();
    }

    return 0;
}

/*----- GPIO -----*/
void init_GPIO(void){
    // Habilitar reloj para GPIOA y GPIOC
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN);

    /*--- Puerto A, restador (PA1) ---*/
    GPIOA->MODER &= ~(GPIO_MODER_MODE1); // Entrada (00)
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD1); // Sin pull-up ni pull-down

    /*--- Puerto C, sumador (PC3) ---*/
    GPIOC->MODER &= ~(GPIO_MODER_MODE3); // Entrada (00)
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD3); // Sin pull-up ni pull-down
}

/*--- EXTI ---*/
void init_EXTI(void){
    // Activar reloj para SYSCFG
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Enlazar PA1 -> EXTI1 y PC3 -> EXTI3
    SYSCFG->EXTICR[0] &= ~(SYSCFG_EXTICR1_EXTI1 | SYSCFG_EXTICR1_EXTI3);
    SYSCFG->EXTICR[0] |= (SYSCFG_EXTICR1_EXTI1_PA | SYSCFG_EXTICR1_EXTI3_PC);

    // Flancos de disparo
    EXTI->RTSR |= EXTI_RTSR_TR3;    // Subida para PC3
    EXTI->FTSR &= ~(EXTI_FTSR_TR3);

    EXTI->FTSR |= EXTI_FTSR_TR1;    // Bajada para PA1
    EXTI->RTSR &= ~(EXTI_RTSR_TR1);

    // Desenmascarar interrupciones
    EXTI->IMR |= (EXTI_IMR_IM1 | EXTI_IMR_IM3);

    // Habilitar en el NVIC
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI3_IRQn);
}

/*--- ISR PC3 (Sumar) ---*/
void EXTI3_IRQHandler(void){
    if (EXTI->PR & EXTI_PR_PR3) {
        EXTI->PR = EXTI_PR_PR3; // Limpiar bandera escribiendo '1'
        flag_sumar = 1;
    }
}

/*--- ISR PA1 (Restar) ---*/
void EXTI1_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR1) {
        EXTI->PR = EXTI_PR_PR1; // Limpiar bandera escribiendo '1'
        flag_restar = 1;
    }
}

/*--- Lógica del contador ---*/
void Actualizar_Contador(void) {
    if (flag_sumar == 1) {
        flag_sumar = 0;
        contador++;
        if (contador > 9999) {
            contador = 0;
        }
    }

    if (flag_restar == 1) {
        flag_restar = 0;
        contador--;
        if (contador < 0) {
            contador = 9999;
        }
    }
}