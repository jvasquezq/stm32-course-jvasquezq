/* SECUENCIA (cada 1 segundo):
 *  1) Sube:  0000, 0001, 0002 ... 0010
 *  2) Reinicia a 0000 y baja 10 veces: 0000, 5535, 5534 ... 5526
 *     (el contador es de 16 bits: 0 - 1 = 65535 y el display muestra solo los 4 digitos menos significativos)
 *  3) Vuelve al paso 1 y repite indefinidamente.
 *
 * CONEXIONES (las mismas de la tarea original)
 *
 * Segmentos (catodos, activos en BAJO):
 *  -Segmento A -> PB12
 *  -Segmento B -> PA12
 *  -Segmento C -> PC11
 *  -Segmento D -> PC12
 *  -Segmento E -> PC10
 *  -Segmento F -> PA11
 *  -Segmento G -> PD2
 *
 * Anodos comunes (cada uno a traves de un transistor, activos en ALTO):
 *  -T12 (DIG.1, izquierda) -> PB7
 *  -T9  (DIG.2)            -> PC5
 *  -T8  (DIG.3)            -> PC6
 *  -T6  (DIG.4, derecha)   -> PC13
 */

#include "stm32f411xe.h"   // Definiciones de registros del STM32F411
#include <stm32f4xx.h>     // Cabecera general de la familia STM32F4
#include <stdint.h>        // Tipos de enteros de tamano fijo (uint8_t, uint16_t...)

// Contador de 16 bits sin signo: el desbordamiento 0 <-> 65535 es automatico
volatile uint16_t contador = 0;      // Valor de 16 bits del cual se muestran los 4 digitos decimales menos significativos
volatile uint8_t digito_actual = 0;  // Digito que se esta refrescando (0 a 3)
volatile uint8_t fase = 0;           // Fase de la demo: 0 = subiendo, 1 = bajando
volatile uint8_t pasos = 0;          // Pasos dados dentro de la fase actual (0 a 10)

// Tabla de segmentos para 0-9. Bit0=A, Bit1=B, Bit2=C, Bit3=D, Bit4=E, Bit5=F, Bit6=G
static const uint8_t tabla_segmentos[10] = {
    0x3F, // 0 -> A B C D E F
    0x06, // 1 -> B C
    0x5B, // 2 -> A B D E G
    0x4F, // 3 -> A B C D G
    0x66, // 4 -> B C F G
    0x6D, // 5 -> A C D F G
    0x7D, // 6 -> A C D E F G
    0x07, // 7 -> A B C
    0x7F, // 8 -> A B C D E F G
    0x6F  // 9 -> A B C D F G
};

// Cabecera de funciones
void init_GPIO(void);                    // Configura los pines del 7-segmentos
void init_timer2(void);                  // TIM2: base de tiempo de 1 segundo para la secuencia
void init_timer3(void);                  // TIM3: multiplexado (refresco) del display
void Mostrar_Segmentos(uint8_t numero);  // Enciende los segmentos de un digito (0-9)

// Main
int main(void)
{
    init_GPIO();     // Configuramos los pines
    init_timer3();   // Arrancamos el refresco del display
    init_timer2();   // Arrancamos la secuencia automatica

    while (1) {      // Bucle infinito
        __WFI();     // Dormimos el micro hasta la siguiente interrupcion (todo se hace en ISR)
    }
}

