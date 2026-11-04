#pragma once
#include <inttypes.h>
#include <string.h>
#include "PXLFormat_v2.h"
#include <PXLReaderInterface.h>

class PXLParser
{
	static const uint8_t _file_version = 3;
	static const uint8_t _file_version_legacy = 2;		// Отличается только смыслом поля img_repeats / img_plays

	static constexpr uint8_t _file_header_size_bytes = sizeof(file_header_t);
	static constexpr uint8_t _frame_header_size_bytes = sizeof(file_frame_t);

	public:

		enum error_t : int8_t
		{
			ERROR_OK = 0,
			ERROR_INVALID_FILE = -1,
			ERROR_VERSION = -2,
			ERROR_SIZE = -3,
			ERROR_FORMAT = -4,
			ERROR_NOFRAMES = -5,
			ERROR_OPEN_FILE = -6,
			ERROR_READ1_ZERO = 1,
			ERROR_READ2_ZERO = 2,
			ERROR_IDX_OVERFLOW = 3,
			ERROR_DATA_TRUNCATED = 4,
		};
		
		PXLParser(uint8_t width, uint8_t height) : _cfg{width, height, false, false}
		{

		}
		
		void SetReader(PXLReaderInterface &reader)
		{
			#warning Need reset file
			_reader = &reader;

			return;
		}
		
		// В случае ошибки в процессе чтения файла, если true то файл будет переоткрыт и начнёться с начала.
		// Если false, то фалй будет просто закрыт
		void SetAutoReopen(bool state)
		{
			_cfg.reopen = state;
			
			return;
		}
		
		void SetIgnoreFrameTimeout(bool state)
		{
			_cfg.ignore_frame_timeout = state;
			
			return;
		}
		
		error_t OpenFile()
		{
			ReInit();
			if(_reader->Open() == 0)
			{
				if(_reader->Read(0, _file_header_size_bytes) == _file_header_size_bytes)
				{
					_file.buffer_ptr = _reader->GetBufferPtr();
					_file.isopen = true;

					_ParseFile();
				}
				else
				{
					_SetError(ERROR_OPEN_FILE);
				}
			}
			else
			{
				_SetError(ERROR_OPEN_FILE);
			}
			
			return _error;
		}
		
		error_t ReOpenFile()
		{
			CloseFile();
			OpenFile();
			
			return _error;
		}
		
		void CloseFile()
		{
			_reader->Close();
			_file.isopen = false;
			
			return;
		}

		void ReInit()
		{
			_pxl.frame_current = 0;
			_pxl.plays_count = 0;

			_file.offset_current_frame = _file_header_size_bytes;
			_file.offset_next_frame = _file_header_size_bytes;
			_file.read_attempts = 0;

			_frame_last_draw_time = 0;

			memset(&_pxl.header_frame, 0x00, _frame_header_size_bytes);

			return;
		}
		
		void Tick(uint32_t time)
		{
			if(_file.isopen == false)
			{
				if(_cfg.reopen == true)
				{
					ReOpenFile();
				}
			}
			
			return;
		}
		
