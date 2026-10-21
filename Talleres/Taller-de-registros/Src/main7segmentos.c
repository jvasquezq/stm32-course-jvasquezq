/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Juliana Andrea Vasquez Quintero
 * @brief          : Contador 0000..LIMITE_MAX en display 7 segmentos de 4
 *                   digitos, con fotocompuertas (+1 subida / -1 bajada)
 ******************************************************************************
 *
 * CONEXIONES DE HARDWARE (sin cambios):
 *
 * Segmentos:
 *  - E (Pin 1)  -> PC10      - B (Pin 7)  -> PA12
 *  - D (Pin 2)  -> PC12      - F (Pin 10) -> PA11
 *  - C (Pin 4)  -> PC11      - A (Pin 11) -> PB12
 *  - G (Pin 5)  -> PD2
 *
 * Transistores NPN de digitos (1 = digito habilitado):
 *  - T6  (Unidades) -> PC13
 *  - T8  (Decenas)  -> PC6
 *  - T9  (Centenas) -> PC5
 *  - T12 (Millares) -> PB7
 *
 * Fotocompuertas:
 *  - Sumador  (flanco de subida) -> PC3  (EXTI3)
 *  - Restador (flanco de bajada) -> PA1  (EXTI1)
 *
 * LED de estado (heartbeat, 1 Hz): LED2 -> PH1
 *
 * Reloj: HSI 16 MHz (reset por defecto, no se configura el PLL).
 ******************************************************************************
 */

#include "stm32f4xx.h"
#include <stdint.h>

/* ========================= PARAMETROS AJUSTABLES ========================== */

/* Valor maximo del contador. Al sumar 1 en LIMITE_MAX se pasa a 0000 y
 * al restar 1 en 0000 se salta a LIMITE_MAX (en lugar de 9999). */
#define LIMITE_MAX            5536u

/* Polaridad de los segmentos:
 *  1 = display ANODO COMUN  -> el segmento enciende con 0 (pin a GND)
 *  0 = display CATODO COMUN -> el segmento enciende con 1
 * NOTA: si los digitos se habilitan con NPN en el lado bajo (1 = digito ON),
 * lo normal es un display de CATODO COMUN. Si ve los numeros "invertidos"
 * (se apagan los segmentos que deberian encender), cambie este valor a 0. */
#define DISPLAY_ANODO_COMUN   1

/* 1 = segmentos en drenador abierto (solo con ANODO COMUN). Util si el anodo
 * se alimenta a 5 V: evita que un "1" de 3.3 V deje los segmentos
 * debilmente encendidos. 0 = push-pull (comportamiento original). */
#define SEG_OPEN_DRAIN        0

/* ============================== DEFINICIONES ============================== */

/* Bits del patron de segmentos */
#define SEG_A  (1u << 0)
#define SEG_B  (1u << 1)
#define SEG_C  (1u << 2)
#define SEG_D  (1u << 3)
#define SEG_E  (1u << 4)
#define SEG_F  (1u << 5)
#define SEG_G  (1u << 6)

/* Mascaras de los pines de segmentos por puerto */
#define SEG_MASK_A  ((1u << 11) | (1u << 12))                 /* F, B       */
#define SEG_MASK_B  (1u << 12)                                /* A          */
#define SEG_MASK_C  ((1u << 10) | (1u << 11) | (1u << 12))    /* E, C, D    */
#define SEG_MASK_D  (1u << 2)                                 /* G          */

/* Mascaras de los pines de digitos por puerto */
#define DIG_MASK_B  (1u << 7)                                 /* T12        */
#define DIG_MASK_C  ((1u << 5) | (1u << 6) | (1u << 13))      /* T9, T8, T6 */

/* Helpers de configuracion de pines */
#define MODO_SALIDA(P, n)   ((P)->MODER = ((P)->MODER & ~(3u << ((n) * 2u))) | (1u << ((n) * 2u)))
#define MODO_ENTRADA(P, n)  ((P)->MODER &= ~(3u << ((n) * 2u)))

/* ============================ VARIABLES GLOBALES ========================== */

static volatile uint16_t contador = 0;   /* Arranca en 0000 */
static uint8_t digito_actual = 0;        /* 0=millares 1=centenas 2=decenas 3=unidades */

/* Patrones 7 segmentos (bit=1 -> segmento encendido): 0..9 */
static const uint8_t tabla_7seg[10] = {
    0x3F, /* 0: A B C D E F   */
    0x06, /* 1: B C           */
    0x5B, /* 2: A B D E G     */
    0x4F, /* 3: A B C D G     */
    0x66, /* 4: B C F G       */
    0x6D, /* 5: A C D F G     */
    0x7D, /* 6: A C D E F G   */
    0x07, /* 7: A B C         */
    0x7F, /* 8: todos         */
    0x6F  /* 9: A B C D F G   */
};

