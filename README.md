# Programming OSPI flash from SD card demo
Example application which reads file from SD card and writes it to the external OSPI flash

This application is built on VSCode Getting Started Template (alif_vscode-template)
The default hardware is Gen 2 Ensemble DevKit

Please follow the template project's [Getting started guide](https://github.com/alifsemi/alif_vscode-template/blob/main/doc/getting_started.md) to set up the environment.

## Brief description
The application initializes OSPI flash and opens the SD card media. Based on the source file size, SD card is first erased using sector erase and then application reads the source file and writes the contents to the flash using page writes.
- Azure FILEX provides the FAT file system implementation.
- A readily formatted SD card is expected. Partition should be smaller than 32GIB
- Default source filename is 'ext_flash.bin' and it is written to the start of OSPI flash address space
- printf is retargeted to UART
- Note/TODO: existing data in the end of last written sector is not preserved. This can be easily implemented by reading the contents of last sector to a temporary buffer and then writing it back with the added source data.
