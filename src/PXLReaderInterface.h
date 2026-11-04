#pragma once
#include <inttypes.h>
#include <CUtils.h>

class PXLReaderInterface
{
	public:
		
		virtual int8_t Open() = 0;
		virtual int8_t Close() = 0;
		virtual uint32_t Read(const uint32_t offset, uint32_t length = UINT32_MAX) = 0;
		virtual const uint8_t *GetBufferPtr() = 0;
};
