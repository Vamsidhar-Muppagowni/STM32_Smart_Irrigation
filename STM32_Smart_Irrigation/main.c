#include "stm32f4xx.h"
#include <stdio.h>

// Millisecond counter for SysTick
volatile uint32_t msTicks = 0;

// -------- Main Application Defines --------
#define ADC_MAX 4095
#define ADC_MIN 0

#define MOISTURE_THRESHOLD_ON 40
#define MOISTURE_THRESHOLD_OFF 50
#define WATER_LEVEL_THRESHOLD 1000

// -------- I2C LCD Defines --------
#define PCF8574_ADDR 0x4E
#define I2C_TIMEOUT 1000  // 1 second timeout

// -------- Function Prototypes --------
void SystemInit(void);
void delay_ms(uint32_t ms);
void GPIO_Init(void);
void ADC1_Init(void);
uint16_t ADC1_Read(uint8_t channel);
void UART2_Init(void);
void UART2_SendString(char *str);
void I2C1_Init(void);
uint8_t I2C1_Write(uint8_t addr, uint8_t data);
void I2C1_Reset(void);
void LCD_Init(void);
void LCD_SendCmd(uint8_t cmd);
void LCD_SendData(uint8_t data);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(char *str);

// Global flag for LCD status
volatile uint8_t lcd_available = 0;

// -------- SysTick Interrupt Handler --------
void SysTick_Handler(void) {
    msTicks++;
}

// -------- MAIN PROGRAM --------
int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    ADC1_Init();
    UART2_Init();
    GPIO_Init();
    
    UART2_SendString("Soil + Water Sensor System with LCD\r\n");
    
    // Initialize I2C and LCD with error handling
    I2C1_Init();
    delay_ms(100);
    
    LCD_Init();
    if (lcd_available) {
        LCD_SetCursor(0, 0);
        LCD_Print("System Ready");
        LCD_SetCursor(1, 0);
        LCD_Print("Initializing...");
        delay_ms(2000);
        LCD_SendCmd(0x01);
        delay_ms(5);
    } else {
        UART2_SendString("LCD not detected - continuing with UART only\r\n");
    }

    uint32_t last_update = 0;

    while (1)
    {
        if ((msTicks - last_update) >= 500)
        {
            // --- Sensor Reading ---
            uint16_t soilValue = ADC1_Read(0);
            uint16_t waterValue = ADC1_Read(1);

            uint16_t moisturePercent = 100 - ((soilValue * 100) / ADC_MAX);
            uint16_t waterPercent = (waterValue * 100) / ADC_MAX;

            // --- UART Output ---
            char uart_msg[120];
            sprintf(uart_msg, "Soil ADC: %u\tMoisture: %u%%\tWater ADC: %u\r\n", 
                    soilValue, moisturePercent, waterValue);
            UART2_SendString(uart_msg);

            // --- LCD Output (with error handling) ---
            if (lcd_available) {
                char lcd_msg[17];
                
                sprintf(lcd_msg, "Soil: %3u%%      ", moisturePercent);
                LCD_SetCursor(0, 0);
                LCD_Print(lcd_msg);

                sprintf(lcd_msg, "Water: %3u%%     ", waterPercent);
                LCD_SetCursor(1, 0);
                LCD_Print(lcd_msg);
                
                // If LCD communication fails, disable it
                if (!lcd_available) {
                    UART2_SendString("LCD communication lost\r\n");
                }
            }

            // --- Control Logic ---
            if (moisturePercent < MOISTURE_THRESHOLD_ON)
                GPIOC->ODR |= (1 << 14);
            else
                GPIOC->ODR &= ~(1 << 14);

            if (waterValue < WATER_LEVEL_THRESHOLD)
                GPIOA->ODR |= (1 << 5);
            else
                GPIOA->ODR &= ~(1 << 5);

            if ((moisturePercent < MOISTURE_THRESHOLD_ON) && 
                (waterValue > WATER_LEVEL_THRESHOLD)) {
                GPIOC->ODR |= (1 << 13);
                GPIOB->MODER |= (1U << (3 * 2));
            }
            else if (moisturePercent > MOISTURE_THRESHOLD_OFF) {
                GPIOC->ODR &= ~(1 << 13);
                GPIOB->MODER &= ~(1U << (3 * 2));
            }

            last_update = msTicks;
        }
    }
}

