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

			// Отрицательные - фатальные: разбор блокируется до OpenFile() или SetReader()
			ERROR_INVALID_FILE = -1,
			ERROR_VERSION = -2,
			ERROR_SIZE = -3,
			ERROR_FORMAT = -4,
			ERROR_NOFRAMES = -5,
			ERROR_NO_READER = -6,

			// Положительные - восстановимые: сбрасываются при успешном открытии файла или успешном кадре
			ERROR_READ1_ZERO = 1,
			ERROR_READ2_ZERO = 2,
			ERROR_IDX_OVERFLOW = 3,
			ERROR_DATA_TRUNCATED = 4,
			ERROR_OPEN_FILE = 5,
			ERROR_READ_HEADER = 6,
		};

		PXLParser(uint8_t width, uint8_t height) : _cfg{width, height, false, false, 1000}
		{

		}

		// Устанавливает ридер и сбрасывает состояние как для нового файла. Старый ридер закрывается.
		// Файл открывается через OpenFile() или Tick() при SetAutoReopen(true). Можно вызывать на ходу.
		// Повторный вызов с тем же ридером начинает файл заново.
		void SetReader(PXLReaderInterface &reader)
		{
			_Close();

			_reader = &reader;
			_file.auto_open = true;
			_file.reopen_wait = false;
			_error = ERROR_OK;
			ReInit();

			return;
		}

		// Автоматическое открытие файла в Tick(). interval_ms - пауза между неудачными попытками,
		// чтобы недоступный источник не блокировал каждый Tick().
		void SetAutoReopen(bool state, uint32_t interval_ms = 1000)
		{
			_cfg.reopen = state;
			_cfg.reopen_interval = interval_ms;

			return;
		}

		// true - frame_timeout из файла игнорируется, каждый вызов GetAutoFrame() переходит к следующему кадру.
		void SetIgnoreFrameTimeout(bool state)
		{
			_cfg.ignore_frame_timeout = state;

			return;
		}

		// Открывает файл и начинает воспроизведение с начала. Открытый файл предварительно закрывается.
		// При ошибке ридер закрывается. Возвращает ошибку, она же доступна через GetError().
		error_t OpenFile()
		{
			if(_reader == nullptr)
				return _SetError(ERROR_NO_READER);

			if(_file.isopen == true)
				_Close();

			ReInit();
			_file.auto_open = true;

			if(_reader->Open() != 0)
				return _OpenFailed(ERROR_OPEN_FILE);

			if(_reader->Read(0, _file_header_size_bytes) != _file_header_size_bytes)
				return _OpenFailed(ERROR_READ_HEADER);

			const uint8_t *header = _reader->GetBufferPtr();
			if(_ParseFile(header) != ERROR_OK)
				return _OpenFailed(_error);

			_file.isopen = true;

			return _error;
		}

		// Закрывает файл. Close() ридера вызывается всегда, даже если файл уже закрыт, - для синхронизации с источником.
		// Отключает открытие в Tick() до следующего OpenFile() или SetReader().
		void CloseFile()
		{
			_file.auto_open = false;
			_Close();

			return;
		}

		// Начинает воспроизведение с первого кадра, не переоткрывая файл.
		void ReInit()
		{
			_pxl.frame_current = 0;
			_pxl.plays_count = 0;

			_file.offset_current_frame = _file_header_size_bytes;
			_file.offset_next_frame = _file_header_size_bytes;
			_file.fail_count = 0;

			_frame_last_draw_time = 0;

			memset(&_pxl.header_frame, 0x00, _frame_header_size_bytes);

			return;
		}

		// Вызывать периодически. При SetAutoReopen(true) открывает файл, если он не открыт, не закрыт через CloseFile()
		// и ошибка не фатальная. После неудачи следующая попытка не раньше чем через interval_ms.
		void Tick(uint32_t time)
		{
			if(_reader == nullptr || _file.isopen == true) return;
			if(_cfg.reopen == false || _file.auto_open == false || _error < ERROR_OK) return;
			if(_file.reopen_wait == true && (time - _file.reopen_time) < _cfg.reopen_interval) return;

			_file.reopen_time = time;
			_file.reopen_wait = (OpenFile() != ERROR_OK);

			return;
		}

		// Последняя ошибка.
		error_t GetError() const
		{
			return _error;
		}

		// true - файл открыт и заголовок принят.
		bool IsOpen() const
		{
			return _file.isopen;
		}

		// Номер последнего успешно разобранного кадра, 1..GetFrameCount(). 0 - ещё ничего не показано.
		uint16_t GetFrameCurrent() const
		{
			return _pxl.frame_current;
		}

		// Кол-во кадров в файле, 0 - файл не открыт.
		uint16_t GetFrameCount() const
		{
			return (_file.isopen == true) ? _pxl.header_file.img_frames : 0;
		}

		// true - все проигрывания завершены и показан последний кадр. При img_plays = 0 (бесконечно) всегда false.
		bool IsFinished() const
		{
			if(_file.isopen == false || _pxl.header_file.img_plays == 0) return false;

			return (_pxl.frame_current == _pxl.header_file.img_frames && _pxl.plays_count + 1 >= _pxl.header_file.img_plays);
		}

		// Разбирает очередной кадр и вызывает callback(const pxl_pixel_t &) для каждой серии пикселей (repeat >= 1).
		// Пропуски (repeat == 0) в callback не передаются, альфа передаётся как есть.
		// Возвращает true, если кадр разобран полностью. При false кадр мог быть нарисован частично.
		template<typename F>
		bool GetAutoFrame(uint32_t current_time, F &&callback)
		{
			if(_reader == nullptr) return false;
			if(_file.isopen == false) return false;
			if(_error < ERROR_OK) return false;

			// Конец кадра известен только после разбора всех его команд, назад - только через начало файла.
			// Поэтому следующий кадр выбирается до сдвига, а состояние фиксируется только при успешном разборе.
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
				_ParsingFailed(ERROR_READ1_ZERO, current_time);
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
						_ParsingFailed(ERROR_READ2_ZERO, current_time);
						return false;
					}

					buffer_ptr = _reader->GetBufferPtr();
					avail = read;
				}

				curr_pixel.repeat = 1;
				uint8_t add_offset = _pxl.pixel_type.Parser( buffer_ptr, curr_pixel );

				// Команда длиннее прочитанных данных
				if(add_offset > avail)
				{
					_ParsingFailed(ERROR_DATA_TRUNCATED, current_time);
					return false;
				}

				if(curr_pixel.repeat > 0)
				{
					// Вся серия должна помещаться в кадр
					if((uint32_t)curr_pixel.index + curr_pixel.repeat > pixels_total)
					{
						_ParsingFailed(ERROR_IDX_OVERFLOW, current_time);
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
			_file.fail_count = 0;
			_error = ERROR_OK;

			return true;
		}
		
	private:
		
		error_t _ParseFile(const uint8_t *data)
		{
			if( memcmp(data, "PXL", 3) != 0 )
				return _SetError(ERROR_INVALID_FILE);
			
			memcpy(&_pxl.header_file, data, _file_header_size_bytes);
			
			return _AnalysisFile();
		}
		
		error_t _AnalysisFile()
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
		
		error_t _SetError(error_t error)
		{
			_error = error;
			
			return _error;
		}
		
		// Ошибка открытия файла: запоминаем ошибку и закрываем ридер.
		error_t _OpenFailed(error_t error)
		{
			_SetError(error);
			_Close();
			
			return _error;
		}
		
		// Закрывает ридер, не меняя auto_open. Close() вызывается всегда - для синхронизации с источником.
		void _Close()
		{
			if(_reader != nullptr)
				_reader->Close();
			
			_file.isopen = false;
			
			return;
		}
		
		void _ParsingFailed(error_t error, uint32_t time)
		{
			_error = error;

			DEBUG_LOG_TOPIC("Failed", "code: %d\n", error);

			// 5 ошибок подряд - закрываем файл. Tick() откроет его снова не раньше чем через interval_ms.
			if(++_file.fail_count >= 5)
			{
				_file.fail_count = 0;
				_file.reopen_wait = true;
				_file.reopen_time = time;
				
				_Close();
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

			bool isopen = false;					// Флаг открытого файла: ридер открыт и заголовок принят
			bool auto_open = false;					// Tick() может открывать файл. Сбрасывается CloseFile()
			bool reopen_wait = false;				// Следующая попытка открытия - не раньше reopen_time + reopen_interval
			uint8_t fail_count = 0;					// Кол-во ошибок кадров подряд, после 5 файл закрывается
			uint32_t reopen_time = 0;				// Время последней неудачной попытки открытия или закрытия из-за ошибок
		} _file;

		struct cfg_t
		{
			const uint8_t width;					// Ширина экрана
			const uint8_t height;					// Высота экрана
			bool reopen;							// Флаг автоматического открытия файла в Tick()
			bool ignore_frame_timeout;				// Флаг игнорирования frame_timeout в PXL файле. Каждый вызов GetAutoFrame будет возвращать следующий кадр.
			uint32_t reopen_interval;				// Пауза между неудачными попытками открытия, мс
		} _cfg;
		
		PXLReaderInterface *_reader = nullptr;		// Указатель на Reader
		uint32_t _frame_last_draw_time = 0;			// Время рисование последнего кадра

		error_t _error = ERROR_OK;
		
};
