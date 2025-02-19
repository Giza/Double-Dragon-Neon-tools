#!/usr/bin/env python3
import os
import sys
import struct
import argparse
from pathlib import Path

VERSION = "1.5.2"
PADDING = 8
LIST_ONLY = False
CLIP_PATH = False

SIG_LINK = 0
SIG_DATA = 1

def make_sig(sig_type: int, align: int) -> bytes:
    signatures = [
        [
            "FILELINK",
        ],
        [
            "MANAGEDFILE_DATABLOCK_USED_IN_ENGINE_________________________END"
        ]
    ]
    return signatures[sig_type][1 if align == 8 else 0].encode('ascii')

def write_to_file(path: str, data: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(path, 'wb') as f:
        f.write(data)

def fix_path(directory: str, src: str) -> str:
    # Преобразование пути как в оригинальной программе
    # example: dir='a/b' src='a/b/c/d/e' => result='c:d:e'
    rel_path = os.path.relpath(src, directory)
    parts = rel_path.split(os.sep)
    if len(parts) > 1:
        result = parts[0] + ':' + ':'.join(parts[1:])
    else:
        result = parts[0]
    return result

def unpack(filename: str, output_dir: str = None, files_filter: list = None) -> int:
    global PADDING
    
    if not output_dir:
        output_dir = f"{filename}_extracted"

    with open(filename, 'rb') as f:
        loc = struct.unpack('<I', f.read(4))[0]
        num_files = struct.unpack('<I', f.read(4))[0]
        
        offset = 8
        unpacked = 0
        
        for _ in range(num_files):
            f.seek(offset)
            
            # Пробуем сначала короткую сигнатуру
            short_sig = make_sig(SIG_LINK, 4)
            sig = f.read(len(short_sig))
            
            if sig == short_sig:
                PADDING = 4
            else:
                # Если не совпала короткая, проверяем длинную
                f.seek(offset)
                long_sig = make_sig(SIG_LINK, 8)
                sig = f.read(len(long_sig))
                if sig == long_sig:
                    PADDING = 8
                else:
                    return -1
                
            at = struct.unpack('<I', f.read(4))[0]
            size = struct.unpack('<I', f.read(4))[0]
            
            name = b''
            while True:
                char = f.read(1)
                if char == b'\0':
                    break
                name += char
            name = name.decode()
            
            # Преобразуем путь с двоеточиями в путь с разделителями ОС для распаковки
            if not CLIP_PATH:
                name = name.replace(':', os.sep)
            
            offset = f.tell()
            while offset % PADDING != 0:
                offset += 1
                
            sig_data_len = len(make_sig(SIG_DATA, PADDING))
            at += loc + sig_data_len
            
            f.seek(at)
            data = f.read(size)
            
            if LIST_ONLY:
                print(f"{at:10d} {size:10d} {name}")
                continue
                
            out_path = name if CLIP_PATH else os.path.join(output_dir, name)
            
            if not files_filter or name in files_filter:
                write_to_file(out_path, data)
                unpacked += 1
                
    if not LIST_ONLY:
        print(f"Unpacked {unpacked} file(s) into {output_dir}{' (compact allocation)' if PADDING < 8 else ''}")
        
    return 0

def pack(directory: str, output_file: str = None) -> int:
    if not output_file:
        output_file = f"{os.path.splitext(directory)[0]}.pak"
        
    files = []
    for root, _, filenames in os.walk(directory):
        for filename in filenames:
            files.append(os.path.join(root, filename))
            
    if not files:
        print("No files, exiting...")
        return -1
        
    with open(output_file, 'wb') as f:
        # Заполнители для последующего обновления
        f.write(struct.pack('<II', 0, len(files)))
        
        offset = 8
        file_entries = []
        
        # Записываем заголовки файлов
        for filepath in files:
            sig = make_sig(SIG_LINK, PADDING)
            f.write(sig)
            f.write(struct.pack('<II', 0, 0))  # Placeholder for at/size
            
            name = fix_path(directory, filepath)
            f.write(name.encode() + b'\0')
            
            entry_size = len(sig) + 8 + len(name) + 1
            offset += entry_size
            
            while offset % PADDING != 0:
                f.write(b'\x3f')
                offset += 1
                
            file_entries.append({'path': filepath, 'name': name})
            
        while offset % (PADDING * 2) != 0:
            f.write(b'\x3f')
            offset += 1
            
        loc = offset
        
        # Записываем данные файлов
        for entry in file_entries:
            with open(entry['path'], 'rb') as inf:
                data = inf.read()
                
            sig = make_sig(SIG_DATA, PADDING)
            f.write(sig)
            
            entry['at'] = offset - loc
            entry['size'] = len(data)
            
            f.write(data)
            
            offset += len(sig) + len(data)
            while offset % PADDING != 0:
                f.write(b'\x3f')
                offset += 1
                
        while offset % (PADDING * 4) != 0:
            f.write(b'\x3f')
            offset += 1
            
        # Обновляем заголовок и смещения файлов
        f.seek(0)
        f.write(struct.pack('<II', loc, len(files)))
        
        offset = 8
        for entry in file_entries:
            offset += len(make_sig(SIG_LINK, PADDING))
            f.seek(offset)
            f.write(struct.pack('<II', entry['at'], entry['size']))
            offset += 8 + len(entry['name']) + 1
            while offset % PADDING != 0:
                offset += 1
                
    print(f"Packed {len(files)} file(s) into {output_file}")
    return 0

def main():
    global PADDING, LIST_ONLY, CLIP_PATH
    
    parser = argparse.ArgumentParser(description=f'WayForward Engine resource packer (for Duck Tales Remastered, etc.) ver. {VERSION}')
    parser.add_argument('input', help='Input .pak file or directory')
    parser.add_argument('output', nargs='?', help='Output directory or .pak file')
    parser.add_argument('files', nargs='*', help='Optional list of files to extract')
    parser.add_argument('-c', action='store_true', help='compact (BloodRayne) allocation')
    parser.add_argument('-l', action='store_true', help='list files without unpacking')
    parser.add_argument('-p', action='store_true', help='extract without paths')
    
    args = parser.parse_args()
    
    if args.c:
        PADDING = 4
    if args.l:
        LIST_ONLY = True
    if args.p:
        CLIP_PATH = True
        
    if os.path.isdir(args.input):
        return pack(args.input, args.output)
    else:
        res = unpack(args.input, args.output, args.files)
        if res < 0 and PADDING == 8:
            PADDING = 4
            res = unpack(args.input, args.output, args.files)
        return res

if __name__ == '__main__':
    sys.exit(main()) 