/*----- GPIO -----*/
void init_GPIO(void){
    // Activamos la senal de reloj de los puertos A, B, C y D
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN);
    (void)RCC->AHB1ENR; // Lectura dummy: garantiza que el reloj ya esta activo antes de usar los puertos

    // Dejamos los niveles iniciales ANTES de configurar como salida, para evitar destellos
    GPIOA->BSRR = (GPIO_BSRR_BS12 | GPIO_BSRR_BS11);                                // Segmentos B, F apagados (1 logico)
    GPIOB->BSRR = (GPIO_BSRR_BS12 | GPIO_BSRR_BR7);                                 // Segmento A apagado (1), T12 apagado (0)
    GPIOC->BSRR = (GPIO_BSRR_BS12 | GPIO_BSRR_BS11 | GPIO_BSRR_BS10                 // Segmentos D, C, E apagados (1)
                 | GPIO_BSRR_BR13 | GPIO_BSRR_BR6 | GPIO_BSRR_BR5);                 // T6, T8, T9 apagados (0)
    GPIOD->BSRR = GPIO_BSRR_BS2;                                                    // Segmento G apagado (1)

    // Puerto A: PA12 (B) y PA11 (F) como salida
    GPIOA->MODER &= ~(GPIO_MODER_MODE12 | GPIO_MODER_MODE11);                       // Limpiamos el modo (no tocamos PA13-15 del debugger)
    GPIOA->MODER |= (GPIO_MODER_MODE12_0 | GPIO_MODER_MODE11_0);                    // 01 = salida de proposito general
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT12 | GPIO_OTYPER_OT11);                        // Salida push-pull
    GPIOA->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED12 | GPIO_OSPEEDR_OSPEED11);             // Baja velocidad
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD12 | GPIO_PUPDR_PUPD11);                       // Sin pull-up ni pull-down

    // Puerto B: PB12 (A) y PB7 (T12) como salida
    GPIOB->MODER &= ~(GPIO_MODER_MODE12 | GPIO_MODER_MODE7);                        // Limpiamos el modo de PB12 y PB7
    GPIOB->MODER |= (GPIO_MODER_MODE12_0 | GPIO_MODER_MODE7_0);                     // 01 = salida
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT12 | GPIO_OTYPER_OT7);                         // Push-pull
    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED12 | GPIO_OSPEEDR_OSPEED7);              // Baja velocidad
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD12 | GPIO_PUPDR_PUPD7);                        // Sin pull-up ni pull-down

    // Puerto C: PC10 (E), PC11 (C), PC12 (D), PC13 (T6), PC6 (T8), PC5 (T9) como salida
    GPIOC->MODER &= ~(GPIO_MODER_MODE10 | GPIO_MODER_MODE11 | GPIO_MODER_MODE12 | GPIO_MODER_MODE13 | GPIO_MODER_MODE6 | GPIO_MODER_MODE5); // Limpiamos el modo
    GPIOC->MODER |= (GPIO_MODER_MODE10_0 | GPIO_MODER_MODE11_0 | GPIO_MODER_MODE12_0 | GPIO_MODER_MODE13_0 | GPIO_MODER_MODE6_0 | GPIO_MODER_MODE5_0); // 01 = salida
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT10 | GPIO_OTYPER_OT11 | GPIO_OTYPER_OT12 | GPIO_OTYPER_OT13 | GPIO_OTYPER_OT6 | GPIO_OTYPER_OT5);         // Push-pull
    GPIOC->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED10 | GPIO_OSPEEDR_OSPEED11 | GPIO_OSPEEDR_OSPEED12 | GPIO_OSPEEDR_OSPEED13 | GPIO_OSPEEDR_OSPEED6 | GPIO_OSPEEDR_OSPEED5); // Baja velocidad
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD10 | GPIO_PUPDR_PUPD11 | GPIO_PUPDR_PUPD12 | GPIO_PUPDR_PUPD13 | GPIO_PUPDR_PUPD6 | GPIO_PUPDR_PUPD5);     // Sin pull-up ni pull-down

    // Puerto D: PD2 (G) como salida
    GPIOD->MODER &= ~(GPIO_MODER_MODE2);                                            // Limpiamos el modo de PD2
    GPIOD->MODER |= (GPIO_MODER_MODE2_0);                                           // 01 = salida
    GPIOD->OTYPER &= ~(GPIO_OTYPER_OT2);                                            // Push-pull
    GPIOD->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED2);                                      // Baja velocidad
    GPIOD->PUPDR &= ~(GPIO_PUPDR_PUPD2);                                            // Sin pull-up ni pull-down
}

/*----- TIM2: base de tiempo de 1 segundo para la secuencia automatica -----*/
void init_timer2(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;     // Activamos el reloj del TIM2
    (void)RCC->APB1ENR;                     // Lectura dummy para asegurar que el reloj esta activo

    TIM2->PSC = 16000 - 1;                  // 16 MHz / 16000 = 1 kHz (1 ms por cuenta)
    TIM2->ARR = 1000 - 1;                   // 1000 cuentas = 1 segundo
    TIM2->CNT = 0;                          // Reiniciamos el contador
    TIM2->EGR = TIM_EGR_UG;                 // Forzamos un update para cargar PSC y ARR de inmediato
    TIM2->SR &= ~TIM_SR_UIF;                // Borramos la bandera que genero el update forzado
    TIM2->DIER |= TIM_DIER_UIE;             // Habilitamos la interrupcion por update

    NVIC_EnableIRQ(TIM2_IRQn);              // Activamos la interrupcion del TIM2 en el NVIC
    TIM2->CR1 |= TIM_CR1_CEN;               // Arrancamos el temporizador
}

/*----- ISR del TIM2: se ejecuta cada 1 segundo y avanza la demo -----*/
void TIM2_IRQHandler(void){
    if (TIM2->SR & TIM_SR_UIF) {            // Verificamos que sea el evento de update
        TIM2->SR &= ~TIM_SR_UIF;            // Limpiamos la bandera

        if (fase == 0) {                    // Fase 0: subiendo
            if (pasos < 10) {               // Mientras no hayamos llegado a 10...
                contador = (uint16_t)(contador + 1U); // ...sumamos 1
                pasos++;                    // Contamos el paso
            } else {                        // Ya se mostro el 10
                contador = 0;               // Volvemos a 0000
                pasos = 0;                  // Reiniciamos el conteo de pasos
                fase = 1;                   // Pasamos a la fase de bajada
            }
        } else {                            // Fase 1: bajando
            if (pasos < 10) {               // Mientras no hayamos dado 10 pasos hacia atras...
                contador = (uint16_t)(contador - 1U); // ...restamos 1 (0 - 1 = 65535 -> se ve 5535)
                pasos++;                    // Contamos el paso
            } else {                        // Ya se dieron los 10 pasos hacia atras
                contador = 0;               // Volvemos a 0000
                pasos = 0;                  // Reiniciamos el conteo de pasos
                fase = 0;                   // Volvemos a la fase de subida
            }
        }
    }
}

