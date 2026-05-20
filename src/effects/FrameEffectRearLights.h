#pragma once
#include <inttypes.h>
#include "FrameEffectInterface.h"

#include <PXLParser.h>
#include <readers/PXLReaderFatFS.h>

/*
	frame_buffer - Объект кадрового буфера с параметрами и флагами
	frame_width - Ширина кадра
	frame_height - Высота кадра
	Init() - Вызывается при инициализации эффекта
	Tick() - Вызывается не менее чем каждые 5 мс
	FramePrepare() - Вызывается каждые 100 мс
*/

//template <uint8_t _max_layers> 
class FrameEffectRearLights : public FrameEffectInterface
{
	static constexpr	uint8_t _max_layers = 8;
	public:

		virtual void Init() override
		{
			for(auto &layer : _layers)
			{
				layer.is_set = false;
				layer.active = false;
				layer.parser.SetAutoReopen(true);
				layer.parser.SetReader(layer.reader);
			}
			
			return;
		}
		
		virtual void Tick(uint32_t time) override
		{
			return;
		}
		
		virtual bool FramePrepare(uint32_t time) override
		{
			uint32_t time1 = HAL_GetTick();
			_Render(time);
			DEBUG_LOG_TOPIC("FramePrepare", "time: %d\n", (HAL_GetTick() - time1));
			
			return true;
		}
		
		bool AddLayer(uint8_t idx, const char *filename)
		{
			if(idx >= _max_layers) return false;
			
			auto &layer = _layers[idx];
			if(layer.is_set == true)
			{
				layer.parser.CloseFile();
			}
			
			PXLParser::error_t code = layer.parser.OpenFile(filename);
			if(code == PXLParser::ERROR_OK)
			{
				layer.is_set = true;
				return true;
			}
			
			return false;
		}
		
		void ShowLayer(uint8_t idx)
		{
			if(idx >= _max_layers) return;
			if(_layers[idx].is_set == false) return;
			
			_layers[idx].active = true;
			
			return;
		}
		
		void HideLayer(uint8_t idx)
		{
			if(idx >= _max_layers) return;
			if(_layers[idx].is_set == false) return;
			
			auto &layer = _layers[idx];
			layer.active = false;
			layer.parser.ReInit();
			
			return;
		}
		
	private:
		
		void _Render(uint32_t time)
		{
			frame_buffer->Clear();
			
			for(auto &layer : _layers)
			{
				if(layer.is_set == false) continue;
				if(layer.active == false) continue;
				
				layer.parser.GetAutoFrame(time, [&](const pxl_pixel_t &pxl_pixel)
				{
					if(pxl_pixel.color[3] < 255) return;
					
					color_t &pixel = *(color_t *)pxl_pixel.color;
					
					if(pxl_pixel.repeat > 1)
						frame_buffer->SetPixel(pxl_pixel.index, pixel, pxl_pixel.repeat);
					else
						frame_buffer->SetPixel(pxl_pixel.index, pixel);
				});
			}
			
			return;
		}
		
		struct layers_t
		{
			bool is_set;			// Флаг установленного слоя
			bool active;			// Флаг активности слоя
			PXLReaderFatFS reader;	// Объект ридера FatFS
			PXLParser parser;		// Объект парсера PXL
			
			layers_t() : is_set(false), active(false), reader(), parser(frame_width, frame_height)
			{}
		} _layers[_max_layers];
		
};
