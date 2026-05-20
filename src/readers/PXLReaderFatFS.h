#pragma once
#include <inttypes.h>
#include "PXLReaderInterface.h"
#include <ff.h>

class PXLReaderFatFS : public PXLReaderInterface
{
	public:
		
		virtual int8_t Open(const char *filename) override
		{
			FRESULT result = f_open(&_file_obj, filename, FA_READ);
			_file_is_open = (result == FR_OK);
			
			return result;
		}
		
		virtual int8_t Close() override
		{
			FRESULT result = f_close(&_file_obj);
			_file_is_open = false;
			
			return result;
		}
		
		virtual uint16_t Read(const uint32_t offset, uint16_t length) override
		{
			if(_file_is_open == false) return 0;
			
			if(length > sizeof(_buffer))
				length = sizeof(_buffer);
			
			FRESULT res;
			UINT read_size = 0;
			
			res = f_lseek(&_file_obj, offset);
			if(res != FR_OK) return 0;
			
			res = f_read(&_file_obj, _buffer, length, &read_size);
			if(res != FR_OK) return 0;
			
			return (uint16_t)read_size;
		}
		
		virtual const uint8_t *GetBufferPtr() override
		{
			return _buffer;
		}
		
	private:
		
		FIL _file_obj;
		bool _file_is_open = false;
		
		static uint8_t _buffer[512];
};

uint8_t PXLReaderFatFS::_buffer[512] = {0};
