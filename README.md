# fbprinter
Prints text to framebuffer using `/dev/mem`. Requires certain kernel config options to be enabled. This config was proven working by the author:
```
CONFIG_DEVMEM=y
CONFIG_ARCH_HAS_DEVMEM_IS_ALLOWED=y
# CONFIG_STRICT_DEVMEM is not set
```
## How to use
```
Usage: fbprinter <fb_address>,<fb_width>,<fb_height> <filename.txt> [font_scale]
Example call:
echo "HELLO WORLD" > /tmp/file.txt
fbprinter 9e000000,1080,2408 /tmp/file.txt 2
```
