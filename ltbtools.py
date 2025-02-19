# -*- coding: utf-8 -*-

ltbtools_version = "0.3"

import struct  # Для работы с бинарными структурами
import sys

def read_strings(ofs, b, output_file=None): 
    """
    Читает строки из бинарного файла .ltb
    
    Args:
        ofs (int): Начальное смещение в файле (88 байт)
        b (bytearray): Содержимое файла
        output_file (str, optional): Путь к файлу для записи результатов
    """
    file_size = len(b)
    output_strings = []
    
    # Читаем количество строк из первых 32 байт
    if ofs - 32 + 4 > file_size:
        print("Ошибка: Неожиданный конец файла при чтении количества строк")
        return
        
    num_strings = struct.unpack("I", b[ofs-56:ofs-52])[0]

    for i in range(num_strings):
        if ofs + 4 > file_size:
            print(f"Ошибка: Неожиданный конец файла при чтении смещения (позиция {ofs})")
            break
            
        loc = struct.unpack("I", bytes(b[ofs:ofs+4]))[0]
        loc = loc
        
        if loc >= file_size:
            print(f"Ошибка: Некорректное смещение строки: {loc} (размер файла: {file_size})")
            break
            
        end = b.find(0, loc)
        if end == -1:
            print(f"Ошибка: Не найден конец строки начиная с позиции {loc}")
            break
        
        try:
            s = b[loc:end].decode('utf-8')
        except UnicodeDecodeError:
            print(f"Ошибка декодирования строки в позиции {loc}")
            continue
            
        s = s.replace('\n','\\n')
        output_strings.append(s)
        ofs += 4

    if output_file:
        try:
            with open(output_file, 'w', encoding='utf-8') as f:
                for s in output_strings:
                    f.write(s + '\n')
        except IOError as e:
            print(f"Ошибка при записи в файл {output_file}: {e}")
    else:
        for s in output_strings:
            print(s)