		// Разбирает очередной кадр и вызывает callback(const pxl_pixel_t &) для каждой серии пикселей (repeat >= 1).
		// Пропуски (repeat == 0) в callback не передаются. Альфа передаётся как есть, решение о ней принимает вызывающий.
		// Возвращает true, если кадр разобран полностью. При false кадр мог быть нарисован частично.
		template<typename F>
		bool GetAutoFrame(uint32_t current_time, F &&callback)
		{
			if(_reader == nullptr) return false;
			if(_file.isopen == false) return false;
			if(_error < ERROR_OK) return false;

			// Формат последовательный: конец кадра известен только после разбора всех его команд,
			// назад можно только через начало файла. Решение о следующем кадре принимается до сдвига.
			// Состояние (_pxl.frame_current, _pxl.plays_count, смещения) фиксируется только при успешном разборе.
			uint16_t frame_current = _pxl.frame_current;
			uint16_t plays_count = _pxl.plays_count;
			uint32_t offset = _file.offset_current_frame;

			// Если пришло время рисовать следующий кадр.
			if (frame_current == 0 || _cfg.ignore_frame_timeout || current_time - _frame_last_draw_time > _pxl.header_frame.frame_timeout)
			{
				// Есть следующий кадр в файле
				if(frame_current < _pxl.header_file.img_frames)
				{
					offset = _file.offset_next_frame;
					++frame_current;
				}
				// Последний кадр показан, начинаем новое проигрывание: 0 - бесконечно, иначе пока не проиграли img_plays раз
				else if(_pxl.header_file.img_plays == 0 || plays_count + 1 < _pxl.header_file.img_plays)
				{
					offset = _file_header_size_bytes;
					frame_current = 1;
					if(_pxl.header_file.img_plays != 0)
						++plays_count;
				}
				// Иначе проигрывания закончились, остаёмся на последнем кадре (offset = текущий кадр)
			}

			const uint32_t offset_frame = offset;

			// Читаем заголовок кадра и сколько получится данных за ним
			uint32_t read = _reader->Read(offset);
			if(read < _frame_header_size_bytes)
			{
				_ParsingFailed(ERROR_READ1_ZERO);
				return false;
			}

			const uint8_t *buffer_ptr = _reader->GetBufferPtr();

			file_frame_t header_frame;
			memcpy(&header_frame, buffer_ptr, _frame_header_size_bytes);
			offset += _frame_header_size_bytes;
			buffer_ptr += _frame_header_size_bytes;

			// Кол-во ещё не разобранных байт в буфере ридера
			uint32_t avail = read - _frame_header_size_bytes;

			// Размер команды в байтах, максимально возможный
			const uint8_t op_size_max = _pxl.pixel_type.size_pixel;

			const uint32_t pixels_total = (uint32_t)_cfg.width * _cfg.height;

			// index = 0xFFFF, чтобы первая команда без индекса (index += 1) попала в ячейку 0.
			// Регистр цвета в начале каждого кадра {0,0,0,0}.
			pxl_pixel_t curr_pixel = {0xFFFF, 1, {}};

			for(uint16_t op_idx = 0; op_idx < header_frame.frame_ops; ++op_idx)
			{
				// В буфере может не хватить на команду - дочитываем. Длину берём по максимальному размеру команды,
				// ридер сам ограничит её размером своего буфера и концом файла.
				if(avail < op_size_max)
				{
					uint32_t length = (uint32_t)(header_frame.frame_ops - op_idx) * op_size_max;

					read = _reader->Read(offset, length);
					if(read == 0)
					{
						_ParsingFailed(ERROR_READ2_ZERO);
						return false;
					}

					buffer_ptr = _reader->GetBufferPtr();
					avail = read;
				}

				curr_pixel.repeat = 1;
				uint8_t add_offset = _pxl.pixel_type.Parser( buffer_ptr, curr_pixel );

				// Команда оказалась длиннее прочитанных данных: короткое чтение не в конце файла или обрезанный файл.
				if(add_offset > avail)
				{
					_ParsingFailed(ERROR_DATA_TRUNCATED);
					return false;
				}

				if(curr_pixel.repeat > 0)
				{
					// Вся серия должна помещаться в кадр
					if((uint32_t)curr_pixel.index + curr_pixel.repeat > pixels_total)
					{
						_ParsingFailed(ERROR_IDX_OVERFLOW);
						return false;
					}

					callback(curr_pixel);

					// index указывает на последний записанный пиксель серии
					curr_pixel.index += curr_pixel.repeat - 1;
				}

				buffer_ptr += add_offset;
				offset += add_offset;
				avail -= add_offset;
			}

			// Кадр разобран полностью, фиксируем состояние
			if(frame_current != _pxl.frame_current || offset_frame != _file.offset_current_frame)
				_frame_last_draw_time = current_time;

			_pxl.header_frame = header_frame;
			_pxl.frame_current = frame_current;
			_pxl.plays_count = plays_count;
			_file.offset_current_frame = offset_frame;
			_file.offset_next_frame = offset;
			_file.read_attempts = 0;
			_error = ERROR_OK;

			return true;
		}
		
