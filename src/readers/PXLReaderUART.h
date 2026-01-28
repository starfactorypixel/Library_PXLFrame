#pragma once
#include <inttypes.h>
#include "PXLReaderInterface.h"
#include <stm32f1xx_hal_uart.h>

extern UART_HandleTypeDef hDebugUart;

class PXLReaderUART : public PXLReaderInterface
{
	static constexpr uint16_t _max_data_request = 1024;
	
	typedef struct
	{
		char id;
		uint32_t offset;
		uint32_t length;
		const uint8_t *data;
	} PXLS_Packet;
	
	public:
		
		virtual int8_t Open(const char *filename) override
		{
			//_HW_ReInit(true);
			_SendRequest('1', 0, 0);
			bool status = _WaitResponse('2');
			if(status == true)
			{
				return 0;
			}

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
			
			return -1;
		}
		
		virtual int8_t Close() override
		{
			_SendRequest('5', 0, 0);
			bool status = _WaitResponse('6');
			if(status == true)
			{
				//_HW_ReInit(false);
				return 0;
			}
			
			return -1;
		}
		
		virtual uint16_t Read(const uint32_t offset, uint16_t length) override
		{
			if(length > _max_data_request)
				length = _max_data_request;
			
			uint16_t result = 0;
			uint8_t attempts = 0;
			do
			{
				_SendRequest('3', offset, length);
				bool status = _WaitResponse('4', 100);
				if(status == true && _parsed_data.offset == offset && _parsed_data.length <= length)
				{
					_buffer_rx_data_ptr = &_parsed_data.data[0];
					result = _parsed_data.length;
					
					break;
				}
			} while (++attempts < 3);
			
			return result;
		}
		
		virtual const uint8_t *GetBufferPtr() override
		{
			return _buffer_rx_data_ptr;
		}
		
	private:
		
		void _HW_ReInit(bool state)
		{
			static uint32_t old_speed = 0;
			
			if(state)
			{
				if(old_speed == 0)
					old_speed = hDebugUart.Init.BaudRate;
				
				hDebugUart.Init.BaudRate = 1500000UL;
			}
			else
			{
				if(old_speed != 0)
					hDebugUart.Init.BaudRate = old_speed;
			}
			
			//HAL_UART_DeInit(&hDebugUart);
			if(HAL_UART_Init(&hDebugUart) != HAL_OK)
			{
				// Error_Handler();
			}

			return;
		}
		
		bool _HW_Print(const void *pData, uint16_t Size)
		{
			HAL_StatusTypeDef result = HAL_UART_Transmit(&hDebugUart, (uint8_t *)pData, Size, 64);
			if(result != HAL_OK)
			{
				HAL_UART_AbortTransmit(&hDebugUart);
			}
			
			return (result == HAL_OK);
		}


		void _SendRequest(char id, uint32_t offset, uint32_t length)
		{
			uint16_t buffer_tx_length = generate_pxls_packet(id, offset, length, _buffer_tx);
			
			_HW_Print(_buffer_tx, buffer_tx_length);
		}
		
