def replace_chars(input_file, output_file):
    # Словарь для замены символов
    replacements = {
        'А': 'A', 'Б': 'B', 'В': 'C', 'Д': 'D', 'З': 'E', 'И': 'F',
        'К': 'G', 'Л': 'H', 'М': 'I', 'Н': 'J', 'О': 'K', 'П': 'L',
        'Р': 'M', 'С': 'N', 'У': 'O', 'Ф': 'P', 'Х': 'Q', 'Ц': 'R',
        'Ь': 'S', 'а': 'T', 'б': 'U', 'в': 'V', 'г': 'W', 'д': 'X',
        'е': 'Y', 'ж': 'Z', 'з': 'a', 'и': 'b', 'й': 'c', 'к': 'd',
        'л': 'e', 'м': 'f', 'н': 'g', 'о': 'h', 'п': 'i', 'р': 'j',
        'с': 'k', 'т': 'l', 'у': 'm', 'ф': 'n', 'х': 'o', 'ц': 'p',
        'ч': 'q', 'ш': 'r', 'щ': 's', 'ъ': 't', 'ы': 'u', 'ь': 'v',
        'э': 'w', 'ю': 'x', 'я': 'y', 'ё': 'z'
    }
    
    try:
        # Читаем исходный файл
        with open(input_file, 'r', encoding='utf-8') as file:
            text = file.read()
        
        # Заменяем символы
        for rus, eng in replacements.items():
            text = text.replace(rus, eng)
        
        # Записываем результат в новый файл
        with open(output_file, 'w', encoding='utf-8') as file:
            file.write(text)
            
        print(f"Замена символов успешно выполнена. Результат сохранен в {output_file}")
        
    except Exception as e:
        print(f"Произошла ошибка: {str(e)}")

# Использование программы
input_file = 'menu_text_conv.txt'  # путь к вашему входному файлу
output_file = 'menu_text_conv_converted.txt'  # путь к выходному файлу

replace_chars(input_file, output_file)