/*----- TIM3: refresco (multiplexado) del 7-segmentos -----*/
void init_timer3(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;     // Activamos el reloj del TIM3
    (void)RCC->APB1ENR;                     // Lectura dummy para asegurar que el reloj esta activo

    TIM3->PSC = 16 - 1;                     // 16 MHz / 16 = 1 MHz (1 us por cuenta)
    TIM3->ARR = 2500 - 1;                   // 2500 cuentas = 2.5 ms por digito -> refresco total de 100 Hz (sin parpadeo y con buen brillo)
    TIM3->CNT = 0;                          // Reiniciamos el contador
    TIM3->EGR = TIM_EGR_UG;                 // Forzamos un update para cargar PSC y ARR de inmediato
    TIM3->SR &= ~TIM_SR_UIF;                // Borramos la bandera que genero el update forzado
    TIM3->DIER |= TIM_DIER_UIE;             // Habilitamos la interrupcion por update

    NVIC_EnableIRQ(TIM3_IRQn);              // Activamos la interrupcion del TIM3 en el NVIC
    TIM3->CR1 |= TIM_CR1_CEN;               // Arrancamos el temporizador
}

/*----- ISR del TIM3: muestra un digito cada 2.5 ms -----*/
void TIM3_IRQHandler(void){
    if (TIM3->SR & TIM_SR_UIF) {            // Verificamos que sea el evento de update
        TIM3->SR &= ~TIM_SR_UIF;            // Limpiamos la bandera

        GPIOB->BSRR = GPIO_BSRR_BR7;                                        // Apagamos T12 (NPN: 0 = apagado)
        GPIOC->BSRR = (GPIO_BSRR_BR13 | GPIO_BSRR_BR6 | GPIO_BSRR_BR5);     // Apagamos T6, T8 y T9

        uint16_t valor = contador;          // Copia local para que los 4 digitos sean coherentes

        switch (digito_actual) {            // Elegimos que digito toca refrescar
            case 0:
                Mostrar_Segmentos((valor / 1000) % 10);  // Miles (si valor = 65535 -> 5)
                GPIOB->BSRR = GPIO_BSRR_BS7;             // Encendemos T12 -> DIG.1 (izquierda)
                break;
            case 1:
                Mostrar_Segmentos((valor / 100) % 10);   // Centenas (si valor = 65535 -> 5)
                GPIOC->BSRR = GPIO_BSRR_BS5;             // Encendemos T9 -> DIG.2
                break;
            case 2:
                Mostrar_Segmentos((valor / 10) % 10);    // Decenas (si valor = 65535 -> 3)
                GPIOC->BSRR = GPIO_BSRR_BS6;             // Encendemos T8 -> DIG.3
                break;
            default:
                Mostrar_Segmentos(valor % 10);           // Unidades (si valor = 65535 -> 5)
                GPIOC->BSRR = GPIO_BSRR_BS13;            // Encendemos T6 -> DIG.4 (derecha)
                break;
        }

        digito_actual++;                    // Pasamos al siguiente digito
        if (digito_actual > 3) {            // Si ya pasamos el cuarto...
            digito_actual = 0;              // ...volvemos al primero
        }
    }
}

/*----- Enciende los segmentos del digito (0-9). Segmento activo = pin en BAJO (anodo comun) -----*/
void Mostrar_Segmentos(uint8_t numero){
    uint8_t m = tabla_segmentos[numero % 10];   // Mascara de segmentos para ese digito (protegida contra valores > 9)

    GPIOB->BSRR = (m & (1U << 0)) ? GPIO_BSRR_BR12 : GPIO_BSRR_BS12; // Segmento A (PB12)
    GPIOA->BSRR = (m & (1U << 1)) ? GPIO_BSRR_BR12 : GPIO_BSRR_BS12; // Segmento B (PA12)
    GPIOC->BSRR = (m & (1U << 2)) ? GPIO_BSRR_BR11 : GPIO_BSRR_BS11; // Segmento C (PC11)
    GPIOC->BSRR = (m & (1U << 3)) ? GPIO_BSRR_BR12 : GPIO_BSRR_BS12; // Segmento D (PC12)
    GPIOC->BSRR = (m & (1U << 4)) ? GPIO_BSRR_BR10 : GPIO_BSRR_BS10; // Segmento E (PC10)
    GPIOA->BSRR = (m & (1U << 5)) ? GPIO_BSRR_BR11 : GPIO_BSRR_BS11; // Segmento F (PA11)
    GPIOD->BSRR = (m & (1U << 6)) ? GPIO_BSRR_BR2  : GPIO_BSRR_BS2;  // Segmento G (PD2)
}