// -------- ADC INITIALIZATION --------
void ADC1_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER |= (3U << (0 * 2)) | (3U << (1 * 2));
    GPIOA->PUPDR &= ~((3U << (0 * 2)) | (3U << (1 * 2)));

    ADC1->CR2 = 0;
    ADC1->CR2 |= ADC_CR2_ADON;
}

// -------- ADC READ --------
uint16_t ADC1_Read(uint8_t channel)
{
    ADC1->SQR3 = channel;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    
    uint32_t timeout = msTicks + 10;
    while (!(ADC1->SR & ADC_SR_EOC)) {
        if (msTicks > timeout) return 0;
    }
    return ADC1->DR;
}

// -------- UART2 INITIALIZATION --------
void UART2_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER &= ~((3U << (2*2)) | (3U << (3*2)));
    GPIOA->MODER |= (2U << (2*2)) | (2U << (3*2));
    GPIOA->AFR[0] |= (7U << (2*4)) | (7U << (3*4));

    USART2->BRR = 0x0683;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

// -------- UART SEND STRING --------
void UART2_SendString(char *str)
{
    while (*str)
    {
        while (!(USART2->SR & USART_SR_TXE));
        USART2->DR = (*str++ & 0xFF);
    }
}

// -------- DELAY --------
void delay_ms(uint32_t ms)
{
    uint32_t startTicks = msTicks;
    while ((msTicks - startTicks) < ms);
}

// -------- GPIO INITIALIZATION --------
void GPIO_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    GPIOC->MODER &= ~(3U << (13 * 2));
    GPIOC->MODER |= (1U << (13 * 2));

    GPIOC->MODER &= ~(3U << (14 * 2));
    GPIOC->MODER |= (1U << (14 * 2));

    GPIOA->MODER &= ~(3U << (5 * 2));
    GPIOA->MODER |= (1U << (5 * 2));

    GPIOB->MODER &= ~(3U << (3 * 2));

    GPIOC->ODR &= ~((1 << 13) | (1 << 14));
    GPIOA->ODR &= ~(1 << 5);
}

// -------- I2C1 RESET --------
void I2C1_Reset(void)
{
    I2C1->CR1 |= (1<<15);  // Software reset
    delay_ms(10);
    I2C1->CR1 &= ~(1<<15);
    delay_ms(10);
}

// -------- I2C1 INITIALIZATION --------
void I2C1_Init(void)
{
    RCC->AHB1ENR |= (1<<1);
    RCC->APB1ENR |= (1<<21);

    // Configure PB6, PB7 as AF4, open-drain, pull-up
    GPIOB->MODER &= ~((3<<12)|(3<<14));
    GPIOB->MODER |=  ((2<<12)|(2<<14));
    GPIOB->OTYPER |= (1<<6)|(1<<7);
    GPIOB->OSPEEDR |= (3<<12)|(3<<14);
    GPIOB->PUPDR &= ~((3<<12)|(3<<14));
    GPIOB->PUPDR |= (1<<12)|(1<<14);
    GPIOB->AFR[0] &= ~((0xF<<24)|(0xF<<28));
    GPIOB->AFR[0] |= (4<<24) | (4<<28);

    // Reset I2C
    I2C1_Reset();

    // Configure I2C1
    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;
    I2C1->CR1 |= (1<<0);
    
    delay_ms(50);
}

