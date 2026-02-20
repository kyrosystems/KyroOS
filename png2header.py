from PIL import Image
import sys

def png_to_c_array(png_file, output_file, scale=0.5):
    img = Image.open(png_file).convert('RGBA')
    
    # Зменшуємо картинку
    new_width = int(img.width * scale)
    new_height = int(img.height * scale)
    img = img.resize((new_width, new_height), Image.LANCZOS)
    
    width, height = img.size
    pixels = list(img.getdata())
    
    with open(output_file, 'w') as f:
        f.write('#ifndef IMAGE_H\n')
        f.write('#define IMAGE_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'#define IMAGE_WIDTH {width}\n')
        f.write(f'#define IMAGE_HEIGHT {height}\n\n')
        f.write('static const uint32_t sample_image_pixels[] = {\n')
        
        for i, (r, g, b, a) in enumerate(pixels):
            # ПРАВИЛЬНИЙ формат: 0xAARRGGBB (не інвертований)
            pixel_value = (a << 24) | (r << 16) | (g << 8) | b
            f.write(f'    0x{pixel_value:08X}')
            if i < len(pixels) - 1:
                f.write(',')
            if (i + 1) % 8 == 0:
                f.write('\n')
            else:
                f.write(' ')
        
        f.write('\n};\n\n')
        f.write('#endif // IMAGE_H\n')
    
    print(f'Converted {png_file} to {output_file}')
    print(f'Image size: {width}x{height} (scaled by {scale})')

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Usage: python png2header.py input.png output.h [scale]')
        print('Example: python png2header.py epstein.png src/include/image.h 0.5')
        sys.exit(1)
    
    scale = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    png_to_c_array(sys.argv[1], sys.argv[2], scale)