def write_strings(ofs, start_line, lines, original_b, input_file):
    """
    Создает новый .ltb файл на основе оригинального, заменяя строки
    
    Args:
        ofs (int): Начальное смещение в файле
        start_line (int): Номер начальной строки
        lines (int): Количество строк для записи
        original_b (bytearray): Содержимое оригинального файла
        input_file (str): Путь к входному текстовому файлу
    """
    # Создаем новый буфер, копируя заголовок из оригинального файла
    b = bytearray(original_b[:ofs])
    
    # Читаем количество строк из оригинального файла
    num_strings = struct.unpack("I", original_b[ofs-56:ofs-52])[0]
    
    # Создаем список для хранения всех строк и их смещений
    strings_data = []
    offsets_end = ofs + (num_strings * 4)  # Конец таблицы смещений
    
    # Добавляем выравнивание после таблицы смещений
    padding_size = 16  # Минимальный размер выравнивания
    if offsets_end % 16 != 0:
        padding_size = 16 - (offsets_end % 16)
    
    current_offset = offsets_end + padding_size  # Начальное смещение для строк с учетом выравнивания
    
    # Читаем строки из файла
    with open(input_file, 'r', encoding='utf-8') as f:
        for s in f:
            s = s.rstrip('\r\n')
            s = s.replace('\\n','\n')
            
            # Кодируем строку в UTF-8 и добавляем нулевой байт
            try:
                w = bytearray(s.encode('utf-8'))
            except UnicodeError as e:
                print(f"Ошибка кодирования строки: {s}")
                print(f"Ошибка: {e}")
                continue
            w.append(0)
            
            # Выравнивание по 4 байтам
            while len(w) % 4 != 0:
                w.append(0)
                
            # Сохраняем данные строки
            strings_data.append((current_offset, w))
            current_offset += len(w)
    
    # Записываем таблицу смещений
    for offset, _ in strings_data:
        b.extend(struct.pack("I", offset))
    
    # Добавляем выравнивание после таблицы смещений
    b.extend(bytearray(padding_size))
    
    # Записываем сами строки
    for _, string_data in strings_data:
        b.extend(string_data)
        
    # Добавляем выравнивание после всех строк
    current_size = len(b)
    end_padding_size = 16  # Минимальный размер выравнивания
    if current_size % 16 != 0:
        end_padding_size = 16 - (current_size % 16)
    b.extend(bytearray(end_padding_size))
    
    # Читаем смещение конца секции строк из оригинального файла
    end_offset = struct.unpack("I", original_b[20:24])[0]
    lang_offset = struct.unpack("I", original_b[28:32])[0]
    lang_offset = lang_offset - end_offset
    
    # Обновляем смещение в заголовке с учетом всех выравниваний
    new_offset = current_size + end_padding_size
    lang_new_offset = new_offset + lang_offset
    b[20:24] = struct.pack("I", new_offset)
    b[28:32] = struct.pack("I", lang_new_offset)
    
    print(f"Размер буфера: {len(b)}")
    print(f"Новое смещение (new_offset): {new_offset}")
    print(f"Старое смещение (end_offset): {end_offset}")
    print(f"Разница смещений: {new_offset - end_offset}")
    
    # Копируем остаток файла после секции строк
    b.extend(original_b[end_offset:])
    
    print(f"Размер буфера после копирования остатка: {len(b)}")
    
    # Читаем количество оффсетов из начала файла
    num_data_offsets = struct.unpack("I", original_b[16:20])[0]
    num_last_offsets = struct.unpack("I", original_b[24:28])[0]
    
    print(f"Количество первых оффсетов: {num_data_offsets}")
    print(f"Количество последних оффсетов: {num_last_offsets}")
    
    # Вычисляем разницу между старым и новым смещением
    offset_difference = new_offset - end_offset
    
    # Корректируем первую группу оффсетов
    first_offsets_start = new_offset  # Начало первой группы оффсетов
    print("\nКорректировка первой группы оффсетов:")
    for i in range(num_data_offsets):
        offset_pos = first_offsets_start + (i * 4)
        old_offset = struct.unpack("I", b[offset_pos:offset_pos+4])[0]
        new_data_offset = old_offset + offset_difference
        print(f"Оффсет {i}: {old_offset} -> {new_data_offset}")
        b[offset_pos:offset_pos+4] = struct.pack("I", new_data_offset)
    
    # Корректируем вторую группу оффсетов, начиная с позиции num_last_offsets
    second_offsets_start = lang_new_offset  # Используем num_last_offsets как смещение
    print("\nКорректировка второй группы оффсетов:")
    for i in range(num_last_offsets):
        offset_pos = second_offsets_start + (i * 4)
        old_offset = struct.unpack("I", b[offset_pos:offset_pos+4])[0]
        new_data_offset = old_offset + offset_difference
        print(f"Оффсет {i}: {old_offset} -> {new_data_offset}")
        b[offset_pos:offset_pos+4] = struct.pack("I", new_data_offset)
    
    return b

if __name__=='__main__':
    if len(sys.argv)<3:
        print("WayForward's Adventure Time text resource files (.ltb) tool ver. %s" % ltbtools_version)
        print("Usage: ltbtools.py localization.ltb output.txt")
        print("       ltbtools.py --write localization.ltb input.txt")
        print("Note: When writing, creates a new file with '_new' suffix instead of modifying the original")
    else:
        write = False
        input_file = None
        output_file = None

        # Разбор аргументов командной строки
        for i, arg in enumerate(sys.argv):
            if arg=='--write':
                write = True
                if i + 2 < len(sys.argv):
                    fname = sys.argv[i + 1]
                    input_file = sys.argv[i + 2]
            elif i == 1 and not write:
                fname = arg
                if i + 1 < len(sys.argv):
                    output_file = sys.argv[i + 1]

        try:
            b = bytearray(open(fname,'rb').read())
            if len(b) < 0x40 + 8:
                print("Ошибка: Файл слишком мал или поврежден")
                sys.exit(1)

            ofs = 64

            if write:
                if not input_file:
                    print("Ошибка: Не указан входной текстовый файл")
                    sys.exit(1)
                new_fname = fname.rsplit('.', 1)[0] + '_new.ltb'
                new_content = write_strings(ofs, 0, 0, b, input_file)
                with open(new_fname, 'wb') as f:
                    f.write(new_content)
                print(f"Файл сохранен как: {new_fname}")
            else:
                read_strings(ofs, b, output_file)
        except IOError as e:
            print(f"Ошибка при работе с файлом: {e}")
            sys.exit(1)
        except struct.error as e:
            print(f"Ошибка в структуре файла: {e}")
            sys.exit(1)