static const uint16_t divisor[4] = { 1000u, 100u, 10u, 1u };

/* ============================== PROTOTIPOS ================================ */

static void Configurar_GPIO(void);
static void Configurar_EXTI(void);
static void Configurar_Timers(void);
static void Escribir_Segmentos(uint8_t patron);
static void Encender_Digito(uint8_t indice);

/* ============================ FUNCION PRINCIPAL =========================== */

int main(void)
{
    Configurar_GPIO();
    Configurar_Timers();
    Configurar_EXTI();

    /* Todo el trabajo se hace en interrupciones */
    while (1)
    {
    }
}

/* ============================ CONFIGURACION =============================== */

static void Configurar_GPIO(void)
{
    /* Reloj de los puertos A, B, C, D y H */
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                     RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN |
                     RCC_AHB1ENR_GPIOHEN);
    (void)RCC->AHB1ENR;   /* lectura de relleno: espera a que el reloj se propague */

#if SEG_OPEN_DRAIN && DISPLAY_ANODO_COMUN
    GPIOA->OTYPER |= SEG_MASK_A;
    GPIOB->OTYPER |= SEG_MASK_B;
    GPIOC->OTYPER |= SEG_MASK_C;
    GPIOD->OTYPER |= SEG_MASK_D;
#endif

    /* Niveles iniciales ANTES de pasar los pines a salida (sin destellos):
     * segmentos apagados y digitos apagados */
    Escribir_Segmentos(0);
    GPIOB->BSRR = (DIG_MASK_B << 16);
    GPIOC->BSRR = (DIG_MASK_C << 16);

    /* Segmentos (salidas) */
    MODO_SALIDA(GPIOA, 11);  /* F */
    MODO_SALIDA(GPIOA, 12);  /* B */
    MODO_SALIDA(GPIOB, 12);  /* A */
    MODO_SALIDA(GPIOC, 10);  /* E */
    MODO_SALIDA(GPIOC, 11);  /* C */
    MODO_SALIDA(GPIOC, 12);  /* D */
    MODO_SALIDA(GPIOD, 2);   /* G */

    /* Transistores de digitos (salidas) */
    MODO_SALIDA(GPIOB, 7);   /* T12 */
    MODO_SALIDA(GPIOC, 5);   /* T9  */
    MODO_SALIDA(GPIOC, 6);   /* T8  */
    MODO_SALIDA(GPIOC, 13);  /* T6  */

    /* LED de estado */
    MODO_SALIDA(GPIOH, 1);

    /* Fotocompuertas (entradas) */
    MODO_ENTRADA(GPIOC, 3);  /* Sumador  */
    MODO_ENTRADA(GPIOA, 1);  /* Restador */
}

static void Configurar_EXTI(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    /* EXTI1 <- PA1 (0x0) ; EXTI3 <- PC3 (0x2) */
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~((0xFu << 4) | (0xFu << 12)))
                        | (0x0u << 4) | (0x2u << 12);

    /* PC3: solo flanco de subida (+1) ; PA1: solo flanco de bajada (-1) */
    EXTI->RTSR = (EXTI->RTSR | EXTI_RTSR_TR3) & ~EXTI_RTSR_TR1;
    EXTI->FTSR = (EXTI->FTSR | EXTI_FTSR_TR1) & ~EXTI_FTSR_TR3;

    /* Limpiar pendientes y habilitar las lineas */
    EXTI->PR  = (EXTI_PR_PR1 | EXTI_PR_PR3);
    EXTI->IMR |= (EXTI_IMR_IM1 | EXTI_IMR_IM3);

    /* Ambas EXTI con la MISMA prioridad: no se interrumpen entre si,
     * asi el contador (lectura-modificacion-escritura) es seguro. */
    NVIC_SetPriority(EXTI1_IRQn, 1);
    NVIC_SetPriority(EXTI3_IRQn, 1);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI3_IRQn);
}

static void Configurar_Timers(void)
{
    RCC->APB1ENR |= (RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN);
    (void)RCC->APB1ENR;

    /* TIM2: heartbeat. 16 MHz / 16000 = 1 kHz -> interrupcion cada 500 ms */
    TIM2->PSC  = 16000u - 1u;
    TIM2->ARR  = 500u - 1u;
    TIM2->EGR  = TIM_EGR_UG;       /* carga PSC/ARR de inmediato */
    TIM2->SR   = 0;
    TIM2->DIER = TIM_DIER_UIE;

    /* TIM3: multiplexado. 16 MHz / 16 = 1 MHz -> interrupcion cada 1 ms
     * (cada digito cada 4 ms = 250 Hz de refresco, sin parpadeo visible) */
    TIM3->PSC  = 16u - 1u;
    TIM3->ARR  = 1000u - 1u;
    TIM3->EGR  = TIM_EGR_UG;
    TIM3->SR   = 0;
    TIM3->DIER = TIM_DIER_UIE;

    NVIC_SetPriority(TIM3_IRQn, 2);   /* display */
    NVIC_SetPriority(TIM2_IRQn, 3);   /* heartbeat */
    NVIC_EnableIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->CR1 |= TIM_CR1_CEN;
    TIM3->CR1 |= TIM_CR1_CEN;
}