		HAL_StatusTypeDef UART_Receive_WithIdle(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint16_t *pRxLen, uint32_t timeout_ms)
		{
			uint32_t start_ms = HAL_GetTick();
			uint32_t last_rx_cycles;
			uint16_t count = 0;
			const uint32_t idle_cycles = (SystemCoreClock / 1000000UL) * 1000UL; // 1 ms @ CPU freq
			
			*pRxLen = 0;
			
			// очистка возможных старых ошибок
			__HAL_UART_CLEAR_OREFLAG(huart);
			__HAL_UART_CLEAR_FEFLAG(huart);
			__HAL_UART_CLEAR_NEFLAG(huart);
			
			// ожидание первого байта
			while(__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE) == RESET)
			{
				if((HAL_GetTick() - start_ms) >= timeout_ms)
					return HAL_TIMEOUT;
				
				// если зашёл ORE ещё до первого байта
				if(__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE))
				{
					__HAL_UART_CLEAR_OREFLAG(huart);
					if (count < Size)
						pData[count++] = 0x00;	//RX_LOST_BYTE
					}
			}
				
			// первый байт
			pData[count++] = (uint8_t)huart->Instance->DR;
			last_rx_cycles = DWT->CYCCNT;
			
			// остальные байты
			while(count < Size)
			{
				// потерянный байт (ORE)
				if(__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE))
				{
					__HAL_UART_CLEAR_OREFLAG(huart);
					
					if(count < Size)
						pData[count++] = 0x00;	//RX_LOST_BYTE
					
					last_rx_cycles = DWT->CYCCNT;
					
					continue;
				}
				
				// нормальный байт
				if(__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE))
				{
					pData[count++] = (uint8_t)huart->Instance->DR;
					last_rx_cycles = DWT->CYCCNT;
					
					continue;
				}
				
				// software IDLE
				if((uint32_t)(DWT->CYCCNT - last_rx_cycles) >= idle_cycles)
					break;
			}
			
			*pRxLen = count;
			
			return HAL_OK;
		}
		
		bool _WaitResponse(char id, uint32_t timeout = 64)
		{
			uint16_t rx_len;
			NVIC_DisableIRQ(CAN1_RX1_IRQn);
			//HAL_StatusTypeDef result = HAL_UARTEx_ReceiveToIdle(&hDebugUart, _buffer_rx, sizeof(_buffer_rx), &rx_len, timeout);
			HAL_StatusTypeDef result = UART_Receive_WithIdle(&hDebugUart, _buffer_rx, sizeof(_buffer_rx), &rx_len, timeout);
			NVIC_EnableIRQ(CAN1_RX1_IRQn);
			if(result == HAL_OK)
			{
				uint8_t status = parse_pxls_packet(_buffer_rx, rx_len, &_parsed_data);
				if(status == 0 && _parsed_data.id == id)
				{
					return true;
				}
			}

			return false;
		}
		
		// Функция для конвертации uint32_t в ASCII строку
		uint8_t *u32_to_ascii(uint32_t value, uint8_t *buf)
		{
			if(value == 0)
			{
				*buf++ = '0';
				*buf = '\0';
				
				return buf;
			}
			
			char tmp[11];
			int i = 0;
			while(value)
			{
				tmp[i++] = '0' + (value % 10);
				value /= 10;
			}
			
			for(int j = i - 1; j >= 0; j--)
			{
				*buf++ = tmp[j];
			}
			*buf = '\0';
			
			return buf;
		}
		
		// Функция для конвертации ASCII строки в uint32
		uint8_t *ascii_to_u32(uint8_t *str, uint32_t *out)
		{
			uint32_t result = 0;
			
			while(*str >= '0' && *str <= '9')
			{
				result = result * 10 + (*str - '0');
				str++;
			}
			*out = result;
			
			return str;
		}
		
		uint16_t generate_pxls_packet(char id, uint32_t offset, uint32_t length, uint8_t *out_buf)
		{
			uint8_t *ptr = out_buf;
			
			*ptr++ = '+';
			memcpy(ptr, "PXLS=", 5);
			ptr += 5;
			
			*ptr++ = id;
			*ptr++ = ',';
			
			ptr = u32_to_ascii(offset, ptr);
			*ptr++ = ',';
			
			ptr = u32_to_ascii(length, ptr);
			*ptr++ = '\n';
			*ptr++ = '\n';
			*ptr = '\0';
			
			return (uint16_t)(ptr - out_buf);
		}
		
		uint8_t parse_pxls_packet(uint8_t *input, uint16_t len, PXLS_Packet *out)
		{
			if(len < 10 || input[0] != '+' || strncmp((const char *)input, "+PXLS=", 6) != 0)
				return 1;
			
			uint8_t *ptr = input + 6;
			
			out->id = *ptr++;
			if(*ptr++ != ',')
				return 2;
			
			ptr = ascii_to_u32(ptr, &out->offset);
			if(*ptr++ != ',')
				return 3;
			
			ptr = ascii_to_u32(ptr, &out->length);
			if(*ptr++ != '\n')
				return 4;
			
			// Проверка длины и извлечение data
			uint32_t expected_len = out->length;
			uint16_t remaining = len - (ptr - input);
			if(remaining < expected_len + 1) // +1 — это завершающий \n
				return 5;
			
			out->data = (const uint8_t*)ptr;
			if(ptr[expected_len] != '\n')
				return 6;
			
			return 0;
		}
		
		uint8_t _buffer_rx[_max_data_request + 64];
		uint8_t _buffer_tx[64];
		
		const uint8_t *_buffer_rx_data_ptr;
		
		PXLS_Packet _parsed_data = {};
};
