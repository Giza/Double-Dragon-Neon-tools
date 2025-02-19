def process_file(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Создаем новую строку с измененными символами
    new_content = ''
    for char in content:
        code = ord(char)
        # Проверяем заглавные буквы (0x401-0x42F)
        if 0x401 <= code <= 0x42F:
            new_code = code - 0x370
            new_content += chr(new_code)
        # Проверяем строчные буквы (0x430-0x451)
        elif 0x430 <= code <= 0x451:
            new_code = code - 0x370
            new_content += chr(new_code)
        else:
            new_content += char
    
    # Записываем результат в файл
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(new_content)

# Использование функции
process_file('songs.txt', 'songs_modified.txt')