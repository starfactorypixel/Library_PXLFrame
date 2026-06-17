#pragma once
#include <inttypes.h>
#include "PXLReaderInterface.h"

class PXLReaderSPI : public PXLReaderInterface
{
	static constexpr uint16_t _buffer_size = 256;
	
	struct file_map_t
	{
		uint32_t spi_start;				// Адрес начала файла в SPI
		uint32_t spi_length;			// Длина файла с SPI
	};
	
	public:
		
		void SetMap(file_map_t map)
		{
			_map = map;
			
			return;
		}
		
		virtual int8_t Open() override
		{
			return 0;
		}
		
		virtual int8_t Close() override
		{
			return 0;
		}
		
		virtual uint16_t Read(const uint32_t offset, uint16_t length) override
		{
			if(offset >= _map.spi_length) return 0;
			if(length > sizeof(_buffer))
				length = sizeof(_buffer);
			
			// Вычисляем доступное количество байт для чтения
			uint16_t readable_length = _map.spi_length - offset;
			uint16_t bytes_to_read = (length > readable_length) ? readable_length : length;

			// Читаем SPI
			SPI::flash.ReadBytes( (_map.spi_start + offset), _buffer, bytes_to_read);
			
			return bytes_to_read;
		}
		
		virtual const uint8_t *GetBufferPtr() override
		{
			return _buffer;
		}
		
	private:
		
		file_map_t _map = {};
		uint8_t _buffer[_buffer_size];
};
