#pragma once
#include <inttypes.h>
#include "PXLReaderInterface.h"

class PXLReaderArray : public PXLReaderInterface
{
	struct file_map_t
	{
		const char *filename;			// Имя файла, не используется
		const uint8_t *array_ptr;		// Указатель на массив
		uint32_t array_length;			// Размер массива
	};
	
	public:
		
		void SetMap(file_map_t map)
		{
			_map = map;
			
			return;
		}
		
		virtual int8_t Open(const char *filename) override
		{
			return 0;
		}
		
		virtual int8_t Close() override
		{
			return 0;
		}
		
		virtual uint16_t Read(const uint32_t offset, uint16_t length) override
		{
			if(offset >= _map.array_length) return 0;
			
			// Вычисляем доступное количество байт для чтения
			uint16_t readable_length = _map.array_length - offset;
			uint16_t bytes_to_read = (length > readable_length) ? readable_length : length;
			
			// Устанавливаем указатель на начало чтения
			_file_ptr = &_map.array_ptr[offset];
			
			return bytes_to_read;
		}
		
		virtual const uint8_t *GetBufferPtr() override
		{
			return _file_ptr;
		}

	private:
		
		file_map_t _map;
		const uint8_t *_file_ptr = nullptr;
};
