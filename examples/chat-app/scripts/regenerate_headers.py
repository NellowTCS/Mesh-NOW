#!/usr/bin/env python3
import sys
from pathlib import Path

def write_header(input_path: Path, output_path: Path, var_name: str):
    data = input_path.read_bytes()
    size = len(data)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(f"#ifndef {var_name}_H\n")
        f.write(f"#define {var_name}_H\n\n")
        f.write("#include <stddef.h>\n\n")
        f.write("const unsigned char %s[] = {\n" % var_name)
        # write bytes grouped
        for i in range(0, size, 12):
            chunk = data[i:i+12]
            line = ', '.join(f"0x{b:02x}" for b in chunk)
            if i + 12 < size:
                f.write("    " + line + ",\n")
            else:
                f.write("    " + line + "\n")
        f.write("};\n\n")
        f.write(f"const size_t {var_name}_size = {size};\n\n")
        f.write(f"#endif // {var_name}_H\n")


def main():
    repo = Path(__file__).parent.parent
    dist = repo / 'frontend' / 'dist'
    out = repo / 'main'
    files = [
        ('index.html', 'index_html.h', 'INDEX_HTML'),
        ('bundle.js', 'bundle_js.h', 'BUNDLE_JS'),
        ('styles.css', 'styles_css.h', 'STYLES_CSS'),
    ]
    missing = []
    for src_name, out_name, var in files:
        src = dist / src_name
        dst = out / out_name
        if not src.exists():
            missing.append(src_name)
            continue
        write_header(src, dst, var)
        print(f"Wrote {dst}")
    if missing:
        print("Missing dist files:", missing)
        sys.exit(1)

if __name__ == '__main__':
    main()