/* ============================ DISPLAY (BAJO NIVEL) ======================== */

/* Escribe TODOS los segmentos con una sola escritura BSRR por puerto
 * (sin el paso intermedio "apagar todo y luego encender" del original). */
static void Escribir_Segmentos(uint8_t patron)
{
    uint32_t a = 0, b = 0, c = 0, d = 0;   /* pines de segmentos a ENCENDER */

    if (patron & SEG_A) b |= (1u << 12);   /* A -> PB12 */
    if (patron & SEG_B) a |= (1u << 12);   /* B -> PA12 */
    if (patron & SEG_C) c |= (1u << 11);   /* C -> PC11 */
    if (patron & SEG_D) c |= (1u << 12);   /* D -> PC12 */
    if (patron & SEG_E) c |= (1u << 10);   /* E -> PC10 */
    if (patron & SEG_F) a |= (1u << 11);   /* F -> PA11 */
    if (patron & SEG_G) d |= (1u << 2);    /* G -> PD2  */

#if DISPLAY_ANODO_COMUN
    /* Encendido = 0 (bits BR, parte alta) ; apagado = 1 (bits BS, parte baja) */
    GPIOA->BSRR = (a << 16) | (SEG_MASK_A & ~a);
    GPIOB->BSRR = (b << 16) | (SEG_MASK_B & ~b);
    GPIOC->BSRR = (c << 16) | (SEG_MASK_C & ~c);
    GPIOD->BSRR = (d << 16) | (SEG_MASK_D & ~d);
#else
    /* Encendido = 1 (BS) ; apagado = 0 (BR) */
    GPIOA->BSRR = ((SEG_MASK_A & ~a) << 16) | a;
    GPIOB->BSRR = ((SEG_MASK_B & ~b) << 16) | b;
    GPIOC->BSRR = ((SEG_MASK_C & ~c) << 16) | c;
    GPIOD->BSRR = ((SEG_MASK_D & ~d) << 16) | d;
#endif
}

static void Encender_Digito(uint8_t indice)
{
    switch (indice)
    {
        case 0:  GPIOB->BSRR = (1u << 7);  break;   /* T12 millares  */
        case 1:  GPIOC->BSRR = (1u << 5);  break;   /* T9  centenas  */
        case 2:  GPIOC->BSRR = (1u << 6);  break;   /* T8  decenas   */
        default: GPIOC->BSRR = (1u << 13); break;   /* T6  unidades  */
    }
}

/* ======================= RUTINAS DE INTERRUPCION ========================== */

/* PC3 - flanco de subida: +1 (0..LIMITE_MAX, luego vuelve a 0) */
void EXTI3_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR3)
    {
        EXTI->PR = EXTI_PR_PR3;                 /* limpiar escribiendo 1 */

        uint16_t c = contador;
        contador = (c >= LIMITE_MAX) ? 0u : (uint16_t)(c + 1u);
    }
}

/* PA1 - flanco de bajada: -1 (desde 0000 salta a LIMITE_MAX) */
void EXTI1_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR1)
    {
        EXTI->PR = EXTI_PR_PR1;

        uint16_t c = contador;
        contador = (c == 0u) ? (uint16_t)LIMITE_MAX : (uint16_t)(c - 1u);
    }
}

/* TIM2 - heartbeat PH1 */
void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF)
    {
        TIM2->SR &= ~TIM_SR_UIF;
        GPIOH->ODR ^= (1u << 1);
    }
}

/* TIM3 - multiplexado: un digito por interrupcion */
void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF)
    {
        TIM3->SR &= ~TIM_SR_UIF;

        /* 1. Apagar los 4 digitos antes de cambiar segmentos (evita fantasmas) */
        GPIOB->BSRR = (DIG_MASK_B << 16);
        GPIOC->BSRR = (DIG_MASK_C << 16);

        /* 2. Un solo snapshot del contador y extraer solo el digito necesario */
        uint16_t v = contador;
        uint8_t  d = (uint8_t)((v / divisor[digito_actual]) % 10u);

        /* 3. Segmentos del digito y luego habilitar su transistor */
        Escribir_Segmentos(tabla_7seg[d]);
        Encender_Digito(digito_actual);

        /* 4. Siguiente digito (0..3) */
        digito_actual = (uint8_t)((digito_actual + 1u) & 3u);
    }
}







