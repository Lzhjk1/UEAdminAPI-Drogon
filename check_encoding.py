# 遍历检测目录下所有.h .cpp .cc文件的编码
import os
import chardet
import sys

abnormal_files = []


def detect_encoding(file_path):
    try:
        with open(file_path, 'rb') as f:
            # Read a chunk of the file, not the whole thing for performance
            raw_data = f.read(10240)

            result = chardet.detect(raw_data)
            if result and result['encoding']:
                return result['encoding']
            else:
                return 'unknown'
    except Exception as e:
        print(f'error: {e}', file=sys.stderr)
        return 'unknown'


def detect_encoding_in_directory(directory_path):
    for root, dirs, files in os.walk(directory_path):
        for file in files:

            if file.endswith(('.h', '.cpp', '.cc')):
                file_path = os.path.join(root, file)
                encoding = detect_encoding(file_path)
                if encoding.lower() not in ('utf-8', 'ascii'):
                    abnormal_files.append((file_path, encoding))


if __name__ == '__main__':
    # if len(sys.argv) < 2:
    #     print('Usage: python detect_encoding.py <directory_path>')
    #     sys.exit(1)

    directory_path = "./"
    detect_encoding_in_directory(directory_path)

    if abnormal_files:
        print('\nAbnormal files:')
        for file_path, encoding in abnormal_files:
            print(f'{file_path}: {encoding}')