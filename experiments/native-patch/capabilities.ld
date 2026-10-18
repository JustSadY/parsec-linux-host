/* ELF RVAs. Ghidra's image base for this analysis was 0x100000. */
json_object = 0x94600;
json_null = 0x94160;
json_bool = 0x94190;
json_string = 0x94400;
json_add = 0x946a0;
machine_hostname = 0xa2260;
SECTIONS {
  . = 0x2cc60;
  .replacement : {
    *(.text.rebuilt_capabilities)
    *(.rodata*)
  }
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
