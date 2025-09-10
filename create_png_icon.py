#!/usr/bin/env python3
"""
Simple script to create a PNG icon for the Snippet Manager
"""

try:
    from PIL import Image, ImageDraw, ImageFont
    
    # Create a 128x128 image
    size = 128
    img = Image.new('RGBA', (size, size), (44, 62, 80, 255))  # Dark blue background
    draw = ImageDraw.Draw(img)
    
    # Draw document background
    doc_x, doc_y = 20, 16
    doc_w, doc_h = 88, 96
    draw.rounded_rectangle([doc_x, doc_y, doc_x + doc_w, doc_y + doc_h], 
                          radius=8, fill=(236, 240, 241, 255), outline=(189, 195, 199, 255), width=2)
    
    # Draw header line
    draw.rounded_rectangle([28, 28, 100, 36], radius=4, fill=(52, 152, 219, 255))
    
    # Draw text lines
    lines = [(28, 44, 84, 48), (28, 52, 92, 56), (28, 60, 76, 64), (28, 68, 88, 72)]
    for x1, y1, x2, y2 in lines:
        draw.rounded_rectangle([x1, y1, x2, y2], radius=2, fill=(127, 140, 141, 255))
    
    # Draw code block
    draw.rounded_rectangle([28, 80, 100, 100], radius=4, fill=(52, 73, 94, 255))
    
    # Draw code lines in different colors
    code_lines = [
        (32, 84, 56, 86, (231, 76, 60, 255)),   # Red
        (32, 88, 64, 90, (243, 156, 18, 255)),  # Orange
        (32, 92, 52, 94, (39, 174, 96, 255)),   # Green
        (32, 96, 60, 98, (155, 89, 182, 255))   # Purple
    ]
    
    for x1, y1, x2, y2, color in code_lines:
        draw.rounded_rectangle([x1, y1, x2, y2], radius=1, fill=color)
    
    # Draw snippet icon (circle with lines)
    draw.ellipse([80, 16, 112, 48], fill=(231, 76, 60, 255))
    
    # Draw lines in the circle
    for i, y in enumerate([24, 28, 32]):
        draw.rounded_rectangle([84, y, 108, y + 2], radius=1, fill=(255, 255, 255, 255))
    
    # Save the images
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    resources_dir = os.path.join(script_dir, 'resources')
    
    # Save PNG
    icon_path = os.path.join(resources_dir, 'icon.png')
    img.save(icon_path, 'PNG')
    print(f"PNG icon created successfully at: {icon_path}")
    
    # Create different sizes for better platform support
    sizes = [(32, 'icon-32.png'), (64, 'icon-64.png')]
    for size, filename in sizes:
        resized = img.resize((size, size), Image.Resampling.LANCZOS)
        resized_path = os.path.join(resources_dir, filename)
        resized.save(resized_path, 'PNG')
        print(f"{size}x{size} PNG icon created at: {resized_path}")
    
    # Create ICO file for Windows (multiple sizes in one file)
    try:
        ico_path = os.path.join(resources_dir, 'icon.ico')
        ico_sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128)]
        ico_images = []
        
        for ico_size in ico_sizes:
            ico_img = img.resize(ico_size, Image.Resampling.LANCZOS)
            ico_images.append(ico_img)
        
        # Save as ICO with multiple sizes
        ico_images[0].save(ico_path, format='ICO', sizes=[(img.width, img.height) for img in ico_images])
        print(f"ICO icon created successfully at: {ico_path}")
        
    except Exception as ico_error:
        print(f"Warning: Could not create ICO file: {ico_error}")
        print("ICO creation requires Pillow with ICO support")
    
except ImportError:
    print("PIL (Pillow) not installed. Install with: pip install Pillow")
except Exception as e:
    print(f"Error creating PNG icon: {e}")