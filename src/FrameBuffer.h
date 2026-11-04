#pragma once
#include <inttypes.h>
#include "FastString.h"

class FrameBuffer
{

#if not DISPLAY_WIDTH > 0 or not DISPLAY_HEIGHT > 0
	#error You must specify 'DISPLAY_WIDTH' and 'DISPLAY_HEIGHT' before including 'FrameBuffer.h'
#endif
	
	public:
		
		struct __attribute__((__packed__)) color_t
		{
			uint8_t R;
			uint8_t G;
			uint8_t B;
		};
		
		struct __attribute__((aligned(4))) frame_buffer_t
		{
			union __attribute__((aligned(4)))
			{
				// Массив байт кадрового буфера
				uint8_t raw[(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(color_t))];

				// Массив пикселей кадрового буфера
				color_t pixels[(DISPLAY_WIDTH * DISPLAY_HEIGHT)];
				
				// Массив 32-битных слов кадрового буфера для Prepare(). Размер union не меняется.
				// Байты выравнивания за последним пикселем обнуляет Clear().
				uint32_t words[((DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(color_t)) + 3) / 4];
			};
		};
		
		volatile bool is_sending = false;						// Флаг активной отправки данных на экран
		volatile bool is_ready_sending = false;					// Флаг готовности отправки данных на экран
		volatile bool is_rendered = false;						// Флаг готовности кадра
		static constexpr uint8_t frame_width = DISPLAY_WIDTH;	// Ширина кадра
		static constexpr uint8_t frame_height = DISPLAY_HEIGHT;	// Высота кадра
		frame_buffer_t frame_buffer;							// Массив пикселей и байт
		
		static constexpr color_t color_black = {0x00, 0x00, 0x00};
		
		
		FrameBuffer() : _brightness(127), _corr_red(255), _corr_green(255), _corr_blue(255)
		{}
		
		
		// Возвращает true когда кадр готов к отрисовки
		bool DrawIsReady()
		{
			return (is_sending == false && is_ready_sending == true);
		}
		
		// Подготавливает кадр и блокирует работу на момент отрисовки
		void DrawBegin()
		{
			is_sending = true;
			is_ready_sending = false;
			Prepare();
			
			return;
		}
		
		// Снимает блокировку на момент отрисовки
		void DrawEnding()
		{
			is_sending = false;

			return;
		}

		void SetBrightness(uint8_t brightness)
		{
			_brightness = brightness;

			return;
		}
		
		void SetColorCorrection(uint8_t red, uint8_t green, uint8_t blue)
		{
			_corr_red = red;
			_corr_green = green;
			_corr_blue = blue;
			
			return;
		}
		
		// Применяет яркость и коррекцию цвета прямо в буфере: перед каждым выводом кадр нужно рисовать заново.
		void Prepare()
		{
			uint8_t scale_r = (_corr_red * _brightness) / 255;
			uint8_t scale_g = (_corr_green * _brightness) / 255;
			uint8_t scale_b = (_corr_blue * _brightness) / 255;
			
			// Обработка словами по 32 бита, нулевое слово пропускается. Компоненты в словах: R G B R | G B R G | B R G B.
			// scale[c .. c+3] - коэффициенты слова, начинающегося с компоненты c (0 - R, 1 - G, 2 - B).
			const uint8_t scale[6] = {scale_r, scale_g, scale_b, scale_r, scale_g, scale_b};
			uint8_t c = 0;
			
			for(uint32_t &word : frame_buffer.words)
			{
				if(word != 0)
				{
					uint8_t *byte = (uint8_t *)&word;
					byte[0] = (byte[0] * scale[c + 0]) / 255;
					byte[1] = (byte[1] * scale[c + 1]) / 255;
					byte[2] = (byte[2] * scale[c + 2]) / 255;
					byte[3] = (byte[3] * scale[c + 3]) / 255;
				}
				
				c = (c == 2) ? 0 : c + 1;
			}
			
			return;
		}
		
		void GetPixel(uint16_t idx, color_t &pixel, bool clear)
		{
			if(idx >= sizeofarray(frame_buffer.pixels)) return;

			pixel = frame_buffer.pixels[idx];
			if(clear == true)
				frame_buffer.pixels[idx] = color_black;
			
			return;
		}
		
		void GetPixel(uint8_t x, uint8_t y, color_t &pixel, bool clear)
		{
			if(x >= frame_width || y >= frame_height) return;

			uint16_t idx = x + (y * frame_width);
			pixel = frame_buffer.pixels[idx];
			if(clear == true)
				frame_buffer.pixels[idx] = color_black;
			
			return;
		}

		void SetPixel(uint16_t idx, const color_t &pixel)
		{
			if(idx >= sizeofarray(frame_buffer.pixels)) return;

			frame_buffer.pixels[idx] = pixel;

			return;
		}
		
		void SetPixel(uint8_t x, uint8_t y, const color_t &pixel)
		{
			if(x >= frame_width || y >= frame_height) return;
			
			uint16_t idx = x + (y * frame_width);
			frame_buffer.pixels[idx] = pixel;
			
			return;
		}
		
		void SetPixel(uint16_t idx, const color_t &pixel, uint16_t count)
		{
			if(idx >= sizeofarray(frame_buffer.pixels)) return;
			
			// Серия, выходящая за буфер, обрезается по его концу
			if(idx + count > sizeofarray(frame_buffer.pixels))
				count = sizeofarray(frame_buffer.pixels) - idx;
			
			memcpy_repeat_fast( (uint8_t *)&frame_buffer.pixels[idx], (uint8_t *)&pixel, sizeof(pixel), count );
			
			return;
		}
		
		void SetPixel(uint8_t x, uint8_t y, const color_t &pixel, uint16_t count)
		{
			if(x >= frame_width || y >= frame_height) return;
			
			uint16_t idx = x + (y * frame_width);
			return SetPixel(idx, pixel, count);
		}
		
		void Clear()
		{
			memset_dma32(frame_buffer.raw, 0x00000000, sizeof(frame_buffer));

			return;
		}
		
		void AdjustBrightness(color_t &color, uint8_t scale_r, uint8_t scale_g, uint8_t scale_b)
		{
			if( (color.R | color.G | color.B) == 0 ) return;
			
			color.R = (color.R * scale_r) / 255;
			color.G = (color.G * scale_g) / 255;
			color.B = (color.B * scale_b) / 255;
			
			return;
		}
		
	private:
		
		uint8_t _brightness;
		uint8_t _corr_red;
		uint8_t _corr_green;
		uint8_t _corr_blue;

};