// -------- I2C WRITE WITH TIMEOUT --------
uint8_t I2C1_Write(uint8_t addr, uint8_t data)
{
    uint32_t timeout;
    
    // Wait until bus not busy (with timeout)
    timeout = msTicks + I2C_TIMEOUT;
    while (I2C1->SR2 & (1<<1)) {
        if (msTicks > timeout) {
            I2C1_Reset();
            lcd_available = 0;
            return 0;
        }
    }
    
    // Generate START
    I2C1->CR1 |= (1<<8);
    timeout = msTicks + 100;
    while (!(I2C1->SR1 & (1<<0))) {
        if (msTicks > timeout) {
            I2C1->CR1 |= (1<<9);
            I2C1_Reset();
            lcd_available = 0;
            return 0;
        }
    }
    
    // Send address
    I2C1->DR = addr;
    timeout = msTicks + 100;
    while (!(I2C1->SR1 & (1<<1))) {
        if (msTicks > timeout) {
            I2C1->CR1 |= (1<<9);
            I2C1_Reset();
            lcd_available = 0;
            return 0;
        }
    }
    (void)I2C1->SR2;
    
    // Wait TXE
    timeout = msTicks + 100;
    while (!(I2C1->SR1 & (1<<7))) {
        if (msTicks > timeout) {
            I2C1->CR1 |= (1<<9);
            I2C1_Reset();
            lcd_available = 0;
            return 0;
        }
    }
    
    // Send data
    I2C1->DR = data;
    
    // Wait BTF
    timeout = msTicks + 100;
    while (!(I2C1->SR1 & (1<<2))) {
        if (msTicks > timeout) {
            I2C1->CR1 |= (1<<9);
            I2C1_Reset();
            lcd_available = 0;
            return 0;
        }
    }
    
    // Generate STOP
    I2C1->CR1 |= (1<<9);
    delay_ms(1);
    
    return 1;
}

// -------- LCD FUNCTIONS --------
void LCD_SendCmd(uint8_t cmd)
{
    if (!lcd_available) return;
    
    uint8_t high = (cmd & 0xF0);
    uint8_t low = ((cmd << 4) & 0xF0);

    if (!I2C1_Write(PCF8574_ADDR, high | 0x0C)) return;
    delay_ms(2);
    if (!I2C1_Write(PCF8574_ADDR, high | 0x08)) return;
    delay_ms(2);

    if (!I2C1_Write(PCF8574_ADDR, low | 0x0C)) return;
    delay_ms(2);
    if (!I2C1_Write(PCF8574_ADDR, low | 0x08)) return;
    delay_ms(2);
}

void LCD_SendData(uint8_t data)
{
    if (!lcd_available) return;
    
    uint8_t high = (data & 0xF0);
    uint8_t low = ((data << 4) & 0xF0);

    if (!I2C1_Write(PCF8574_ADDR, high | 0x0D)) return;
    delay_ms(2);
    if (!I2C1_Write(PCF8574_ADDR, high | 0x09)) return;
    delay_ms(2);

    if (!I2C1_Write(PCF8574_ADDR, low | 0x0D)) return;
    delay_ms(2);
    if (!I2C1_Write(PCF8574_ADDR, low | 0x09)) return;
    delay_ms(2);
}

void LCD_Init(void)
{
    lcd_available = 1;
    
    delay_ms(100);
    
    // Try to initialize LCD
    if (!I2C1_Write(PCF8574_ADDR, 0x00)) {
        lcd_available = 0;
        return;
    }
    
    delay_ms(50);
    LCD_SendCmd(0x33);
    delay_ms(10);
    LCD_SendCmd(0x32);
    delay_ms(10);
    LCD_SendCmd(0x28);
    delay_ms(5);
    LCD_SendCmd(0x0C);
    delay_ms(5);
    LCD_SendCmd(0x06);
    delay_ms(5);
    LCD_SendCmd(0x01);
    delay_ms(10);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    if (!lcd_available) return;
    uint8_t addr = (row == 0 ? 0x80 : 0xC0) + col;
    LCD_SendCmd(addr);
}

void LCD_Print(char *str)
{
    if (!lcd_available) return;
    while (*str) {
        LCD_SendData(*str++);
    }
}