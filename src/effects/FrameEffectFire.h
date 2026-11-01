#pragma once
#include <inttypes.h>
#include "FrameEffectInterface.h"
#include "CUtils_Perlin2DInt.h"

class FrameEffectFire : public FrameEffectInterface
{
	public:
		
		virtual void Init() override
		{
			_Random(rand());
			
			for(uint16_t i = 0; i < frame_width * frame_height; i++)
			{
				_fire_pixels[i] = 0;
			}
			_noise_offset = 0;
			
			return;
		}
		
		virtual void Tick(uint32_t time) override
		{
			return;
		}
		
		virtual bool FramePrepare(uint32_t time) override
		{
			_UpdateFire();
			
			return true;
		}

	private:
		
		static constexpr uint8_t _flame_height = 66;		// Высота пламени в % от высоты экрана
		static constexpr uint8_t _drift_threshold = 48;		// Пламя тусклее этого сносит в стороны
		static constexpr int16_t _noise_step_x = 64;		// Шаг шума по горизонтали
		static constexpr int16_t _noise_step_time = 32;		// Скорость изменения шума от кадра к кадру
		static constexpr int16_t _noise_base = 160;			// Средний жар у основания, шум добавляет к нему примерно +-256

		// Максимальное затухание на строку, подобранное под высоту экрана.
		// Среднее затухание _decay_max / 2, пламя гаснет за 255 / (_decay_max / 2) строк.
		// При высоте 16 и 66% получается 48, как в исходной версии.
		static constexpr uint16_t _decay_calc = (512UL * 100) / (frame_height * _flame_height);
		static constexpr uint16_t _decay_max = (_decay_calc > 0) ? _decay_calc : 1;
		
		void _UpdateFire()
		{
			// Распространение огня вверх: каждая строка берёт значения из строки ниже,
			// которая в этом кадре ещё не обновлялась
			for(uint8_t y = 0; y < frame_height - 1; y++)
			{
				const uint16_t row_dst = y * frame_width;
				const uint16_t row_src = row_dst + frame_width;
				
				for(uint8_t x = 0; x < frame_width; x++)
				{
					const uint32_t random = _Random();
					uint8_t src_x = x;
					
					// Тусклые языки пламени сносит в сторону
					if(_fire_pixels[row_src + x] < _drift_threshold)
					{
						const uint8_t shift = ((random >> 8) & 0xFF) % 3;	// 0, 1, 2 -> -1, 0, +1
						
						if(shift == 0 && x > 0)
							src_x = x - 1;
						else if(shift == 2 && x < frame_width - 1)
							src_x = x + 1;
					}
					
					const uint8_t heat = _fire_pixels[row_src + src_x];
					const uint8_t decay = (((random >> 16) & 0xFF) * _decay_max) >> 8;
					
					_fire_pixels[row_dst + x] = (heat > decay) ? heat - decay : heat / 2;
				}
				
				// Строка больше не изменится в этом кадре - сразу рисуем
				_DrawRow(row_dst);
			}
			
			// Генерация шума в нижней строке
			const uint16_t row_bottom = (frame_height - 1) * frame_width;
			for(uint8_t x = 0; x < frame_width; x++)
			{
				// Шум плавный, без заворота через & 0xFF: провалы шума дают промежутки между языками пламени
				int16_t heat = Perlin2DInt::noise2d(x * _noise_step_x, _noise_offset) + _noise_base;
				if(heat < 0)
					heat = 0;
				else if(heat > 255)
					heat = 255;

				_fire_pixels[row_bottom + x] = heat;
			}
			_noise_offset += _noise_step_time;
			
			_DrawRow(row_bottom);
			
			return;
		}
		
		void _DrawRow(uint16_t row)
		{
			for(uint8_t x = 0; x < frame_width; x++)
			{
				frame_buffer->SetPixel(row + x, _HeatToColor(_fire_pixels[row + x]));
			}
			
			return;
		}
		
		// Палитра чёрного тела: чёрный -> красный -> оранжевый -> жёлтый -> белый.
		// Шкала делится на 3 участка по 64 шага, в каждом нарастает один канал.
		inline color_t _HeatToColor(uint8_t heat) const
		{
			const uint8_t t = (heat * 191) >> 8;		// 0..190
			const uint8_t ramp = (t & 0x3F) << 2;		// 0..252 внутри участка

			color_t color;
			if(t > 0x7F)
			{
				color.R = 255;
				color.G = 255;
				color.B = ramp;
			}
			else if(t > 0x3F)
			{
				color.R = 255;
				color.G = ramp;
				color.B = 0;
			}
			else
			{
				color.R = ramp;
				color.G = 0;
				color.B = 0;
			}

			return color;
		}
		
		uint8_t _fire_pixels[frame_width * frame_height] = {0};
		int32_t _noise_offset = 0;
};
