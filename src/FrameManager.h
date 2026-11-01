#pragma once
#include <inttypes.h>
#include "FrameBuffer.h"
#include "FrameEffectInterface.h"

class FrameManager
{
	static constexpr uint32_t _tick_time = 5;
	
	public:
		
		FrameManager(FrameBuffer &frame_buffer) : frame_buffer(&frame_buffer)
		{
			return;
		}
		
		void SelectEffect(FrameEffectInterface &effect, uint32_t frame_rate = 100)
		{
			_effect = &effect;
			_frame_rate = frame_rate;
			effect.PrepareInit(*frame_buffer);
			effect.Init();
		
			return;
		}
		
		void Tick(uint32_t time)
		{
			if(_effect == nullptr) return;

			if(time - last_tick >= _tick_time)
			{
				last_tick = time;
				
				_effect->Tick(time);
			}
			
			if(frame_buffer->is_sending == false && frame_buffer->is_ready_sending == false)
			{
				if(frame_buffer->is_rendered == false)
				{
#ifdef DEBUG_FRAME_TIME
					static uint32_t prepare_start = 0;
					if(prepare_start == 0)
						prepare_start = HAL_GetTick();
#endif
					
					frame_buffer->is_rendered = _effect->FramePrepare(last_render + _frame_rate);
					
#ifdef DEBUG_FRAME_TIME
					if(frame_buffer->is_rendered == true)
					{
						DEBUG_LOG_TOPIC("FrameManager", "FramePrepare: %lu ms\n", (unsigned long)(HAL_GetTick() - prepare_start));
						prepare_start = 0;
					}
#endif
				}
				
				if(frame_buffer->is_rendered == true && time - last_render >= _frame_rate)
				{
					last_render += _frame_rate;
					
					if(time - last_render > _frame_rate / 4)
						last_render = time;
					
					frame_buffer->is_ready_sending = true;
					frame_buffer->is_rendered = false;
				}
			}
			
			return;
		}
		
		FrameBuffer *frame_buffer;
		
	private:
		
		FrameEffectInterface *_effect = nullptr;
		uint32_t _frame_rate;
		uint32_t last_tick = 0;
		uint32_t last_render = 0;
		
};
