#pragma once
#include <inttypes.h>
#include <stdlib.h>
#include "FrameEffectInterface.h"

class FrameEffectGameOfLife : public FrameEffectInterface
{
	public:
		
		virtual void Init() override
		{
			_InitRandomGeneration(rand());

			return;
		}
		
		virtual void Tick(uint32_t time) override
		{
			return;
		}

		// Поколение считается порциями по _rows_per_call строк за вызов,
		// пока кадр не готов - возвращаем false, чтобы не блокировать loop.
		virtual bool FramePrepare(uint32_t time) override
		{
			uint16_t row_end = _current_row + _rows_per_call;
			if(row_end > frame_height)
				row_end = frame_height;
			
			_UpdateRows(_current_row, row_end);
			_current_row = row_end;
			
			if(_current_row < frame_height)
				return false;
			
			_FinishGeneration(time);
			
			return true;
		}
		
	private:
		
		static constexpr uint16_t _buffer_size = (frame_width * frame_height + 7) / 8;
		static constexpr uint8_t _rows_per_call = 4;		// Строк за один вызов FramePrepare (frame_height - всё за раз)
		static constexpr uint8_t _history_size = 8;			// Обнаруживает осцилляторы с периодом до 8
		static constexpr uint8_t _stagnation_limit = 60;	// Сколько поколений показывать застывшую картинку
		static constexpr uint16_t _generation_limit = 2000;	// Страховка от вечных планеров на торе
		static constexpr uint8_t _init_density = 85;		// Плотность начальной генерации: 85 / 256 ~ 1/3
		
		void _InitRandomGeneration(uint32_t seed)
		{
			_rng_state ^= seed;
			if(_rng_state == 0)
				_rng_state = 0x2545F491;
			
			for(uint16_t i = 0; i < frame_width * frame_height; i++)
			{
				_SetBit(_current_generation, i, (_Random() & 0xFF) < _init_density);
			}
			
			for(uint8_t i = 0; i < _history_size; i++)
			{
				_history[i] = 0;
			}
			_history_pos = 0;
			_stagnation_count = 0;
			_generation_count = 0;
			_current_row = 0;
			_population = 0xFFFF;
			_population_next = 0;
			
			return;
		}
		
		// Отрисовывает строки [row_begin, row_end) текущего поколения и вычисляет их в следующем.
		// Поле замкнуто в тор. Соседи считаются скользящим окном из сумм столбцов 3x1,
		// поэтому на каждую клетку читается 3 бита вместо 8.
		void _UpdateRows(uint16_t row_begin, uint16_t row_end)
		{
			for(uint16_t y = row_begin; y < row_end; y++)
			{
				const uint16_t row_up   = ((y == 0) ? frame_height - 1 : y - 1) * frame_width;
				const uint16_t row_mid  = y * frame_width;
				const uint16_t row_down = ((y == frame_height - 1) ? 0 : y + 1) * frame_width;
				
				uint8_t sum_left = _ColumnSum(row_up, row_mid, row_down, frame_width - 1);
				uint8_t sum_mid  = _ColumnSum(row_up, row_mid, row_down, 0);
				
				for(uint16_t x = 0; x < frame_width; x++)
				{
					const uint8_t sum_right = _ColumnSum(row_up, row_mid, row_down, (x == frame_width - 1) ? 0 : x + 1);
					
					const uint16_t index_2d = row_mid + x;
					const bool isAlive = _GetBit(_current_generation, index_2d);
					const uint8_t neighbors = sum_left + sum_mid + sum_right - isAlive;
					const bool willLive = (neighbors == 3) || (isAlive && neighbors == 2);
					
					_SetBit(_next_generation, index_2d, willLive);
					frame_buffer->SetPixel(index_2d, isAlive ? _color_green : _color_black);
					
					_population_next += willLive;
					
					sum_left = sum_mid;
					sum_mid = sum_right;
				}
			}
			
			return;
		}
		
		void _FinishGeneration(uint32_t time)
		{
			uint8_t *swap = _current_generation;
			_current_generation = _next_generation;
			_next_generation = swap;
			
			_population = _population_next;
			_population_next = 0;
			_current_row = 0;
			_generation_count++;
			
			if(_IsStagnant())
				_InitRandomGeneration(time);
			
			return;
		}
		
		// Поле вымерло, застыло или зациклилось (период <= _history_size)
		bool _IsStagnant()
		{
			if(_population == 0)
				return true;
			
			if(_generation_count >= _generation_limit)
				return true;
			
			const uint32_t hash = _HashGeneration(_current_generation);
			
			bool repeated = false;
			for(uint8_t i = 0; i < _history_size; i++)
			{
				if(_history[i] == hash)
				{
					repeated = true;
					break;
				}
			}
			
			_history[_history_pos] = hash;
			_history_pos = (_history_pos + 1) % _history_size;
			
			_stagnation_count = (repeated) ? _stagnation_count + 1 : 0;
			
			return _stagnation_count >= _stagnation_limit;
		}
		
		inline uint8_t _ColumnSum(uint16_t row_up, uint16_t row_mid, uint16_t row_down, uint16_t x) const
		{
			return _GetBit(_current_generation, row_up + x) + 
				   _GetBit(_current_generation, row_mid + x) + 
				   _GetBit(_current_generation, row_down + x);
		}
		
		// FNV-1a
		uint32_t _HashGeneration(const uint8_t *buffer) const
		{
			uint32_t hash = 2166136261UL;
			for(uint16_t i = 0; i < _buffer_size; i++)
			{
				hash ^= buffer[i];
				hash *= 16777619UL;
			}
			
			return hash;
		}
		
		// xorshift32: быстрее rand() и не трогает глобальное состояние
		uint32_t _Random()
		{
			_rng_state ^= _rng_state << 13;
			_rng_state ^= _rng_state >> 17;
			_rng_state ^= _rng_state << 5;
			
			return _rng_state;
		}
		
		inline void _SetBit(uint8_t *buffer, uint16_t index, bool value)
		{
			const uint8_t mask = 1 << (index & 0x07);
			
			if(value)
				buffer[index >> 3] |= mask;
			else
				buffer[index >> 3] &= ~mask;
			
			return;
		}
		
		inline bool _GetBit(const uint8_t *buffer, uint16_t index) const
		{
			return (buffer[index >> 3] >> (index & 0x07)) & 0x01;
		}
		
		uint8_t _buffer_a[_buffer_size] = {0};
		uint8_t _buffer_b[_buffer_size] = {0};
		uint8_t *_current_generation = _buffer_a;
		uint8_t *_next_generation = _buffer_b;
		
		uint32_t _history[_history_size] = {0};
		uint8_t _history_pos = 0;
		uint8_t _stagnation_count = 0;
		uint16_t _generation_count = 0;
		uint16_t _current_row = 0;
		uint16_t _population = 0xFFFF;
		uint16_t _population_next = 0;
		
		uint32_t _rng_state = 0x2545F491;

		color_t _color_black = {0x00, 0x00, 0x00};
		color_t _color_green = {0x00, 0xFF, 0x00};

};
