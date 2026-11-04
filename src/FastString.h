#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <string.h>

// Блокирующие функции заполнения и копирования памяти.
// *_dma32: dst и src должны быть выровнены на 4 байта (DMA игнорирует младшие биты адреса).
//          size может быть не кратен 4: целые слова пересылаются по 32 бита, хвост дописывается процессором.
// memset_dma32 заполняет 32-битным шаблоном c (в памяти little-endian), memset_dma8 - байтом c.

#ifdef STM32F1

#include "stm32f1xx_hal.h"

// Номер свободного канала DMA1 (1..7). Можно переопределить до подключения заголовка.
#ifndef FASTSTRING_DMA_CHANNEL
	#define FASTSTRING_DMA_CHANNEL 7
#endif

static DMA_Channel_TypeDef *const DMA_Channel = (DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + (FASTSTRING_DMA_CHANNEL - 1) * 0x14UL);

// Флаги канала в DMA1->ISR / DMA1->IFCR: GIF, TCIF, HTIF, TEIF по 4 бита на канал
static constexpr uint32_t _dma_flags_shift = (FASTSTRING_DMA_CHANNEL - 1) * 4;
static constexpr uint32_t _dma_flags_all = (0x0FUL << _dma_flags_shift);
static constexpr uint32_t _dma_flag_tc = (0x02UL << _dma_flags_shift);
static constexpr uint32_t _dma_flag_te = (0x08UL << _dma_flags_shift);

// Пересылка память-память, блокирующая. count - кол-во пересылок, src_step/dst_step - шаг адреса на пересылку в байтах.
// ccr - биты размера данных и инкремента адресов. Размер CNDTR 16 бит, поэтому длинные пересылки идут частями.
// Возвращает false при ошибке шины DMA.
inline bool _dma_mem_transfer(uint32_t src, uint32_t dst, uint32_t count, uint32_t ccr, uint32_t src_step, uint32_t dst_step)
{
	// Включаем тактирование DMA1
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;

	// Записи процессора должны попасть в память до старта DMA. __DSB() также барьер для компилятора.
	__DSB();

	bool result = true;
	while(count > 0)
	{
		uint32_t chunk = (count > 0xFFFFUL) ? 0xFFFFUL : count;

		// Сбрасываем настройки и флаги канала
		DMA_Channel->CCR = 0;
		DMA1->IFCR = _dma_flags_all;

		// Memory-to-Memory: CPAR - источник, CMAR - приемник
		DMA_Channel->CPAR = src;
		DMA_Channel->CMAR = dst;
		DMA_Channel->CNDTR = chunk;
		DMA_Channel->CCR = ccr | DMA_CCR_MEM2MEM | DMA_CCR_PL_1;

		// Запуск DMA
		DMA_Channel->CCR |= DMA_CCR_EN;

		// Ожидаем завершения передачи или ошибки
		uint32_t isr;
		do
		{
			isr = DMA1->ISR & (_dma_flag_tc | _dma_flag_te);
		} while(isr == 0);

		// Останавливаем DMA
		DMA_Channel->CCR = 0;
		DMA1->IFCR = _dma_flags_all;

		if(isr & _dma_flag_te)
		{
			result = false;
			break;
		}

		src += chunk * src_step;
		dst += chunk * dst_step;
		count -= chunk;
	}

	// Барьер компилятора: память изменена DMA
	__asm volatile("" ::: "memory");

	return result;
}

inline void memset_dma8(uint8_t *dst, uint8_t c, size_t size)
{
	static volatile uint8_t value;
	value = c;

	// 8 бит: биты MSIZE/PSIZE = 00. Инкремент только адреса приемника.
	_dma_mem_transfer((uint32_t)&value, (uint32_t)dst, size, DMA_CCR_MINC, 0, 1);

	return;
}

inline void memset_dma32(uint8_t *dst, uint32_t c, size_t size)
{
	static volatile uint32_t value;
	value = c;

	size_t words = size / 4;

	// 32 бита: MSIZE/PSIZE = 10. Инкремент только адреса приемника.
	_dma_mem_transfer((uint32_t)&value, (uint32_t)dst, words, DMA_CCR_MINC | DMA_CCR_MSIZE_1 | DMA_CCR_PSIZE_1, 0, 4);

	// Хвост, не кратный 4 байтам
	for(size_t i = words * 4; i < size; ++i)
		dst[i] = (uint8_t)(c >> (8 * (i % 4)));

	return;
}

inline void memcpy_dma8(uint8_t *dst, const uint8_t *src, size_t size)
{
	// 8 бит: биты MSIZE/PSIZE = 00. Инкремент адресов источника и приемника.
	_dma_mem_transfer((uint32_t)src, (uint32_t)dst, size, DMA_CCR_MINC | DMA_CCR_PINC, 1, 1);

	return;
}

inline void memcpy_dma32(uint8_t *dst, const uint8_t *src, size_t size)
{
	size_t words = size / 4;

	// 32 бита: MSIZE/PSIZE = 10. Инкремент адресов источника и приемника.
	_dma_mem_transfer((uint32_t)src, (uint32_t)dst, words, DMA_CCR_MINC | DMA_CCR_PINC | DMA_CCR_MSIZE_1 | DMA_CCR_PSIZE_1, 4, 4);

	// Хвост, не кратный 4 байтам
	memcpy(dst + words * 4, src + words * 4, size - words * 4);

	return;
}

#else

// Остальные платформы: процессорные версии с той же семантикой.

#ifdef STM32H7
	#include "stm32h7xx_hal.h"
#endif

inline void memset_dma8(uint8_t *dst, uint8_t c, size_t size)
{
	memset(dst, c, size);

	return;
}

inline void memset_dma32(uint8_t *dst, uint32_t c, size_t size)
{
	// Все 4 байта шаблона одинаковые - достаточно обычного memset
	if(c == 0x01010101UL * (uint8_t)c)
	{
		memset(dst, (uint8_t)c, size);

		return;
	}

	size_t words = size / 4;
	uint32_t *dst32 = (uint32_t *)dst;
	for(size_t i = 0; i < words; ++i)
		dst32[i] = c;

	// Хвост, не кратный 4 байтам
	for(size_t i = words * 4; i < size; ++i)
		dst[i] = (uint8_t)(c >> (8 * (i % 4)));

	return;
}

inline void memcpy_dma8(uint8_t *dst, const uint8_t *src, size_t size)
{
	memcpy(dst, src, size);

	return;
}

inline void memcpy_dma32(uint8_t *dst, const uint8_t *src, size_t size)
{
	memcpy(dst, src, size);

	return;
}

#endif

// Заполняет count элементов размером elem_size значением src, удваивая уже скопированную часть.
// src не должен указывать внутрь области dest.
inline void memcpy_repeat_fast(uint8_t *dest, const uint8_t *src, size_t elem_size, size_t count)
{
	if(count == 0) return;

	size_t total_copied = 1;
	memcpy(dest, src, elem_size);
	while(total_copied < count)
	{
		size_t copy_now = (count - total_copied) < total_copied ? (count - total_copied) : total_copied;
		memcpy(dest + total_copied * elem_size, dest, copy_now * elem_size);
		total_copied += copy_now;
	}

	return;
}
