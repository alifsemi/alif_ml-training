import argparse
import os

parser = argparse.ArgumentParser(description='Convert a TFLite model to a C byte array.')
parser.add_argument('-i', required=True, metavar='input.tflite', help='Input .tflite file')
parser.add_argument('-o', metavar='output.c', help='Output .c file (default: input name with .c extension)')
parser.add_argument('-a', default='model_tflite', metavar='array_name', help='C array name')
args = parser.parse_args()

input_path = args.i
output_path = args.o if args.o else os.path.splitext(input_path)[0] + '.c'
array_name = args.a

data = open(input_path, 'rb').read()
print(f'File size: {len(data)} bytes')

lines = [f'const unsigned char __attribute__((aligned(16), section(".rodata.model"))) {array_name}[] = {{']
for i in range(0, len(data), 12):
    chunk = data[i:i+12]
    hex_vals = ', '.join(f'0x{b:02x}' for b in chunk)
    lines.append(f'  {hex_vals}, ')
lines.append('};')
lines.append(f'const unsigned int {array_name}_len = {len(data)};')

with open(output_path, 'w') as f:
    f.write('\n'.join(lines) + '\n')
print(f'Written to {output_path}')