	private:
		
		void _ParseFile()
		{
			if( memcmp(_file.buffer_ptr, "PXL", 3) != 0 )
				return _SetError(ERROR_INVALID_FILE);
			
			memcpy(&_pxl.header_file, _file.buffer_ptr, _file_header_size_bytes);
			
			_AnalysisFile();
			
			return;
		}
		
		void _AnalysisFile()
		{
			if(_pxl.header_file.version != _file_version && _pxl.header_file.version != _file_version_legacy)
				return _SetError(ERROR_VERSION);
			
			if(_pxl.header_file.size_x != _cfg.width || _pxl.header_file.size_y != _cfg.height)
				return _SetError(ERROR_SIZE);
			
			_pxl.pixel_type = file_pixel_type[ _pxl.header_file.format_color ];
			if(_pxl.header_file.format_strip != 0 || _pxl.pixel_type.size_pixel == 0)
				return _SetError(ERROR_FORMAT);
			
			if(_pxl.header_file.img_frames == 0)
				return _SetError(ERROR_NOFRAMES);
			
			// Версия 2: img_repeats, где 0 и 1 - один раз, 255 - бесконечно. Приводим к img_plays (0 - бесконечно).
			if(_pxl.header_file.version == _file_version_legacy)
			{
				uint8_t repeats = _pxl.header_file.img_plays;
				_pxl.header_file.img_plays = (repeats == 255) ? 0 : ((repeats == 0) ? 1 : repeats);
			}

			return _SetError(ERROR_OK);
		}
		
		void _SetError(error_t error)
		{
			_error = error;
			
			return;
		}
		
		void _ParsingFailed(error_t error)
		{
			_error = error;

			DEBUG_LOG_TOPIC("Failed", "code: %d\n", error);

			// 5 ошибок подряд (счётчик сбрасывается при успешном кадре) - закрываем файл, Tick() переоткроет его при SetAutoReopen(true)
			if(++_file.read_attempts >= 5)
			{
				_file.read_attempts = 0;

				CloseFile();
			}
			
			return;
		}
		
		struct pxl_t
		{
			file_header_t header_file;				// Заголовок файла, структура
			file_frame_t header_frame;				// Заголовок кадра, структура
			file_pixel_type_t pixel_type;			// Конфигурация пикселя + Парсер, структура
			uint16_t frame_current;					// Номер текущего кадра, 1..img_frames (0 - ещё ничего не показано)
			uint16_t plays_count;					// Кол-во завершённых проигрываний анимации
		} _pxl;

		struct file_t
		{
			uint32_t offset_current_frame;			// Смещение до текущего кадра, байт
			uint32_t offset_next_frame;				// Смещение до следующего кадра, байт

			const uint8_t *buffer_ptr = nullptr;	// Указатель на буфер

			bool isopen = false;					// Флаг открытого файла
			uint8_t read_attempts = 0;				// Кол-во попыток чтения файла, после чего он закрывается
		} _file;

		struct cfg_t
		{
			const uint8_t width;					// Ширина экрана
			const uint8_t height;					// Высота экрана
			bool reopen;							// Флаг автоматического переоткрытия файла в случае ошибок чтения
			bool ignore_frame_timeout;				// Флаг игнорирования frame_timeout в PXL файле. Каждый вызов GetAutoFrame будет возвращать следующий кадр.
		} _cfg;
		
		PXLReaderInterface *_reader = nullptr;		// Указатель на Reader
		uint32_t _frame_last_draw_time = 0;			// Время рисование последнего кадра

		error_t _error = ERROR_OK;
		
};
